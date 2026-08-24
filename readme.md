# FCEUX 2.6.6 Yhc Version

> An unofficial enhanced fork based on **FCEUX 2.6.6**, focused on emulator compatibility, NSF playback, PPU/video accuracy, input improvements, mapper support, and practical debugging/recording features.

> 基于 **FCEUX 2.6.6** 的非官方增强版本，主要面向模拟器兼容性、NSF 播放、PPU/视频精度、输入系统、Mapper 支持以及调试与录制功能的改进。

---

## 中文说明

### 项目简介

FCEUX 2.6.6 Yhc 是基于 FCEUX 2.6.6 源代码维护的自定义版本。

本项目并不是对 FCEUX 进行大规模重写，而是在尽量保持原有使用习惯和兼容性的基础上，对实际使用中遇到的问题进行修正和增强。当前版本的主要修改集中在以下几个方向：

- NSF / NSFe 播放器增强
- PPU 与 Color 512 显示修正
- 手柄快捷键支持
- 鼠标 / 窗口焦点误触修复
- 视频与调色板改进
- AVI 录制相关增强
- Mapper 与扩展音源兼容性改进
- 一些针对特殊 ROM / Hack / 多合一游戏的兼容性修正

### 主要功能

#### 1. NSF / NSFe 播放器增强

当前版本支持：

- NSF 与 NSFe 播放
- 曲名、作者、版权信息显示
- NSFe Track Label
- 曲目时间与 Fade 信息
- 播放 / 暂停 / 停止控制
- 波形显示
- 虚拟键盘显示
- Peak / RMS 电平显示
- 扩展音源状态显示
- NSF 播放界面的手柄控制
- 多扩展音源混合播放

已针对以下扩展音源进行适配或兼容性修正：

- VRC6
- VRC7
- FDS
- MMC5
- Namco 106 / N163
- Sunsoft FME-7 / 5B

其中也修复了部分组合扩展音源 NSF 中，某个音源被后初始化的音源覆盖、可视键盘与真实音频状态不一致等问题。

#### 2. NSF 自定义播放速率

NSF Header 中的：

- `NTSCspeed`
- `PALspeed`

现在会作为实际的 PLAY 调用间隔使用。

此前采用“按视频帧累计误差”的方式时，当 NSF 速率与视频刷新率存在轻微差异，会出现：

- 某一帧连续执行两次 PLAY，听感像突然前进一帧
- 某一帧不执行 PLAY，听感像某个声音多停留一帧

典型情况包括：

- `40FF`（16639 µs）
- `4165`（16741 µs）

当前版本已经改为更细粒度的 PLAY 调度，使 PLAY 调用不再被强制量化到整帧边界，从而避免长时间播放后突然跳过或额外停留一帧的问题。

#### 3. PPU / Color 512 修正

NEWPPU 的 Color 512 路径进行了多项修正，包括：

- `$2001` 状态按 dot 处理
- Grayscale 按 dot 生效
- Color Emphasis 按 dot 生效
- Emphasis 状态可以正确恢复到 0
- 强制空白期间的调色板读取状态修正
- 避免后续 `$2007` 写入影响已经记录的像素
- 修正部分渲染标志原本按 8 像素块处理的问题
- 修正普通整数 2x / 3x 放大路径未正确读取 `XDBuf` 的问题
- 调整 Color Emphasis 的亮度表现，避免强调色过暗

#### 4. 手柄快捷键支持

当前版本增加了 DirectInput 手柄快捷键支持，可绑定：

- 普通按钮
- X / Y / Z 轴
- Rx / Ry / Rz 轴
- POV / Hat

例如可以将以下功能绑定到手柄：

- Pause
- Reset
- Save State
- Load State
- Save Slot
- Screenshot
- Frame Advance
- Turbo

对于一次性快捷键与持续型快捷键，也分别处理：

- Pause / Reset / Screenshot / Save / Load 等：按下一次只触发一次
- Frame Advance / Turbo 等：保留按住行为

手柄绑定同时会保存设备 GUID，以提高重新启动模拟器后的设备匹配稳定性。

> 注意：部分带有 M1 / M2 / M3 / M4 背键的手柄，如果驱动没有把这些按键作为独立 DirectInput Button 暴露给 Windows，则 FCEUX 无法直接识别这些按键。

#### 5. 鼠标与窗口焦点误触修复

修复了手柄 Hotkeys 加入后出现的一些 DirectInput 边界问题，包括：

- 鼠标点击模拟器内部时，错误识别为 `JS 0 axis x+ / x-`
- DirectInput `GetDeviceState()` 失败后，全零状态被误认为有效轴输入
- 点击桌面、任务栏或其他程序时，错误触发 Save Slot 等快捷键
- 从快捷键设置窗口返回后，刚刚用于绑定的按键立即触发对应功能

当前版本在设备失去焦点、重新 Acquire 或读取失败时，会重新建立输入基准，避免生成虚假的轴方向或上升沿。

