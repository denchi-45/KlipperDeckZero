# KlipperDeckZero

[English](README.md) | [日本語](README.ja.md) | 简体中文

在 M5Stack **CardputerZero** 上运行 Klipper / Moonraker,通过设备自带的屏幕和键盘操作 3D 打印机的 LVGL 应用。

> 🚧 开发中。已实现第 2 阶段(温度设置、挤出/回抽、文件选择、打印控制、轴移动的操作界面)。

## 目标功能

- 打印机状态显示(状态、进度、已用/剩余时间、喷嘴和热床温度、正在打印的文件缩略图等)
- 选择 G-code 文件并开始打印,暂停/继续/取消
- 设置热端和热床的加热目标
- 耗材挤出(Extrude)与回抽(Retract)
- 轴移动(点动)、归零、关闭电机

## 架构

- 基础: [CardputerZero/Template](https://github.com/CardputerZero/Template)(C++17 / LVGL 9.5 / CMake)
- 打印机控制: [Moonraker](https://moonraker.readthedocs.io/) 的 JSON-RPC API
  - 实机: Unix 套接字(`~/printer_data/comms/moonraker.sock`)
  - 桌面模拟器: WebSocket(`ws://<printer-ip>:7125/websocket`)

## 界面与操作

### 界面布局

<img src="docs/images/screen-status-printing.png" width="640" alt="打印中的状态界面。从上到下依次为标题栏、PRINTING 徽标与文件名和进度条、时间/层数/温度、最下一行的 Speed/Flow/Fan,以及 5 个槽位的导航栏和右端的层显示 1/2">

所有界面使用相同的框架。

- **标题栏**: 左侧显示界面名称,右侧显示网络连接(Wi-Fi 信号强度 / 有线)、电池电量和时钟。
- **状态徽标**: 用不同颜色显示打印机状态(`OFFLINE` / `STARTING` / `READY` / `PRINTING` / `PAUSED` / `COMPLETE` / `CANCELLED` / `ERROR` / `SHUTDOWN`)。在状态界面和打印控制界面中,旁边会并排显示文件名和进度条。
- **内容区域**: 各界面的显示内容和操作项。当前选中的项以绿色标示。
- **最下一行**: 当前选中项的操作提示,或错误等消息。状态界面在此显示 Speed / Flow / Fan。
- **导航栏**: 在屏幕底部排列 5 个槽位。每个槽位正好位于键盘 `4`~`8` 键的上方,与所按按键的位置对齐。槽位分为若干层(目前为 2 层),用 `Sym`+`H` / `Sym`+`J` 切换到上一层 / 下一层。右端显示当前层(如 `1/2`)。当前界面的图标显示为绿色(若该界面不在所显示的层中,则没有绿色图标),空槽位为空白且无法选择。
- **Toast 提示**: 在屏幕底部临时显示操作结果(成功 / Klipper 错误)或“Printer is not ready”等信息。
- **主题**: 按 `D` 切换浅色 / 深色。

> 本 README 中的截图是将桌面模拟器连接到模拟 Moonraker(fake_moonraker)后拍摄的,包含设备外框和虚拟键盘(键盘上的符号为模拟器中按键分配的参考)。重新拍摄的方法请参阅“[README 截图](#readme-截图)”。

### 界面一览

| 层 | 按键 | 界面 | 用途 |
|---|---|---|---|
| 1 | `4` | 状态显示(主页) | 状态、进度、已用 / 剩余时间、层数、喷嘴和热床温度、速度、流量、风扇 |
| 1 | `5` | 温度设置 | 喷嘴和热床的目标温度、预设 |
| 1 | `6` | 挤出 / 回抽 | 耗材挤出(Extrude)与回抽(Retract) |
| 1 | `7` | 文件选择与开始打印 | G-code 文件列表和详情、开始打印 |
| 1 | `8` | 打印控制 | 暂停 / 继续、取消、紧急停止 |
| 2 | `4` | 轴移动 | 点动、归零、关闭电机 |
| 2 | `5` | 连接设置(也可按 `S` 打开) | 连接目标(本设备上的 Klipper / 网络上的打印机) |
| 2 | `6`~`8` | (空) | 预留给今后的界面 |

层与界面相互独立。切换层只会改变导航栏的显示(哪个键打开哪个界面),当前界面保持不变。只有按下 `4`~`8`、`S`、`ESC` 时界面才会改变,此时层也不会切换。

| 显示第 1 层 | 用 `Sym`+`J` 显示第 2 层 |
|---|---|
| <img src="docs/images/screen-status-ready.png" width="640" alt="在状态界面显示第 1 层。导航栏为状态、温度、挤出、文件、打印控制,状态图标为绿色,右端为 1/2"> | <img src="docs/images/screen-status-layer2.png" width="640" alt="保持同一状态界面并显示第 2 层。导航栏变为移动、连接设置和 3 个空槽位,没有绿色图标,右端为 2/2"> |

在右图的状态下,按 `4` 打开轴移动,按 `5` 打开连接设置。

### 通用按键操作

| 按键 | 功能 |
|---|---|
| `4`~`8` | 切换到当前显示层中对应槽位的界面(见上方界面一览) |
| `Sym`+`H` / `Sym`+`J` | 切换到上一层 / 下一层(模拟器中为 `<` / `>`) |
| `S` | 连接设置(当前显示的层不变) |
| `ESC` | 返回主页(状态显示)(当前显示的层不变) |
| 长按 `4` / `ESC` | 退出(仅在主页按下时有效;`4` 仅在显示第 1 层时有效) |
| `D` | 切换浅色 / 深色 |
| `R` / `F` | 重启 Klipper(`RESTART`) / 重启 MCU(`FIRMWARE_RESTART`)。在状态界面 3 秒内按两次即执行。Klipper 处于 ready 且正在打印或已暂停时不执行 |
| `Fn`+`H` | 帮助 |
| `Fn`+`J` | 截图(以 PNG 保存到 `~/Pictures/Screenshots`;模拟器中为 `PrintScreen` 键) |

在各界面中,用 `↑`/`↓` 选择项目,用 `←`/`→` 切换数值或选项,按 `Enter` 执行。输入数字期间,所有按键都由当前界面接收(输入 `6` 也不会切换页面)。

#### 状态显示(第 1 层的 `4`)

| 打印中(浅色) | 打印中(深色) |
|---|---|
| <img src="docs/images/screen-status-printing.png" width="640" alt="打印中的状态界面(浅色主题)。PRINTING 徽标、文件名、进度 3%、已用 02:35 / 剩余 1:20:25、层 7/240、喷嘴 215.3/215、热床 58.4/60"> | <img src="docs/images/screen-status-printing-dark.png" width="640" alt="打印中的状态界面(深色主题)"> |

<img src="docs/images/screen-status-ready.png" width="640" alt="待机中的状态界面。READY 徽标和 No active print,喷嘴和热床的目标温度为 off">

显示状态徽标、文件名和进度(%),已用 / 剩余时间和层数,喷嘴和热床的当前温度、目标温度及加热器功率,并在最下一行显示 Speed / Flow / Fan。未在打印时显示“No active print”。打印中或暂停中时,会在上方 3 行的左侧显示文件缩略图(在桌面模拟器中不显示,因为模拟 Moonraker 在文件元数据中返回空的 `thumbnails`)。

在此界面中 3 秒内按两次 `R` / `F`,即可重启 Klipper / MCU。Klipper 处于 ready 且正在打印或已暂停时不执行。在 Klipper 启动中、断开连接期间,或处于错误状态(`error`、`shutdown`)时,即使打印状态信息已过时也可以重启,因此可用于 Klipper 停止后的恢复。

#### 温度设置(第 1 层的 `5`)

<img src="docs/images/screen-temperature.png" width="640" alt="温度设置界面。Nozzle 和 Bed 的当前温度与 Target,Preset 的 PLA 200/60、PETG 235/80、Off,最下一行为 mcu 传感器温度">

用 `↑`/`↓` 选择喷嘴、热床或预设(没有热床的配置不显示热床行)。

- 喷嘴 / 热床: 用 `←`/`→` 以 5 为步长升降目标温度,按 `Enter` 可直接输入数字。再按一次 `Enter` 发送,按 `ESC` 取消。上限为喷嘴 350、热床 150(实际上限由 Klipper 的 `max_temp` 判定)。
- 预设(PLA 200/60、PETG 235/80、Off): 用 `←`/`→` 选择,按 `Enter` 同时设置两个加热器。

目标温度通过 `SET_HEATER_TEMPERATURE` 发送,结果以 Toast 提示显示。最下一行显示喷嘴和热床以外的温度传感器(如 `temperature_sensor`)以及本设备的 CPU 温度。

#### 挤出 / 回抽(第 1 层的 `6`)

<img src="docs/images/screen-extruder.png" width="640" alt="挤出界面。上方为喷嘴温度和 Nozzle too cold to extrude(因为喷嘴温度过低),以及 Length、Speed、Move(Retract / Extrude)的选择行">

用 `↑`/`↓` 选择长度(1/5/10/25/50 mm)、速度(2/5/10 mm/s)、方向(Retract / Extrude),用 `←`/`→` 切换。在任意一行按 `Enter` 均可执行。界面上方显示喷嘴温度以及当前能否挤出。

- 不执行的情况: 正在打印时,或喷嘴未达到可挤出温度(Klipper 的 `min_extrude_temp`)时。
- 暂停中可以执行(用于更换耗材)。

发送的指令为 `SAVE_GCODE_STATE` → `M83` → `G1 E<±长度> F<速度×60>`(`F` 的单位为 mm/min) → `RESTORE_GCODE_STATE`,因此不会改变打印任务的挤出模式。

#### 文件选择与开始打印(第 1 层的 `7`)

| 列表 | 详情 |
|---|---|
| <img src="docs/images/screen-files.png" width="640" alt="文件列表。G-code 文件按从新到旧排列并附带大小,显示第 1/6 页"> | <img src="docs/images/screen-files-detail.png" width="640" alt="文件详情。Time、Filament、Layer、Temps 各行,以及 Enter: start print Esc: back to list 的提示"> |

按从新到旧的顺序列出打印机上的 G-code 文件(每次打开界面时重新获取)。用 `↑`/`↓` 选择(`←`/`→` 每次移动 5 项),按 `Enter` 显示详情(缩略图、预计时间、耗材用量、层高、首层温度)。

在详情中再按一次 `Enter` 即开始打印,成功开始后返回状态界面。开始失败时,Toast 提示中会显示原因,并停留在 Files 界面。按 `ESC` 返回列表。正在打印或暂停中时不会开始打印。

#### 打印控制(第 1 层的 `8`)

<img src="docs/images/screen-control.png" width="640" alt="打印控制界面。上方为状态徽标、文件名和进度,以及 Pause、Cancel、E-Stop 按钮">

上方显示状态徽标、文件名、进度、已用 / 剩余时间和层数。用 `←`/`→` 选择按钮。

- Pause / Resume: 按 `Enter` 暂停或继续。暂停中标签变为 Resume。
- Cancel: 3 秒内按两次 `Enter` 即取消打印。第一次按下后按钮变红,按其他任意键可撤销。
- E-Stop: 长按 `Enter` 执行紧急停止(`printer.emergency_stop`)。即使 Klipper 未处于 ready,只要已连接 Moonraker 就会发送。

未在打印时,Pause 和 Cancel 显示为灰色,按下也只会提示“No print is running”。紧急停止后,在状态界面按两次 `F` 执行 `FIRMWARE_RESTART` 即可恢复。

#### 轴移动(第 2 层的 `4`)

<img src="docs/images/screen-move.png" width="640" alt="移动界面。上方为 X/Y/Z 位置和 Not homed,以及 Axis X/Y/Z、Step 0.1/1/10/50、X-10 / X+10、Home X、Home all、Motors off。导航栏为第 2 层(2/2)">

用 `↑`/`↓` 选择项目(轴 / 步长 / `−` 方向 / `+` 方向 / 该轴归零 / 全部轴归零 / 关闭电机)。轴(X/Y/Z)和步长(0.1/1/10/50 mm)用 `←`/`→` 切换,其他项目按 `Enter` 执行。界面上方显示工具头位置和已归零的轴(“Not homed”,或“Homed”后跟轴名)。

- 点动: 发送 `SAVE_GCODE_STATE` → `G91` → `G1 <轴><±移动量> F<进给速度>`(XY 3000 / Z 600 mm/min) → `RESTORE_GCODE_STATE`,因此不会改变打印任务的定位模式。
- 归零: `G28`(全部轴) / `G28 X`(该轴)。关闭电机: `M84`。

不执行的条件(原因会显示在 Toast 提示和最下一行):

- 正在打印时: 全部操作。
- 暂停中: 归零和关闭电机(会丢失暂停位置)。点动可以执行。
- 点动未归零的轴: 显示“Home the axis first”。

#### 连接设置(第 2 层的 `5`,或 `S`)

<img src="docs/images/screen-settings.png" width="640" alt="连接设置界面。Connect to Network、Host、Port、API key、Save & connect,最下一行为状态徽标。导航栏为第 2 层(2/2)">

用 `↑`/`↓` 选择项目,按 `Enter` 编辑 / 确认,用 `←`/`→` 切换选项,按 `ESC` 取消编辑 / 返回。连接目标可在“本设备(Unix 套接字)”和“网络(WebSocket,Host / Port / API key)”之间选择,通过“Save & connect”保存并重新连接。最下一行始终显示状态徽标,其旁边通常显示连接状态,在有保存结果或输入错误期间则显示相应消息。

## 路线图

1. ✅ Moonraker 通信层 + 状态显示界面
2. ✅ 温度设置、挤出/回抽、文件选择、打印控制、轴移动界面

## 构建

使用与 [CardputerZero/Template](https://github.com/CardputerZero/Template) 相同的 CMake Presets。

### Windows 桌面模拟器(MinGW)

前提条件: CMake 3.31+、Ninja、MinGW-w64(WinLibs 等)、vcpkg(需设置 `VCPKG_ROOT`)

```powershell
cmake --preset win32-mingw64
cmake --build build/mingw64 --config Debug
./build/mingw64/Debug/klipper-deck-zero.exe
```

使用 MSVC 时,请使用 `win32-msvc` 预设(`build/msvc`)。

### 面向实机(CardputerZero)的交叉编译 / 打包

```sh
cmake --workflow --preset cp0-cross-package   # dist/KlipperDeckZero_*_arm64.deb
REMOTE_USER=pi REMOTE_HOST=<ip> ./deploy.sh
```

## 配置文件

- 桌面: `config/klipper-deck-zero.conf`
- 实机: `/etc/klipper-deck-zero.conf`(用户配置为 `~/.config/klipper-deck-zero/klipper-deck-zero.conf`)

```ini
[moonraker]
# unix: 本机的 Klipper / websocket: 另一台机器上的 Klipper
# (默认: 实机 = unix, 模拟器 = websocket, 地址 127.0.0.1)
transport=websocket
socket_path=~/printer_data/comms/moonraker.sock
# websocket 时的连接目标 (ws://<host>:<port>/websocket)
host=192.168.1.50
port=7125
# 仅当客户端不在 trusted_clients 中时才需要
#api_key=
```

## 无打印机运行(模拟 Moonraker)

桌面构建中包含 `fake_moonraker`。它可模拟加热器升温、挤出温度检查、打印进度、紧急停止和固件重启。

```powershell
./build/mingw64/Debug/fake_moonraker.exe --speed 20   # ws://127.0.0.1:7125/websocket
./build/mingw64/Debug/klipper-deck-zero.exe
```

连接实机时,改为 `host=<打印机IP>`。

### README 截图

`docs/images/screen-*.png` 是将桌面模拟器连接到模拟 Moonraker 后拍摄的。修改界面后,可在 Debug 构建完成后用以下脚本全部重新拍摄(请确保端口 7125 未被占用)。

```powershell
./tools/readme_screenshots/capture.ps1
```

脚本向模拟器窗口发送按键操作,将应用截图功能保存的界面(320x170)叠加到设备外框图像上并保存。模拟器窗口无需在屏幕上可见。用 `D` 切换的主题会在退出时恢复原状。

## 测试

针对不依赖 LVGL 部分的单元测试。验证内容包括 Moonraker 客户端(JSON-RPC / 状态合并 / 连接流程)、配置的读取与保存、各界面的操作逻辑(连接设置表单、文件列表、温度、挤出、打印控制、移动)、G-code 状态的保存与恢复、缩略图的选择方式、重启确认、导航栏的层等。即使没有 LVGL 也可以单独构建。

```powershell
ctest --test-dir build/mingw64 -C Debug            # 包含在预设构建中
# 或者单独运行
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ./build/tests/kdz_tests
```

## 文档

- [设计文档](docs/design/README.md)(连接机制、操作界面设计)
- [Moonraker API × CardputerZero 技术调研笔记](docs/moonraker_tech_notes.md)
