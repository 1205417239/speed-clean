#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <dlfcn.h>
#import <objc/runtime.h>
#import <CoreMotion/CoreMotion.h>
#import "LuckySpeeder.h"
#import "SPSpeedManager.h"
#import "SPFloatingBall.h"

static BOOL hook_installed = NO;
static BOOL g_shakeDisabled = YES;   // 禁止摇一摇
static BOOL g_motionDisabled = YES;  // 禁止陀螺仪

static int install_hooks(void) {
    int ret = hook_timeScale();
    hook_time_realtimeSinceStartup();
    hook_time_unscaledTime();
    hook_time_timeSinceLevelLoad();
    hook_time_fixedUnscaledTime();
    hook_time_time();
    hook_time_deltaTime();
    hook_targetFrameRate();
    hook_vSyncCount();
    hook_maximumDeltaTime();
    hook_smoothDeltaTime();
    hook_time_unscaledDeltaTime();
    return ret;
}

static void apply_speed(float value) {
    set_timeScale(value);
    set_realtime(value);
    set_unscaledTime(value);
    set_timeSinceLevelLoad(value);
    set_fixedUnscaledTime(value);
    set_time_time(value);
    set_deltaTime(value);
    set_smoothDeltaTime(value);
    set_targetFrameRate(60);  // 稳定60帧
    set_vSyncCount(0);        // 关闭垂直同步
}

static void reset_speed(void) {
    reset_timeScale();
    reset_realtime();
    reset_unscaledTime();
    reset_timeSinceLevelLoad();
    reset_fixedUnscaledTime();
    reset_time_time();
    reset_deltaTime();
    reset_smoothDeltaTime();
    reset_targetFrameRate();
    reset_vSyncCount();
    reset_maximumDeltaTime();
}

#pragma mark - KVO 监听倍速变化
static void *kSpeedContext = &kSpeedContext;

@interface SpeedObserver : NSObject
@end
@implementation SpeedObserver
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    if (context == kSpeedContext) {
        SPSpeedManager *mgr = [SPSpeedManager shared];
        if (mgr.enabled && fabs(mgr.speed - 1.0) > 0.01) {
            apply_speed((float)mgr.speed);
        } else {
            reset_speed();
        }
    }
}
@end

static SpeedObserver *g_observer = nil;

static void __attribute__((constructor)) initialize(void) {
    @autoreleasepool {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            @try {
                int ret = install_hooks();
                if (ret == 0) {
                    hook_installed = YES;
                    NSLog(@"UnityAudioPlugin: speed hooks installed");
                }
            } @catch (NSException *e) {
                NSLog(@"UnityAudioPlugin: install exception: %@", e);
            }
        });

        g_observer = [[SpeedObserver alloc] init];
        [[SPSpeedManager shared] addObserver:g_observer forKeyPath:@"speed" options:NSKeyValueObservingOptionNew context:kSpeedContext];
        [[SPSpeedManager shared] addObserver:g_observer forKeyPath:@"enabled" options:NSKeyValueObservingOptionNew context:kSpeedContext];

        // 禁止摇一摇 + 禁止陀螺仪
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            @try {
                // 禁止摇一摇
                Method origSendEvent = class_getInstanceMethod([UIApplication class], @selector(sendEvent:));
                void (*origSendEventImp)(id, SEL, UIEvent*) = (void(*)(id, SEL, UIEvent*))method_getImplementation(origSendEvent);
                class_replaceMethod([UIApplication class], @selector(sendEvent:), imp_implementationWithBlock(^(id _self, UIEvent *event) {
                    if (g_shakeDisabled && event.type == UIEventTypeMotion && event.subtype == UIEventSubtypeMotionShake) {
                        return;
                    }
                    origSendEventImp(_self, @selector(sendEvent:), event);
                }), method_getTypeEncoding(origSendEvent));
                NSLog(@"UnityAudioPlugin: shake disabled");

                // 禁止陀螺仪
                Class cmMgr = NSClassFromString(@"CMMotionManager");
                if (cmMgr) {
                    NSArray *selectors = @[@"startAccelerometerUpdates", @"startGyroUpdates", @"startDeviceMotionUpdates", @"startMagnetometerUpdates"];
                    for (NSString *selName in selectors) {
                        SEL sel = NSSelectorFromString(selName);
                        Method orig = class_getInstanceMethod(cmMgr, sel);
                        if (orig) {
                            void (*origImp)(id, SEL) = (void(*)(id, SEL))method_getImplementation(orig);
                            class_replaceMethod(cmMgr, sel, imp_implementationWithBlock(^(id _self) {
                                if (g_motionDisabled) return;
                                origImp(_self, sel);
                            }), method_getTypeEncoding(orig));
                        }
                    }
                    NSArray *props = @[@"accelerometerData", @"gyroData", @"deviceMotion", @"magnetometerData"];
                    for (NSString *propName in props) {
                        SEL sel = NSSelectorFromString(propName);
                        Method orig = class_getInstanceMethod(cmMgr, sel);
                        if (orig) {
                            id (*origImp)(id, SEL) = (id(*)(id, SEL))method_getImplementation(orig);
                            class_replaceMethod(cmMgr, sel, imp_implementationWithBlock(^id(id _self) {
                                if (g_motionDisabled) return nil;
                                return origImp(_self, sel);
                            }), method_getTypeEncoding(orig));
                        }
                    }
                    NSLog(@"UnityAudioPlugin: motion disabled");
                }
            } @catch (NSException *e) {
                NSLog(@"UnityAudioPlugin swizzle failed: %@", e);
            }
        });

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            @try {
                [[SPFloatingBall shared] show];
            } @catch (NSException *e) {
                NSLog(@"UnityAudioPlugin UI init failed: %@", e);
            }
        });
    }
}
