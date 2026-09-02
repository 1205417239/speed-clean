#import "SPSpeedManager.h"

// 最大时间增量限制，防止时间跳变导致游戏卡死
#define MAX_DELTA_NS 100000000LL      // 100ms (clock_gettime)
#define MAX_DELTA_US 100000LL          // 100ms (gettimeofday)
#define MAX_DELTA_MACH 1000000000ULL  // ~1秒的mach ticks (mach_absolute_time)

@interface SPSpeedManager ()
@property (nonatomic, assign) uint64_t lastSystemMach;
@property (nonatomic, assign) uint64_t lastScaledMach;
// clock_gettime 基准（按 clk_id 分别维护）
@property (nonatomic, strong) NSMutableDictionary<NSNumber *, NSValue *> *clockSystemBase;
@property (nonatomic, strong) NSMutableDictionary<NSNumber *, NSValue *> *clockScaledBase;
// gettimeofday 基准
@property (nonatomic, assign) struct timeval tvSystemBase;
@property (nonatomic, assign) struct timeval tvScaledBase;
@property (nonatomic, assign) BOOL tvBaseInitialized;
@end

@implementation SPSpeedManager

+ (instancetype)shared {
    static SPSpeedManager *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[SPSpeedManager alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _speed = 1.0;
        _pauseTimeoutEnabled = YES;  // 默认开启: 弹窗2秒后自动加速
        _enabled = NO;
        _lastSystemMach = 0;
        _lastScaledMach = 0;
        _clockSystemBase = [NSMutableDictionary dictionary];
        _clockScaledBase = [NSMutableDictionary dictionary];
        _tvBaseInitialized = NO;
    }
    return self;
}

// speed 变化时不重置基准，平滑过渡
- (void)setSpeed:(double)speed {
    if (speed <= 0) speed = 0.1;
    if (speed > 20) speed = 20;
    _speed = speed;
}

// 从关闭到开启时重置基准
- (void)setEnabled:(BOOL)enabled {
    if (enabled && !_enabled) {
        [self reset];
    }
    _enabled = enabled;
}

- (void)reset {
    self.lastSystemMach = 0;
    self.lastScaledMach = 0;
    [self.clockSystemBase removeAllObjects];
    [self.clockScaledBase removeAllObjects];
    self.tvBaseInitialized = NO;
}

#pragma mark - mach_absolute_time

- (uint64_t)adjustMachAbsolute:(uint64_t)systemValue {
    if (!self.enabled || self.speed <= 0) {
        return systemValue;
    }
    @synchronized (self) {
        if (self.lastSystemMach == 0) {
            self.lastSystemMach = systemValue;
            self.lastScaledMach = systemValue;
            return systemValue;
        }
        uint64_t delta = systemValue - self.lastSystemMach;
        // 最大增量限制
        if (delta > MAX_DELTA_MACH) delta = MAX_DELTA_MACH;
        uint64_t scaledDelta = (uint64_t)((double)delta * self.speed);
        self.lastScaledMach += scaledDelta;
        self.lastSystemMach = systemValue;
        return self.lastScaledMach;
    }
}

#pragma mark - clock_gettime

- (void)adjustTimespec:(struct timespec *)tp clockId:(clockid_t)clkId {
    if (!tp) return;
    if (!self.enabled || self.speed <= 0) return;

    @synchronized (self) {
        NSNumber *key = @(clkId);
        NSValue *sysBaseVal = self.clockSystemBase[key];
        NSValue *scaledBaseVal = self.clockScaledBase[key];

        int64_t systemNs = (int64_t)tp->tv_sec * 1000000000LL + tp->tv_nsec;

        if (!sysBaseVal || !scaledBaseVal) {
            struct timespec sysBase = *tp;
            struct timespec scaledBase = *tp;
            self.clockSystemBase[key] = [NSValue value:&sysBase withObjCType:@encode(struct timespec)];
            self.clockScaledBase[key] = [NSValue value:&scaledBase withObjCType:@encode(struct timespec)];
            return;
        }

        struct timespec sysBase;
        struct timespec scaledBase;
        [sysBaseVal getValue:&sysBase];
        [scaledBaseVal getValue:&scaledBase];

        int64_t sysBaseNs = (int64_t)sysBase.tv_sec * 1000000000LL + sysBase.tv_nsec;
        int64_t scaledBaseNs = (int64_t)scaledBase.tv_sec * 1000000000LL + scaledBase.tv_nsec;

        int64_t deltaNs = systemNs - sysBaseNs;
        if (deltaNs > MAX_DELTA_NS) deltaNs = MAX_DELTA_NS;
        if (deltaNs < 0) deltaNs = 0;

        int64_t scaledDeltaNs = (int64_t)((double)deltaNs * self.speed);
        int64_t resultNs = scaledBaseNs + scaledDeltaNs;

        sysBase = *tp;
        scaledBase.tv_sec = resultNs / 1000000000LL;
        scaledBase.tv_nsec = resultNs % 1000000000LL;
        self.clockSystemBase[key] = [NSValue value:&sysBase withObjCType:@encode(struct timespec)];
        self.clockScaledBase[key] = [NSValue value:&scaledBase withObjCType:@encode(struct timespec)];

        tp->tv_sec = scaledBase.tv_sec;
        tp->tv_nsec = scaledBase.tv_nsec;
    }
}

#pragma mark - gettimeofday

- (void)adjustTimeval:(struct timeval *)tv {
    if (!tv) return;
    if (!self.enabled || self.speed <= 0) return;

    @synchronized (self) {
        int64_t systemUs = (int64_t)tv->tv_sec * 1000000LL + tv->tv_usec;

        if (!self.tvBaseInitialized) {
            self.tvSystemBase = *tv;
            self.tvScaledBase = *tv;
            self.tvBaseInitialized = YES;
            return;
        }

        int64_t sysBaseUs = (int64_t)self.tvSystemBase.tv_sec * 1000000LL + self.tvSystemBase.tv_usec;
        int64_t scaledBaseUs = (int64_t)self.tvScaledBase.tv_sec * 1000000LL + self.tvScaledBase.tv_usec;

        int64_t deltaUs = systemUs - sysBaseUs;
        if (deltaUs > MAX_DELTA_US) deltaUs = MAX_DELTA_US;
        if (deltaUs < 0) deltaUs = 0;

        int64_t scaledDeltaUs = (int64_t)((double)deltaUs * self.speed);
        int64_t resultUs = scaledBaseUs + scaledDeltaUs;

        self.tvSystemBase = *tv;
        struct timeval scaled;
        scaled.tv_sec = resultUs / 1000000LL;
        scaled.tv_usec = resultUs % 1000000LL;
        self.tvScaledBase = scaled;

        tv->tv_sec = scaled.tv_sec;
        tv->tv_usec = scaled.tv_usec;
    }
}

@end
