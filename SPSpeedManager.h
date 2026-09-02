#import <Foundation/Foundation.h>
#import <time.h>
#import <sys/time.h>
#import <mach/mach_time.h>

NS_ASSUME_NONNULL_BEGIN

@interface SPSpeedManager : NSObject

@property (nonatomic, assign) double speed;          // 倍速，1.0=正常，2.0=两倍
@property (nonatomic, assign) BOOL enabled;           // 加速开关
@property (nonatomic, assign) BOOL pauseTimeoutEnabled; // 暂停超时加速
@property (nonatomic, assign) BOOL shakeDisabled;       // 禁止摇一摇
@property (nonatomic, assign) BOOL motionDisabled;      // 禁止陀螺仪/加速度计
@property (nonatomic, readonly) uint64_t lastSystemMach;
@property (nonatomic, readonly) uint64_t lastScaledMach;

+ (instancetype)shared;

/// 重置时间基准（切换倍速/开关时调用，避免时间跳变）
- (void)reset;

/// 调整 mach_absolute_time 返回值
- (uint64_t)adjustMachAbsolute:(uint64_t)systemValue;

/// 调整 clock_gettime 返回值
- (void)adjustTimespec:(struct timespec *)tp clockId:(clockid_t)clkId;

/// 调整 gettimeofday 返回值
- (void)adjustTimeval:(struct timeval *)tv;

@end

NS_ASSUME_NONNULL_END
