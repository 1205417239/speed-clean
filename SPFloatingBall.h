#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface SPFloatingBall : UIView

+ (instancetype)shared;
- (void)show;
- (void)hide;
- (void)updateSpeedDisplay;

@end

NS_ASSUME_NONNULL_END
