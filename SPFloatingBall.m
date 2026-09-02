#import "SPFloatingBall.h"
#import "SPSpeedManager.h"
#import "SPControlPanel.h"

@interface SPFloatingBall ()
@property (nonatomic, strong) UIView *ballView;
@property (nonatomic, strong) UILabel *speedLabel;
@property (nonatomic, strong) SPControlPanel *controlPanel;
@property (nonatomic, assign) CGPoint startPoint;
@property (nonatomic, assign) BOOL isDragging;
@property (nonatomic, assign) BOOL didShow;
@property (nonatomic, strong) NSTimer *autoHideTimer;
@property (nonatomic, assign) BOOL isScreenCaptured;
@end

@implementation SPFloatingBall

+ (instancetype)shared {
    static SPFloatingBall *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[SPFloatingBall alloc] initWithFrame:[UIScreen mainScreen].bounds];
    });
    return instance;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = YES;
        self.didShow = NO;
        self.isScreenCaptured = NO;

        // 小一半尺寸（原60，现30）
        CGFloat ballSize = 30;
        CGFloat startX = frame.size.width - ballSize - 8;
        CGFloat startY = 150;

        _ballView = [[UIView alloc] initWithFrame:CGRectMake(startX, startY, ballSize, ballSize)];
        // 透明化
        _ballView.backgroundColor = [UIColor colorWithRed:0.2 green:0.6 blue:1.0 alpha:0.5];
        _ballView.layer.cornerRadius = ballSize / 2;
        [self addSubview:_ballView];

        _speedLabel = [[UILabel alloc] initWithFrame:_ballView.bounds];
        _speedLabel.textAlignment = NSTextAlignmentCenter;
        _speedLabel.textColor = [UIColor whiteColor];
        _speedLabel.font = [UIFont boldSystemFontOfSize:10];
        _speedLabel.text = @"1x";
        [_ballView addSubview:_speedLabel];

        UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTap:)];
        [_ballView addGestureRecognizer:tap];

        UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
        [_ballView addGestureRecognizer:pan];

        // AUTO_HIDE_DISABLED: 录屏/截图自动隐藏已禁用
        // [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(screenshotTaken) name:UIApplicationUserDidTakeScreenshotNotification object:nil];
        // [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(screenCaptureChanged) name:UIScreenCapturedDidChangeNotification object:nil];
    }
    return self;
}

#pragma mark - 截图/录屏检测
- (void)screenshotTaken {
    // AUTO_HIDE_DISABLED: 截图时隐藏已禁用
}

- (void)screenCaptureChanged {
    self.isScreenCaptured = [UIScreen mainScreen].isCaptured;
    // AUTO_HIDE_DISABLED: 录屏时隐藏已禁用
}

#pragma mark - 找到游戏的主窗口
+ (UIWindow *)findKeyWindow {
    UIApplication *app = [UIApplication sharedApplication];
    if (!app) return nil;

    // 方式1：遍历 connectedScenes 找 keyWindow
    if (@available(iOS 13.0, *)) {
        for (UIScene *scene in app.connectedScenes) {
            if (![scene isKindOfClass:[UIWindowScene class]]) continue;
            UIWindowScene *windowScene = (UIWindowScene *)scene;
            for (UIWindow *window in windowScene.windows) {
                if (window.isKeyWindow && !window.hidden) {
                    return window;
                }
            }
            // fallback：取第一个可见窗口
            for (UIWindow *window in windowScene.windows) {
                if (!window.hidden && window.alpha > 0) {
                    return window;
                }
            }
            if (windowScene.windows.count > 0) {
                return windowScene.windows.firstObject;
            }
        }
    }

    // 方式2：传统方式
    for (UIWindow *window in app.windows) {
        if (window.isKeyWindow && !window.hidden) {
            return window;
        }
    }
    for (UIWindow *window in app.windows) {
        if (!window.hidden && window.alpha > 0) {
            return window;
        }
    }
    return app.windows.firstObject;
}

#pragma mark - 触摸穿透
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    if (self.controlPanel && !self.controlPanel.hidden) {
        CGPoint panelPoint = [self convertPoint:point toView:self.controlPanel];
        if ([self.controlPanel pointInside:panelPoint withEvent:event]) {
            return [super hitTest:point withEvent:event];
        }
    }
    CGPoint ballPoint = [self convertPoint:point toView:self.ballView];
    if ([self.ballView pointInside:ballPoint withEvent:event]) {
        return self.ballView;
    }
    return nil;
}

