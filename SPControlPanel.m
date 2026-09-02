#import "SPControlPanel.h"
#import "LuckySpeeder.h"
#import "SPSpeedManager.h"
#import "SPFloatingBall.h"

@interface SPControlPanel ()
@property (nonatomic, strong) UIView *contentView;
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) UISwitch *enableSwitch;
@property (nonatomic, strong) UILabel *speedValueLabel;
@property (nonatomic, strong) UISlider *speedSlider;
@property (nonatomic, strong) UIButton *minusButton;
@property (nonatomic, strong) UIButton *plusButton;
@property (nonatomic, strong) UIButton *closeButton;
@property (nonatomic, weak) SPFloatingBall *ball;
@property (nonatomic, assign) BOOL panelHidden;
@end

@implementation SPControlPanel

- (instancetype)init {
    CGFloat width = 240;
    CGFloat height = 200;
    CGRect frame = CGRectMake(0, 0, width, height);
    self = [super initWithFrame:frame];
    if (self) {
        _panelHidden = YES;
        self.backgroundColor = [UIColor clearColor];
        self.alpha = 0;
        self.hidden = YES;

        _contentView = [[UIView alloc] initWithFrame:self.bounds];
        _contentView.backgroundColor = [UIColor colorWithWhite:0.1 alpha:0.85];
        _contentView.layer.cornerRadius = 14;
        [self addSubview:_contentView];

        // 标题
        _titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(14, 10, 100, 20)];
        _titleLabel.text = @"游戏加速";
        _titleLabel.textColor = [UIColor whiteColor];
        _titleLabel.font = [UIFont boldSystemFontOfSize:14];
        [_contentView addSubview:_titleLabel];

        // 开关
        _enableSwitch = [[UISwitch alloc] initWithFrame:CGRectMake(width - 58, 10, 44, 24)];
        _enableSwitch.transform = CGAffineTransformMakeScale(0.8, 0.8);
        _enableSwitch.onTintColor = [UIColor colorWithRed:0.2 green:0.8 blue:0.4 alpha:1.0];
        [_enableSwitch addTarget:self action:@selector(switchChanged:) forControlEvents:UIControlEventValueChanged];
        [_contentView addSubview:_enableSwitch];

        // 倍速值
        _speedValueLabel = [[UILabel alloc] initWithFrame:CGRectMake(width/2 - 40, 38, 80, 26)];
        _speedValueLabel.textAlignment = NSTextAlignmentCenter;
        _speedValueLabel.textColor = [UIColor whiteColor];
        _speedValueLabel.font = [UIFont boldSystemFontOfSize:22];
        _speedValueLabel.text = @"1.0x";
        [_contentView addSubview:_speedValueLabel];

        // 减号
        _minusButton = [UIButton buttonWithType:UIButtonTypeSystem];
        _minusButton.frame = CGRectMake(14, 72, 30, 30);
        _minusButton.titleLabel.font = [UIFont boldSystemFontOfSize:18];
        [_minusButton setTitle:@"-" forState:UIControlStateNormal];
        [_minusButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        _minusButton.backgroundColor = [UIColor colorWithWhite:0.3 alpha:0.8];
        _minusButton.layer.cornerRadius = 15;
        [_minusButton addTarget:self action:@selector(minusTapped) forControlEvents:UIControlEventTouchUpInside];
        [_contentView addSubview:_minusButton];

        // 滑块
        _speedSlider = [[UISlider alloc] initWithFrame:CGRectMake(50, 76, width - 100, 22)];
        _speedSlider.minimumValue = 0.1;
        _speedSlider.maximumValue = 20.0;
        _speedSlider.value = 1.0;
        _speedSlider.minimumTrackTintColor = [UIColor colorWithRed:0.2 green:0.6 blue:1.0 alpha:1.0];
        [_speedSlider addTarget:self action:@selector(sliderChanged:) forControlEvents:UIControlEventValueChanged];
        [_contentView addSubview:_speedSlider];

        // 加号
        _plusButton = [UIButton buttonWithType:UIButtonTypeSystem];
        _plusButton.frame = CGRectMake(width - 44, 72, 30, 30);
        _plusButton.titleLabel.font = [UIFont boldSystemFontOfSize:18];
        [_plusButton setTitle:@"+" forState:UIControlStateNormal];
        [_plusButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        _plusButton.backgroundColor = [UIColor colorWithWhite:0.3 alpha:0.8];
        _plusButton.layer.cornerRadius = 15;
        [_plusButton addTarget:self action:@selector(plusTapped) forControlEvents:UIControlEventTouchUpInside];
        [_contentView addSubview:_plusButton];

        // 预设倍速（7档，分两行）
        NSArray *speeds = @[@0.1, @1.0, @3.0, @5.0, @10.0, @15.0, @20.0];
        CGFloat btnW = (width - 28 - 3 * 6) / 4;
        for (NSInteger i = 0; i < speeds.count; i++) {
            UIButton *btn = [UIButton buttonWithType:UIButtonTypeSystem];
            NSInteger row = i / 4;
            NSInteger col = i % 4;
            CGFloat btnY = 110 + row * 32;
            btn.frame = CGRectMake(14 + col * (btnW + 6), btnY, btnW, 26);
            btn.titleLabel.font = [UIFont boldSystemFontOfSize:12];
            [btn setTitle:[NSString stringWithFormat:@"%@x", speeds[i]] forState:UIControlStateNormal];
            [btn setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
            btn.backgroundColor = [UIColor colorWithWhite:0.3 alpha:0.8];
            btn.layer.cornerRadius = 6;
            btn.tag = (NSInteger)([speeds[i] doubleValue] * 10);
            [btn addTarget:self action:@selector(presetTapped:) forControlEvents:UIControlEventTouchUpInside];
            [_contentView addSubview:btn];
        }

        // 关闭按钮
        _closeButton = [UIButton buttonWithType:UIButtonTypeSystem];
        _closeButton.frame = CGRectMake(width - 28, -4, 20, 20);
        _closeButton.titleLabel.font = [UIFont boldSystemFontOfSize:14];
        [_closeButton setTitle:@"×" forState:UIControlStateNormal];
        [_closeButton setTitleColor:[UIColor lightGrayColor] forState:UIControlStateNormal];
        [_closeButton addTarget:self action:@selector(closeTapped) forControlEvents:UIControlEventTouchUpInside];
        [_contentView addSubview:_closeButton];
    }
    return self;
}

- (void)showFromBall:(SPFloatingBall *)ball {
    self.ball = ball;
    [self refreshUI];
    self.hidden = NO;
    _panelHidden = NO;
    self.alpha = 0;
    self.transform = CGAffineTransformMakeScale(0.8, 0.8);
    [UIView animateWithDuration:0.2 animations:^{
        self.alpha = 1;
        self.transform = CGAffineTransformIdentity;
    }];
}

- (void)hide {
    _panelHidden = YES;
    [UIView animateWithDuration:0.15 animations:^{
        self.alpha = 0;
        self.transform = CGAffineTransformMakeScale(0.8, 0.8);
    } completion:^(BOOL finished) {
        self.hidden = YES;
    }];
}

- (BOOL)isHidden {
    return _panelHidden;
}

- (void)refreshUI {
    SPSpeedManager *mgr = [SPSpeedManager shared];
    self.enableSwitch.on = mgr.enabled;
    self.speedSlider.value = mgr.speed;
    self.speedValueLabel.text = [NSString stringWithFormat:@"%.1fx", mgr.speed];
}

- (void)switchChanged:(UISwitch *)sw {
    SPSpeedManager *mgr = [SPSpeedManager shared];
    mgr.enabled = sw.on;
    [self.ball updateSpeedDisplay];
}

- (void)sliderChanged:(UISlider *)slider {
    double value = round(slider.value * 10) / 10.0;
    SPSpeedManager *mgr = [SPSpeedManager shared];
    mgr.speed = value;
    self.speedValueLabel.text = [NSString stringWithFormat:@"%.1fx", value];
    [self.ball updateSpeedDisplay];
}

- (void)minusTapped {
    NSArray *presets = @[@0.1, @1.0, @3.0, @5.0, @10.0, @15.0, @20.0];
    SPSpeedManager *mgr = [SPSpeedManager shared];
    double current = mgr.speed;
    double newSpeed = [presets[0] doubleValue];
    for (NSInteger i = presets.count - 1; i >= 0; i--) {
        if ([presets[i] doubleValue] < current - 0.01) {
            newSpeed = [presets[i] doubleValue];
            break;
        }
    }
    mgr.speed = newSpeed;
    [self refreshUI];
    [self.ball updateSpeedDisplay];
}

- (void)plusTapped {
    NSArray *presets = @[@0.1, @1.0, @3.0, @5.0, @10.0, @15.0, @20.0];
    SPSpeedManager *mgr = [SPSpeedManager shared];
    double current = mgr.speed;
    double newSpeed = [presets.lastObject doubleValue];
    for (NSInteger i = 0; i < presets.count; i++) {
        if ([presets[i] doubleValue] > current + 0.01) {
            newSpeed = [presets[i] doubleValue];
            break;
        }
    }
    mgr.speed = newSpeed;
    [self refreshUI];
    [self.ball updateSpeedDisplay];
}

- (void)presetTapped:(UIButton *)btn {
    double speed = btn.tag / 10.0;
    SPSpeedManager *mgr = [SPSpeedManager shared];
    mgr.speed = speed;
    [self refreshUI];
    [self.ball updateSpeedDisplay];
}

- (void)closeTapped {
    [self hide];
}

@end
