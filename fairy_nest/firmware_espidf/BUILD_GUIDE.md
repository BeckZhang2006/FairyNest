# FairyNest ESP-IDF v5.5.2 固件构建指南（小白版）

> 本文面向完全不懂 ESP32 开发的新手，手把手教你从零开始编译和烧录 FairyNest 固件。

---

## 📋 目录

1. [你需要准备什么](#1-你需要准备什么)
2. [安装 ESP-IDF 开发环境](#2-安装-esp-idf-开发环境)
3. [下载 FairyNest 代码](#3-下载-fairynest-代码)
4. [配置项目](#4-配置项目)
5. [编译固件](#5-编译固件)
6. [烧录到 ESP32-S3](#6-烧录到-esp32-s3)
7. [查看运行日志](#7-查看运行日志)
8. [常见问题](#8-常见问题)

---

## 1. 你需要准备什么

### 硬件

| 物品 | 说明 | 价格 |
|------|------|------|
| ESP32-S3-DevKitC-1 (N16R8) | 主控开发板 | ￥30-50 |
| USB 数据线 | Type-C 或 Micro-USB（看开发板接口） | 自备 |
| 电脑 | Windows / macOS / Linux | - |

> 💡 如果你还没有其他模块（麦克风、功放、LED），也可以先只买开发板，把基础固件跑起来，后续再逐步添加模块。

### 软件

| 软件 | 用途 | 下载地址 |
|------|------|---------|
| ESP-IDF v5.5.2 | 编译和烧录工具 | https://dl.espressif.com/dl/esp-idf/ |
| Git | 下载代码 | https://git-scm.com/downloads |
| Python 3.11+ | 云端服务器用 | https://www.python.org/downloads/ |

---

## 2. 安装 ESP-IDF 开发环境

ESP-IDF 是乐鑫（Espressif）官方提供的开发框架，用来编译 ESP32 的固件。本项目使用 **ESP-IDF v5.5.2** 版本。

### 2.1 Windows 用户（推荐用官方离线安装器）

#### 步骤 1：下载安装器

1. 打开浏览器，访问：https://dl.espressif.com/dl/esp-idf/
2. 找到 **"ESP-IDF v5.5.2 - Offline Installer"**
3. 点击下载 `esp-idf-tools-setup-offline-5.5.2.exe`（约 1GB）

#### 步骤 2：运行安装器

1. 双击下载的 `.exe` 文件
2. 如果弹出 "Windows 已保护你的电脑"，点击 **"更多信息"** -> **"仍要运行"**
3. 点击 **"Next"** 开始安装
4. **选择安装路径**：建议保持默认 `C:\Espressif`，不要改！
5. **选择组件**：保持默认（全部勾选）
6. 点击 **"Install"**，等待安装完成（约 10-30 分钟，取决于电脑性能）
7. 安装完成后，点击 **"Finish"**

#### 步骤 3：验证安装

1. 点击 Windows 开始菜单
2. 找到 **"ESP-IDF 5.5.2"** 文件夹
3. 点击 **"ESP-IDF 5.5.2 CMD"**（一个黑色的命令行窗口会打开）
4. 在窗口中输入：
   ```bash
   idf.py --version
   ```
5. 按回车，如果看到：
   ```
   ESP-IDF v5.5.2
   ```
   说明安装成功！🎉

> 💡 **小技巧**：你可以右键点击 "ESP-IDF 5.5.2 CMD"，选择 **"更多" -> "打开文件位置"**，然后右键 -> **"发送到" -> "桌面快捷方式"**，以后直接从桌面打开。

### 2.2 macOS 用户

#### 步骤 1：安装 Homebrew（如果还没有）

打开终端（Terminal），输入：
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

#### 步骤 2：安装依赖

```bash
brew install cmake ninja dfu-util
```

#### 步骤 3：下载并安装 ESP-IDF

```bash
# 创建目录
mkdir -p ~/esp && cd ~/esp

# 克隆 ESP-IDF（包含所有子模块，约 1GB）
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git

# 进入目录
cd esp-idf

# 安装工具链（针对 ESP32-S3）
./install.sh esp32s3

# 设置环境变量
. ./export.sh

# 验证
idf.py --version
```

> ⚠️ **注意**：`./install.sh` 可能需要 20-40 分钟，请耐心等待。
>
> 💡 **提示**：`export.sh` 每次打开新终端都要执行。建议把它加到 `~/.zshrc` 或 `~/.bash_profile`：
> ```bash
echo 'alias get_idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.zshrc
```
> 以后每次新终端输入 `get_idf` 即可加载环境。

### 2.3 Linux (Ubuntu/Debian) 用户

#### 步骤 1：安装系统依赖

```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

#### 步骤 2：下载并安装 ESP-IDF

```bash
# 创建目录
mkdir -p ~/esp && cd ~/esp

# 克隆 ESP-IDF
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git

# 进入目录
cd esp-idf

# 安装工具链
./install.sh esp32s3

# 设置环境变量
. ./export.sh

# 验证
idf.py --version
```

> 💡 **提示**：同样建议把 `export.sh` 加到 `~/.bashrc`：
> ```bash
echo '. $HOME/esp/esp-idf/export.sh' >> ~/.bashrc
```

---

## 3. 下载 FairyNest 代码

### 3.1 使用 Git 下载（推荐）

在 ESP-IDF 命令行/终端中执行：

```bash
# 进入你想放代码的目录
# Windows:
cd C:\Users\你的用户名\Desktop
# macOS/Linux:
cd ~/Documents

# 克隆代码（替换为实际仓库地址）
git clone https://github.com/你的用户名/fairy_nest.git

# 进入项目目录
cd fairy_nest
```

### 3.2 直接下载 ZIP（没有 Git 时）

1. 打开浏览器，访问项目 GitHub 页面
2. 点击绿色的 **"<> Code"** 按钮
3. 选择 **"Download ZIP"**
4. 解压到桌面或文档目录
5. 在 ESP-IDF 命令行中，用 `cd` 命令进入解压后的 `fairy_nest/firmware_espidf` 目录

---

## 4. 配置项目

### 4.1 进入固件目录

```bash
cd fairy_nest/firmware_espidf
```

### 4.2 设置目标芯片

告诉 ESP-IDF 我们要编译的是 ESP32-S3：

```bash
idf.py set-target esp32s3
```

第一次执行会下载一些工具，可能需要几分钟。看到 `Done` 就是成功了。

### 4.3 打开配置菜单

```bash
idf.py menuconfig
```

会弹出一个蓝色/黑色的文本界面：

```
┌───────────────────────────── FairyNest Configuration ─────────────────────────────┐
│                                                                                   │
│  FairyNest Configuration  --->                                                    │
│  Serial flasher config  --->                                                     │
│                                                                                   │
│  ↑↓: 移动光标    Enter: 进入/选择    Esc: 返回    ?: 帮助    /: 搜索              │
│                                                                                   │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### 4.4 配置 WiFi 和服务器

1. 用 **方向键 ↓** 选中 **"FairyNest Configuration"**，按 **Enter**
2. 进入后看到：

```
┌──────────────────────────── FairyNest Configuration ────────────────────────────┐
│                                                                                 │
│  (YOUR_WIFI_SSID) WiFi SSID                                                     │
│  (YOUR_WIFI_PASSWORD) WiFi Password                                             │
│  (your-server.com) Cloud Server Host                                            │
│  (80) Cloud Server Port                                                         │
│  (/ws/device) WebSocket Path                                                    │
│  (YOUR_DEVICE_API_KEY) Device API Key                                           │
│  (CST-8) Timezone String                                                        │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

3. **配置 WiFi**：
   - 选中 **"WiFi SSID"**，按 **Enter**
   - 输入你家的 WiFi 名称（注意大小写必须完全一致）
   - 按 **Enter** 确认
   - 同样方法设置 **"WiFi Password"**

4. **配置服务器**（如果你还没有服务器，先填你电脑的局域网 IP）：
   - **查看电脑 IP**：
     - Windows：CMD 中输入 `ipconfig`，找 "IPv4 地址"
     - macOS/Linux：终端输入 `ifconfig` 或 `ip addr`
   - **Cloud Server Host**：填电脑 IP，如 `192.168.1.100`
   - **Cloud Server Port**：保持 `80`
   - **Device API Key**：随便设一个密码，如 `my-secret-key`

5. 按 **Esc** 返回上一级，选择 **"Save"** 保存
6. 再按 **Esc** 退出 menuconfig

> ⚠️ **重要提醒**：
> - ESP32-S3 只支持 **2.4GHz WiFi**，不支持 5GHz
> - WiFi 名称和密码区分大小写
> - 如果 WiFi 有中文名，可能会出问题，建议用英文名

### 4.5 安装 WebSocket 组件

FairyNest 需要额外的 WebSocket 组件，执行：

```bash
idf.py add-dependency "espressif/esp_websocket_client^1.2.3"
```

如果提示网络错误，多试几次。

---

## 5. 编译固件

### 5.1 执行编译

```bash
idf.py build
```

第一次编译非常慢（10-30分钟），因为需要下载和编译大量依赖。请耐心等待。

你会看到大量的编译输出，最后应该是：

```
[100%] Built target fairy_nest.elf
[100%] Generating binary image from built executable...
Generated .../fairy_nest.bin
```

这就是编译成功了！🎉

### 5.2 如果编译失败

常见错误及解决方法：

| 错误信息 | 原因 | 解决方法 |
|---------|------|---------|
| `esp_websocket_client.h: No such file` | WebSocket 组件未安装 | `idf.py add-dependency "espressif/esp_websocket_client^1.2.3"` |
| `driver/gpio.h: No such file` | CMakeLists.txt 缺少 driver | 检查 `main/CMakeLists.txt` 的 `REQUIRES` 中是否有 `driver` |
| 网络超时 | 下载依赖失败 | 检查网络，多试几次，或配置代理 |
| 内存不足 | 电脑内存不够 | 关闭其他程序，或增加虚拟内存 |

---

## 6. 烧录到 ESP32-S3

### 6.1 连接开发板

1. 用 USB 数据线把 ESP32-S3 开发板连接到电脑
2. **Windows**：打开"设备管理器" -> "端口(COM和LPT)"，记录 COM 口号（如 `COM3`）
3. **macOS**：终端输入 `ls /dev/cu.*`，记录设备名（如 `/dev/cu.usbserial-1120`）
4. **Linux**：输入 `ls /dev/ttyUSB*`，记录设备名（如 `/dev/ttyUSB0`）

> 💡 **Windows 驱动问题**：如果设备管理器里显示黄色感叹号，需要安装 CP210x 驱动：
> https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

### 6.2 执行烧录

**Windows**（COM3 为例）：
```bash
idf.py -p COM3 flash
```

**macOS**（/dev/cu.usbserial-1120 为例）：
```bash
idf.py -p /dev/cu.usbserial-1120 flash
```

**Linux**（/dev/ttyUSB0 为例）：
```bash
idf.py -p /dev/ttyUSB0 flash
```

烧录过程中会看到进度条：
```
Writing at 0x00010000... (1 %)
Writing at 0x00020000... (5 %)
...
Writing at 0x00080000... (100 %)
Wrote 1048576 bytes (123456 compressed) at 0x00010000 in 12.3 seconds
Leaving...
Hard resetting via RTS pin...
Done
```

烧录成功！🎉

### 6.3 完整命令（编译+烧录+监控）

如果你已经配置好了，以后修改代码后可以用一条命令完成全部：

```bash
# Windows
idf.py -p COM3 build flash monitor

# macOS/Linux
idf.py -p /dev/ttyUSB0 build flash monitor
```

---

## 7. 查看运行日志

### 7.1 打开串口监视器

```bash
# Windows
idf.py -p COM3 monitor

# macOS
idf.py -p /dev/cu.usbserial-1120 monitor

# Linux
idf.py -p /dev/ttyUSB0 monitor
```

### 7.2 正常启动日志

如果一切正常，你会看到：

```
========================================
  FairyNest v1.0.0 - Smart Bedside Terminal
  ESP-IDF v5.5.2 | ESP32-S3
  Build: Jan 10 2025
========================================

[OK] NVS flash initialized
[OK] FreeRTOS primitives created
[OK] Configuration loaded
Device ID: FAIRY_A1B2C3
[OK] LED PWM initialized
[OK] I2S audio initialized
[OK] WiFi+CSI initialized
[OK] WebSocket client initialized
[OK] Voice FSM initialized
[OK] Alarm system initialized
All tasks created successfully
Say "Hi Fairy" to wake me up
```

然后每隔几秒会输出状态：
```
Uptime: 10s | WiFi: OK | WS: OK | CSI: 5.2 | Presence: 0
```

### 7.3 退出监视器

按 `Ctrl+]`（按住 Ctrl，按右方括号键）退出。

> 💡 **Windows 用户注意**：有些键盘布局 `Ctrl+]` 不好按，可以按 `Ctrl+T` 然后按 `X`。

---

## 8. 常见问题

### Q1: 烧录时报错 "Failed to connect to ESP32"

**原因**：ESP32 没有进入下载模式。

**解决**：
1. 按住开发板上的 **BOOT** 按钮不放
2. 按一下 **RST** 按钮（复位）
3. 松开 **BOOT** 按钮
4. 重新执行烧录命令

### Q2: 编译时提示 "No module named 'idf_component_manager'"

**原因**：ESP-IDF 环境没有正确加载。

**解决**：
- Windows：确保你打开的是 **"ESP-IDF 5.5.2 CMD"**，不是普通 CMD
- macOS/Linux：先执行 `. ./export.sh` 加载环境

### Q3: WiFi 连接不上

**排查**：
1. 检查 `menuconfig` 中的 WiFi 名称和密码
2. 确认 WiFi 是 2.4GHz（看路由器设置）
3. 确认密码没有特殊字符（建议用字母+数字）
4. 把 ESP32 靠近路由器试试
5. 查看串口日志中的错误码

### Q4: 串口监视器输出乱码

**原因**：波特率不对。

**解决**：ESP-IDF 默认使用 115200 波特率，确保你的串口工具设置正确。

### Q5: 提示 "Permission denied"（Linux/macOS）

**原因**：当前用户没有串口权限。

**解决**：
```bash
# Linux
sudo usermod -a -G dialout $USER
# 然后注销并重新登录

# macOS
sudo chmod 666 /dev/cu.usbserial-*
```

### Q6: 编译时内存不足（Windows）

**解决**：
1. 关闭其他程序释放内存
2. 增加虚拟内存：
   - 右键 "此电脑" -> "属性" -> "高级系统设置"
   - "性能" -> "设置" -> "高级" -> "虚拟内存" -> "更改"
   - 选择系统盘，设置 "自定义大小"，初始大小 4096，最大值 8192
   - 点击 "设置" -> "确定"，重启电脑

### Q7: 如何修改代码后重新烧录？

修改代码后，只需要执行：
```bash
idf.py -p COM3 build flash
```

不需要重新 `set-target` 或 `menuconfig`。

### Q8: 如何完全重新编译？

如果编译出现奇怪的错误，可以清理后重新编译：
```bash
idf.py fullclean
idf.py build
```

---

## 附录：项目文件说明

```
firmware_espidf/
├── CMakeLists.txt              # 项目顶层配置
├── sdkconfig.defaults          # 默认 SDK 配置（分区、频率等）
├── partitions.csv              # Flash 分区表
└── main/                       # 主程序目录
    ├── CMakeLists.txt          # 组件配置（源文件列表、依赖）
    ├── Kconfig.projbuild       # menuconfig 自定义选项
    ├── idf_component.yml       # 组件依赖（WebSocket 等）
    ├── fairy_nest.h            # 公共头文件（引脚定义、配置常量）
    ├── fairy_nest_main.c       # 程序入口：初始化 + 创建任务
    ├── wifi_csi.c              # WiFi 连接 + CSI 人体检测
    ├── i2s_audio.c             # I2S 麦克风采集 + 扬声器播放
    ├── led_pwm.c               # LED PWM 调光 + 渐变效果
    ├── websocket_mgr.c         # WebSocket 客户端（连接云端）
    ├── voice_fsm.c             # 语音状态机（唤醒 -> 录音 -> 播放）
    ├── alarm_sys.c             # 闹钟系统 + 智能唤醒
    └── config_mgr.c            # NVS 配置管理（保存 WiFi 等设置）
```

## 附录：引脚接线速查表

```
ESP32-S3          INMP441          MAX98357A        LED
--------          -------          ---------        ---
GPIO 5    <----   WS (LRCK)
GPIO 6    <----   SCK (BCLK)
GPIO 7    <----   SD (DATA)
3.3V      ---->   VDD
GND       ---->   GND

GPIO 15   ---->                    BCLK
GPIO 16   ---->                    LRC
GPIO 8    ---->                    DIN
5V        ---->                    Vin
GND       ---->                    GND

GPIO 4    ---->                                     DIN
5V        ---->                                     VCC
GND       ---->                                     GND
```

---

> 如有问题，请参考主文档 `../docs/FairyNest完整使用文档.md` 或提交 GitHub Issue。