#pragma mark - 显示/隐藏
- (void)show {
    UIWindow *keyWindow = [SPFloatingBall findKeyWindow];
    if (!keyWindow) {
        // 窗口还没准备好，0.5秒后重试（不设didShow，允许无限重试）
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [self show];
        });
        return;
    }
    if (self.superview != keyWindow) {
        [self removeFromSuperview];
        [keyWindow addSubview:self];
    }
    // 确保在最上层
    [keyWindow bringSubviewToFront:self];
    self.didShow = YES;
    self.hidden = NO;
    [self updateSpeedDisplay];
}

- (void)hide {
    self.hidden = YES;
    [self.controlPanel hide];
    [self cancelAutoHideTimer];
}

- (void)updateSpeedDisplay {
    SPSpeedManager *mgr = [SPSpeedManager shared];
    self.speedLabel.text = [NSString stringWithFormat:@"%.0fx", mgr.speed];
    self.ballView.backgroundColor = mgr.enabled
        ? [UIColor colorWithRed:0.2 green:0.8 blue:0.4 alpha:0.5]
        : [UIColor colorWithRed:0.5 green:0.5 blue:0.5 alpha:0.5];
}

#pragma mark - 3秒不动自动收回
- (void)resetAutoHideTimer {
    [self cancelAutoHideTimer];
    self.autoHideTimer = [NSTimer scheduledTimerWithTimeInterval:3.0 target:self selector:@selector(autoHidePanel) userInfo:nil repeats:NO];
}

- (void)cancelAutoHideTimer {
    if (self.autoHideTimer) {
        [self.autoHideTimer invalidate];
        self.autoHideTimer = nil;
    }
}

- (void)autoHidePanel {
    if (self.controlPanel && !self.controlPanel.hidden) {
        [self.controlPanel hide];
    }
}

#pragma mark - 点击
- (void)handleTap:(UITapGestureRecognizer *)tap {
    if (self.isDragging) return;
    if (!self.controlPanel) {
        self.controlPanel = [[SPControlPanel alloc] init];
    }
    if (self.controlPanel.hidden) {
        if (self.controlPanel.superview != self) {
            [self.controlPanel removeFromSuperview];
            [self addSubview:self.controlPanel];
        }
        CGFloat ballCenterX = self.ballView.center.x;
        CGFloat ballBottom = CGRectGetMaxY(self.ballView.frame);
        CGFloat panelW = self.controlPanel.frame.size.width;
        CGFloat panelH = self.controlPanel.frame.size.height;
        CGFloat panelX = ballCenterX - panelW / 2;
        panelX = MAX(8, MIN(self.bounds.size.width - panelW - 8, panelX));
        CGFloat panelY = ballBottom + 6;
        if (panelY + panelH > self.bounds.size.height - 16) {
            panelY = self.ballView.frame.origin.y - panelH - 6;
        }
        self.controlPanel.frame = CGRectMake(panelX, panelY, panelW, panelH);
        [self.controlPanel showFromBall:self];
        [self resetAutoHideTimer];
    } else {
        [self.controlPanel hide];
        [self cancelAutoHideTimer];
    }
}

#pragma mark - 拖动
- (void)handlePan:(UIPanGestureRecognizer *)pan {
    [self resetAutoHideTimer];
    CGPoint translation = [pan translationInView:self];
    switch (pan.state) {
        case UIGestureRecognizerStateBegan:
            self.startPoint = self.ballView.center;
            self.isDragging = NO;
            break;
        case UIGestureRecognizerStateChanged: {
            CGFloat distance = sqrt(pow(translation.x, 2) + pow(translation.y, 2));
            if (distance > 5) self.isDragging = YES;
            CGPoint newCenter = CGPointMake(self.startPoint.x + translation.x, self.startPoint.y + translation.y);
            CGFloat margin = 20;
            newCenter.x = MAX(margin, MIN(self.bounds.size.width - margin, newCenter.x));
            newCenter.y = MAX(margin, MIN(self.bounds.size.height - margin, newCenter.y));
            self.ballView.center = newCenter;
            break;
        }
        case UIGestureRecognizerStateEnded: {
            CGFloat margin = 20;
            CGFloat targetX = (self.ballView.center.x < self.bounds.size.width / 2) ? margin : self.bounds.size.width - margin;
            [UIView animateWithDuration:0.25 animations:^{
                self.ballView.center = CGPointMake(targetX, self.ballView.center.y);
            }];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                self.isDragging = NO;
            });
            break;
        }
        default:
            break;
    }
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
