# SpeedTweak - iOS 游戏加速器 dylib

基于「冲鸭变速器」Android 版加速逻辑复刻的 iOS 越狱 Tweak，编译产物为 dylib。

## 加速原理

Hook 进程内三个时间函数，按设定倍速缩放返回值：

| 函数 | 用途 |
|------|------|
| `mach_absolute_time` | 高性能计时，游戏主循环/动画常用 |
| `clock_gettime` | POSIX 标准计时 |
| `gettimeofday` | 传统时间获取 |

核心算法：维护「系统真实时间」和「缩放时间」两套基准，每次调用时计算时间增量 × 倍速，累加到缩放时间上返回。切换倍速/开关时重置基准，避免时间跳变。

## 编译环境要求

- macOS（必须，Apple 工具链只能在 macOS 运行）
- Xcode + Command Line Tools
- Theos（越狱开发框架）
- iOS 14.0+ 越狱设备（用于安装测试）

## 编译步骤

### 1. 安装 Theos

```bash
# 安装依赖
brew install ldid xz

# 克隆 Theos
export THEOS=~/theos
git clone --recursive https://github.com/theos/theos.git $THEOS

# 下载 iOS SDK（放到 $THEOS/sdks/）
# 从 https://github.com/theos/sdks 下载对应版本
```

### 2. 配置工程

编辑 `control` 文件，把 `Maintainer` 和 `Author` 改成你的名字。

编辑 `Makefile`，确认 `TARGET = iphone:clang:16.5:14.0` 中的 SDK 版本和最低系统版本符合你的环境。

### 3. 编译

```bash
cd SpeedTweak
make clean
make package
```

编译成功后，产物在 `./packages/` 目录下：
- `com.yourname.speedtweak_1.0.0_iphoneos-arm.deb` — 安装包
- dylib 在 deb 包内：`/Library/MobileSubstrate/DynamicLibraries/SpeedTweak.dylib`

### 4. 安装到设备

```bash
# 方式一：通过 Theos 直接安装（需要设备 SSH）
export THEOS_DEVICE_IP=你的设备IP
make install

# 方式二：把 deb 传到设备，用 Filza/Zebra 安装
scp ./packages/*.deb root@设备IP:/tmp/
# 设备上执行
dpkg -i /tmp/com.yourname.speedtweak_*.deb
killall SpringBoard
```

## 使用说明

1. 安装后注销 SpringBoard，屏幕边缘出现蓝色悬浮球
2. 拖动悬浮球到任意位置（自动吸附左右边缘）
3. 点击悬浮球展开控制面板：
   - **开关**：启用/禁用加速
   - **滑块**：0.5x ~ 10x 无级调节
   - **+/- 按钮**：每次 ±0.5x
   - **快捷按钮**：1x / 2x / 3x / 5x 一键切换
4. 悬浮球颜色：灰色=未启用，绿色=已启用

## 注入范围配置

默认只注入 SpringBoard（`SpeedTweak.plist`）。如果要注入到具体游戏/App，修改 `SpeedTweak.plist`：

```xml
{
    Filter = {
        Bundles = (
            "com.company.game1",
            "com.company.game2"
        );
    };
}
```

如果要全局注入（所有进程），删除 Filter 字段或留空。

**注意：全局注入可能导致系统不稳定，建议只注入目标游戏。**

## 文件结构

```
SpeedTweak/
├── Makefile              # Theos 编译配置
├── control               # deb 包信息
├── SpeedTweak.plist      # 注入过滤规则
├── Tweak.xm              # 入口：MSHookFunction hook 时间函数
├── SPSpeedManager.h      # 速度管理器（核心算法）
├── SPSpeedManager.m
├── SPFloatingBall.h      # 悬浮球
├── SPFloatingBall.m
├── SPControlPanel.h      # 控制面板（滑块/开关/快捷按钮）
├── SPControlPanel.m
└── README.md
```

## 常见问题

**Q: 编译报错 `substrate.h` not found**
A: 确认 Theos 安装完整，`$THEOS` 环境变量已设置。

**Q: 安装后悬浮球不出现**
A: 检查设备是否越狱、Substitute 或 Cydia Substrate 是否安装。查看 `/Library/MobileSubstrate/DynamicLibraries/` 下是否有 dylib 和 plist。

**Q: 加速无效**
A: 部分游戏使用自己的服务器时间校验或反作弊，hook 本地时间可能无效。确认 plist 注入范围包含目标游戏。

**Q: 倍速太高游戏闪退**
A: 部分游戏对时间敏感，建议从 1.5x ~ 2x 开始逐步测试。

## 免责声明

本项目仅用于学习研究。使用本软件修改游戏可能违反游戏用户协议，请自行承担风险。
