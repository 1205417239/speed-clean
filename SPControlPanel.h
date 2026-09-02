#import <UIKit/UIKit.h>

@class SPFloatingBall;

NS_ASSUME_NONNULL_BEGIN

@interface SPControlPanel : UIView

@property (nonatomic, readonly, getter=isHidden) BOOL hidden;

- (void)showFromBall:(SPFloatingBall *)ball;
- (void)hide;

@end

NS_ASSUME_NONNULL_END