#### 6. 视频与调色板

视频相关修改包括：

- NTSC 2x 显示支持改进
- Digital Prime 调色板相关调整
- Color 512 输出路径修正
- 2x / 3x 整数缩放路径修正
- 部分强调色与灰度显示问题修复

#### 7. AVI 录制

针对录制流程进行了一些增强与兼容性调整，包括：

- NSF 播放界面录制
- NTSC 2x / PAL 3x 等输出场景
- 自定义输出尺寸相关支持
- 录制时的画面 / 声音同步修正

具体可用选项取决于当前 Win32 构建版本。

#### 8. Mapper 与兼容性更新

本版本包含多项 Mapper 兼容性修正与新增支持。

已经处理过的 Mapper 包括但不限于：

`242`, `343`, `351`, `376`, `412`, `438`, `445`, `447`, `449`, `511`, `556`, `598`, `599`

此外还进行了 MMC5、N163、VRC 系列及其他特殊 Mapper / 多合一 ROM 的兼容性调整。

部分修改主要用于：

- PRG Bank 切换
- CHR Bank 切换
- Mirroring
- IRQ
- 扩展音频
- Split-ROM / 多合一 ROM
- 特殊寄存器响应

### 编译

本项目基于 FCEUX 2.6.6 的源代码结构。

Windows 版本建议使用与 FCEUX 2.6.6 源工程兼容的 Visual Studio 环境进行编译。

基本流程：

1. 获取完整源代码。
2. 打开 Win32 / Visual Studio 工程。
3. 确认 DirectX / DirectInput 等依赖可用。
4. 选择 Release 或 Debug 配置。
5. 编译生成 FCEUX 可执行文件。

如果使用的是较新的 Visual Studio，可能需要针对旧工程格式、Windows SDK 或旧版依赖进行少量兼容性调整。

### 使用说明

本版本总体保持 FCEUX 原有操作习惯。

建议在升级到新版本后重新检查：

- Input
- Hotkeys
- Sound
- Video
- NSF Playback
- Background Input

等选项。

如果旧配置文件中保存了已经不存在的手柄设备，建议重新进入 Hotkeys 或 Input 设置进行绑定。

### 已知限制

- M1 / M2 / M3 / M4 等厂商自定义背键是否可用，取决于驱动是否将其作为独立 DirectInput Button 暴露。
- 部分极少见 Mapper / 多合一 ROM 仍可能存在兼容性问题。
- NSF 扩展音源组合较多，不同文件可能使用非常规寄存器写法，仍可能需要单独适配。
- 本项目属于非官方 FCEUX 分支，不代表 FCEUX 官方行为。

### 问题反馈

提交问题时建议附带：

- ROM / NSF 的准确名称
- Mapper 编号
- 使用的 FCEUX 构建版本
- 是否启用 New PPU
- 重现步骤
- Debugger / Trace Logger 信息
- 必要时附带存档、FM2、NSF 或截图

### 致谢

感谢 FCEUX 原项目及其历代开发者。

本项目中的大量功能仍建立在 FCEUX 原有 CPU / PPU 模拟、APU、Debugger、Trace Logger、TAS、Movie、NSF 与 Mapper 基础设施之上。

---

## English

### About

FCEUX 2.6.6 Yhc is an unofficial custom build based on the FCEUX 2.6.6 source tree.

The goal of this fork is not to rewrite FCEUX from scratch. Instead, it keeps the original workflow and compatibility as much as possible while fixing practical issues found during real-world use.

The current version mainly focuses on:

- NSF / NSFe player improvements
- PPU and Color 512 accuracy fixes
- Gamepad hotkey support
- Mouse / focus false-input fixes
- Video and palette improvements
- AVI recording enhancements
- Mapper and expansion-audio compatibility
- Compatibility fixes for special ROM hacks and multicarts

### Main Features

#### 1. Enhanced NSF / NSFe Player

Current features include:

- NSF and NSFe playback
- Song / artist / copyright information
- NSFe track labels
- Track time and fade information
- Play / Pause / Stop controls
- Waveform display
- Virtual keyboard display
- Peak / RMS meters
- Expansion-audio status display
- Gamepad control in the NSF player
- Multiple expansion-audio chips in one NSF

Expansion audio compatibility work includes:

- VRC6
- VRC7
- FDS
- MMC5
- Namco 106 / N163
- Sunsoft FME-7 / 5B

Several mixed-expansion NSF issues were also fixed, including cases where one sound core overwrote another during initialization or where the visual keyboard did not match the actual audio state.

#### 2. NSF Custom Playback Rates

The NSF header fields `NTSCspeed` and `PALspeed` are now used as actual PLAY-call intervals.

The previous frame-based scheduler could accumulate timing error when the NSF playback rate differed slightly from the emulator video refresh rate. This could eventually cause:

- two PLAY calls in one video frame, sounding like a one-frame skip
- no PLAY call in one video frame, making a sound last one extra frame

Typical examples:

