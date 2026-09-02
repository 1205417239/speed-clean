# GitHub Actions 自动编译指南

## 快速开始

### 1. 创建 GitHub 仓库

```bash
# 在 GitHub 上新建一个空仓库（不要勾选 README）
# 然后在本地工程目录执行：
cd SpeedTweak
git init
git add .
git commit -m "Initial commit"
git branch -M main
git remote add origin https://github.com/你的用户名/SpeedTweak.git
git push -u origin main
```

### 2. 自动编译

推送代码后，GitHub Actions 会自动触发编译（`.github/workflows/build.yml`）。

查看进度：
- 打开你的仓库页面
- 点击顶部 **Actions** 标签
- 找到最新的 "Build SpeedTweak" 工作流
- 等待运行完成（约 3-5 分钟）

### 3. 下载产物

编译成功后：
- 在 Actions 运行详情页底部，找到 **Artifacts** 区域
- 点击 `SpeedTweak-build` 下载 zip
- 解压后包含：
  - `com.yourname.speedtweak_1.0.0_iphoneos-arm.deb` — 安装包
  - `SpeedTweak.dylib` — 动态库

### 4. 发布版本（可选）

打 tag 推送会自动创建 GitHub Release：

```bash
git tag -a v1.0.0 -m "Release 1.0.0"
git push origin v1.0.0
```

Release 页面会自动附上 deb 和 dylib 文件。

## Workflow 说明

| 步骤 | 内容 |
|------|------|
| Runner | `macos-latest`（必须 macOS，Xcode 只能在 macOS 运行） |
| 依赖 | `ldid`（伪签名）、`xz`（解压 SDK）、`make` |
| Theos | 从官方仓库克隆到 `$HOME/theos` |
| SDK | 下载 iOS 16.5 SDK（兼容 iOS 14.0+ 部署目标） |
| 编译 | `make package FINALPACKAGE=1` |
| 产物 | 解包 deb 提取 dylib，一起上传 |

## 常见问题

**Q: Actions 报错 `SDK not found`**
A: 确认 `Makefile` 里的 SDK 版本（`16.5`）和 workflow 下载的版本一致。可在 `theos/sdks` 仓库找其他版本。

**Q: 编译失败 `substrate.h` not found**
A: Theos 自带 substrate 头文件，确认 `git clone --recursive` 拉取了子模块。

**Q: 想改最低系统版本**
A: 编辑 `Makefile`：`TARGET = iphone:clang:16.5:15.0`（最后一个数字是最低版本）。

**Q: 想编译 rootless 版本（无根越狱）**
A: 在 `Makefile` 加：`THEOS_PACKAGE_SCHEME = rootless`