- `40FF` (16639 µs)
- `4165` (16741 µs)

The current version uses a finer-grained PLAY scheduler so NSF PLAY calls are no longer forced onto whole video-frame boundaries.

#### 3. PPU / Color 512 Fixes

Several fixes were made to the NEWPPU Color 512 path:

- per-dot `$2001` state handling
- per-dot grayscale
- per-dot color emphasis
- correct emphasis return to zero
- forced-blank palette handling fixes
- protection against later `$2007` writes affecting already-recorded pixels
- rendering flags no longer being limited to 8-pixel blocks
- fixed normal integer 2x / 3x scaling paths that previously ignored `XDBuf`
- adjusted color-emphasis brightness to avoid overly dark output

#### 4. Gamepad Hotkeys

This fork adds DirectInput gamepad hotkey support for:

- normal buttons
- X / Y / Z axes
- Rx / Ry / Rz axes
- POV / Hat input

Commands such as the following can be bound to a controller:

- Pause
- Reset
- Save State
- Load State
- Save Slot
- Screenshot
- Frame Advance
- Turbo

One-shot and held commands are handled separately:

- Pause / Reset / Screenshot / Save / Load: fire once per press
- Frame Advance / Turbo: keep held behavior

Controller bindings also retain the device GUID to improve device matching after restarting the emulator.

> Note: M1 / M2 / M3 / M4 rear buttons can only be detected if the controller driver exposes them as independent DirectInput buttons.

#### 5. Mouse and Focus False-Input Fixes

Several DirectInput edge cases introduced by gamepad hotkeys were fixed:

- clicking inside the emulator being detected as `JS 0 axis x+ / x-`
- failed `GetDeviceState()` calls producing a false zero-axis state
- clicking the desktop, taskbar, or another application triggering Save Slot or other hotkeys
- the button used in the hotkey configuration dialog immediately firing after leaving the dialog

When the device loses focus, is reacquired, or fails to return a valid state, the input baseline is rebuilt so no fake axis direction or rising edge is generated.

#### 6. Video and Palette

Video-related work includes:

- improved NTSC 2x output
- Digital Prime palette adjustments
- Color 512 output fixes
- integer 2x / 3x scaling fixes
- grayscale and emphasis corrections

#### 7. AVI Recording

The recording path also includes several improvements and compatibility adjustments, including:

- recording the NSF player interface
- NTSC 2x / PAL 3x output cases
- custom output-size support
- video / audio synchronization fixes during recording

Available options may vary depending on the current Win32 build.

#### 8. Mapper and Compatibility Updates

This fork contains a number of mapper additions and compatibility fixes.

Mappers that have received work include, but are not limited to:

`242`, `343`, `351`, `376`, `412`, `438`, `445`, `447`, `449`, `511`, `556`, `598`, `599`

Additional compatibility work has also been done for MMC5, N163, VRC-series hardware, special multicarts, and unusual ROM layouts.

Areas covered include:

- PRG banking
- CHR banking
- mirroring
- IRQ behavior
- expansion audio
- split-ROM / multicart layouts
- special register behavior

### Building

This project follows the FCEUX 2.6.6 source layout.

For Windows builds, use a Visual Studio environment compatible with the original FCEUX 2.6.6 Win32 project.

Basic steps:

1. Obtain the full source tree.
2. Open the Win32 / Visual Studio project.
3. Make sure DirectX / DirectInput dependencies are available.
4. Select a Release or Debug configuration.
5. Build the FCEUX executable.

Newer Visual Studio versions may require small compatibility adjustments for older project formats, Windows SDK versions, or legacy dependencies.

### Usage Notes

This fork keeps the general FCEUX workflow unchanged.

After upgrading, it is recommended to review:

- Input
- Hotkeys
- Sound
- Video
- NSF Playback
- Background Input

If an old configuration contains controller mappings for devices that are no longer present, rebind them in the Input or Hotkeys dialog.

### Known Limitations

- Vendor-specific M1 / M2 / M3 / M4 rear buttons are only available when exposed as independent DirectInput buttons.
- Some uncommon mappers and multicart layouts may still require additional compatibility work.
- Mixed-expansion NSF files may use unusual register behavior and can still require file-specific fixes.
- This is an unofficial FCEUX fork and does not represent official FCEUX behavior.

### Bug Reports

When reporting an issue, please include:

- exact ROM / NSF name
- mapper number
- FCEUX build/version
- whether New PPU is enabled
- reproduction steps
- Debugger / Trace Logger information
- savestate, FM2, NSF, or screenshots when necessary

### Credits

Thanks to the original FCEUX project and all of its contributors.

This fork continues to rely heavily on FCEUX's existing CPU / PPU emulation, APU, Debugger, Trace Logger, TAS tools, Movie system, NSF player, and Mapper infrastructure.

---

## License

This project is derived from FCEUX and follows the licensing terms of the original FCEUX source code and the individual source files included in the repository.

Please refer to the source headers and the upstream FCEUX project for full license details.
