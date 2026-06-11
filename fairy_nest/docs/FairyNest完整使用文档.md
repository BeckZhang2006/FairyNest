# FairyNest 智能床头终端 - 小白从零开始部署指南

> **本文目标**：让完全不懂编程、不懂硬件的小白，也能一步步跟着操作，把 FairyNest 智能床头终端跑起来。
>
> 版本: v1.0.0 | 日期: 2025-1 | 硬件: ESP32-S3

---

## 📋 目录

1. [你需要准备什么？](#1-你需要准备什么)
2. [第一步：买齐硬件并接线](#2-第一步买齐硬件并接线)
3. [第二步：搭建电脑上的开发环境](#3-第二步搭建电脑上的开发环境)
4. [第三步：下载本项目代码](#4-第三步下载本项目代码)
5. [第四步：配置 WiFi 和服务器信息](#5-第四步配置-wifi-和服务器信息)
6. [第五步：编译并烧录固件到 ESP32](#6-第五步编译并烧录固件到-esp32)
7. [第六步：部署云端服务器](#7-第六步部署云端服务器)
8. [第七步：打开前端管理界面](#8-第七步打开前端管理界面)
9. [第八步：测试语音和夜灯功能](#9-第八步测试语音和夜灯功能)
10. [常见问题排查](#10-常见问题排查)

---

## 1. 你需要准备什么？

### 1.1 硬件清单（全部可以在淘宝买到）

| 组件 | 型号/规格 | 数量 | 参考价格 | 购买渠道 |
|------|----------|------|---------|---------|
| **主控板** | ESP32-S3-DevKitC-1 (N16R8) | 1 | ￥30-50 | 淘宝/立创 |
| **麦克风** | INMP441 I2S MEMS | 1 | ￥8-15 | 淘宝 |
| **音频功放** | MAX98357A I2S 3W | 1 | ￥6-12 | 淘宝 |
| **扬声器** | 4Ω 3W 小喇叭 | 1 | ￥3-5 | 淘宝 |
| **LED灯带** | WS2812B 暖白光 5V | 1米 | ￥10-20 | 淘宝 |
| **电源** | 5V 2A USB 电源适配器 | 1 | ￥10-15 | 淘宝 |
| **杜邦线** | 母对母/公对母 20cm | 若干 | ￥5 | 淘宝 |
| **USB 数据线** | Type-C 或 Micro-USB（看开发板接口） | 1 | 自备 | - |

**总计预算: 约 ￥80-150**

> 💡 **省钱提示**：如果你只是想先跑通代码看看效果，可以先只买 **ESP32-S3 开发板**（￥30-50），其他模块后续再加。

### 1.2 电脑环境要求

- **操作系统**：Windows 10/11、macOS、或 Linux（Ubuntu 推荐）
- **网络**：能正常访问 GitHub（可能需要科学上网）
- **磁盘空间**：至少 10GB 空闲空间（ESP-IDF 比较大）
- **内存**：8GB 以上

---

## 2. 第一步：买齐硬件并接线

### 2.1 认识你的 ESP32-S3 开发板

ESP32-S3-DevKitC-1 是一块蓝色（或黑色）的小电路板，上面有一个 USB 口，四周有很多金属针脚（GPIO）。

**关键引脚位置**（以开发板正面朝上、USB 口朝左为准）：
- `3.3V` — 给麦克风供电（⚠️ 不能接 5V，会烧坏！）
- `5V` — 给 LED 灯带和功放供电
- `GND` — 地线（所有模块的负极都要接到这里）
- `GPIO 4/5/6/7/8/15/16` — 信号线，用来传输数据

### 2.2 接线图（按这个接，不要接错）

```
ESP32-S3-DevKitC-1 引脚分配:
===========================

+-----------+        +-----------------+
|           |        |   INMP441 麦克风 |
|  ESP32    |        |                 |
|   S3      |        | VDD  -> 3.3V    |
|           |        | GND  -> GND     |
|  GPIO 5   |<-------| WS   (LRCK)     |
|  GPIO 6   |<-------| SCK  (BCLK)     |
|  GPIO 7   |<-------| SD   (DATA)     |
|           |        | L/R  -> GND     |
|           |        +-----------------+
|           |
|           |        +-----------------+
|           |        | MAX98357A 功放   |
|           |        |                 |
|           |        | Vin  -> 5V      |
|  GPIO 15  |------->| BCLK            |
|  GPIO 16  |------->| LRC  (LRCK)     |
|  GPIO 8   |------->| DIN  (DATA)     |
|           |        | GND  -> GND     |
|           |        | OUT+ -> 喇叭+   |
|           |        | OUT- -> 喇叭-   |
|           |        +-----------------+
|           |
|           |        +-----------------+
|           |        | PWM LED 灯带    |
|           |        |                 |
|  GPIO 4   |------->| DIN  (PWM信号)  |
|           |        | VCC  -> 5V      |
|           |        | GND  -> GND     |
|           |        +-----------------+
+-----------+

其他必要连接:
- 3.3V -> 麦克风 VDD
- 5V   -> LED灯带 VCC, 功放 Vin
- GND  -> 共地连接（所有 GND 连在一起）
```

### 2.3 详细接线步骤（一步一步来）

#### 🔴 重要提醒

> ⚠️ **INMP441 麦克风的 VDD 必须接 3.3V，绝对不能接 5V！接 5V 会立刻烧坏麦克风！**
>
> ⚠️ **接线前请确保 USB 没有插在电脑上（断电操作更安全）**

#### 步骤 1：接麦克风（INMP441）

| INMP441 引脚 | ESP32-S3 引脚 | 说明 |
|-------------|--------------|------|
| VDD | **3.3V** | 电源 (⚠️ 切勿接5V!) |
| GND | GND | 地线 |
| SCK | GPIO 6 | I2S 位时钟 |
| WS | GPIO 5 | I2S 字选择 |
| SD | GPIO 7 | I2S 数据输入 |
| L/R | GND | 左声道选择 |

**操作**：用杜邦线把麦克风上的每个引脚，按照上表接到开发板对应的引脚上。

#### 步骤 2：接功放（MAX98357A）

| MAX98357A 引脚 | ESP32-S3 引脚 | 说明 |
|---------------|--------------|------|
| Vin | **5V** | 电源 |
| GND | GND | 地线 |
| BCLK | GPIO 15 | I2S 位时钟 |
| LRC | GPIO 16 | I2S 左右时钟 |
| DIN | GPIO 8 | I2S 数据输入 |
| GAIN | 悬空不接 | 默认 +9dB 增益 |

**操作**：
1. 用杜邦线连接 Vin、GND、BCLK、LRC、DIN
2. 扬声器（小喇叭）的两根线接到功放的 `OUT+` 和 `OUT-` 上
3. **注意**：喇叭线不要接地，只接 OUT+ 和 OUT-

#### 步骤 3：接 LED 灯带

| LED 引脚 | ESP32-S3 引脚 | 说明 |
|---------|--------------|------|
| DIN | GPIO 4 | PWM 信号输入 |
| VCC | 5V | 电源 |
| GND | GND | 地线 |

**注意**：
- 如果灯带较长（超过 50 颗 LED），需要额外供电，不能仅靠 ESP32 的 USB 供电
- 可在信号线串联一个 330Ω 电阻保护 ESP32（可选）

#### 步骤 4：检查接线

接完后，用万用表（如果有的话）检查：
1. 3.3V 和 GND 之间是否有短路
2. 5V 和 GND 之间是否有短路
3. 各信号线是否接对位置

---

## 3. 第二步：搭建电脑上的开发环境

本项目使用 **ESP-IDF v5.5.2** 进行开发。ESP-IDF 是乐鑫官方的开发框架，用来编译和烧录 ESP32 的固件。

### 3.1 安装 ESP-IDF（Windows 用户）

#### 方法 A：使用官方安装器（推荐小白）

1. **下载安装器**
   - 打开浏览器，访问：https://dl.espressif.com/dl/esp-idf/
   - 找到 **"ESP-IDF v5.5.2 - Offline Installer"**（离线安装器，大约 1GB）
   - 下载 `esp-idf-tools-setup-offline-5.5.2.exe`

2. **运行安装器**
   - 双击下载的 `.exe` 文件
   - 一路点击 "Next"，保持默认选项
   - 安装路径建议保持默认：`C:\Espressif`
   - 等待安装完成（可能需要 10-30 分钟）

3. **验证安装**
   - 安装完成后，开始菜单里会出现 **"ESP-IDF 5.5.2 CMD"**
   - 点击打开它（一个黑色的命令行窗口）
   - 输入以下命令，按回车：
   ```bash
   idf.py --version
   ```
   - 如果看到类似 `ESP-IDF v5.5.2` 的输出，说明安装成功！🎉

#### 方法 B：使用 VS Code + ESP-IDF 插件（推荐有编程基础的用户）

1. 下载安装 [VS Code](https://code.visualstudio.com/)
2. 打开 VS Code，点击左侧的扩展图标（四个方块）
3. 搜索 "ESP-IDF"，安装 **Espressif IDF** 插件
4. 按 `Ctrl+Shift+P`，输入 "ESP-IDF: Configure ESP-IDF Extension"
5. 选择 **"ADVANCED"** 模式
6. 选择版本 **v5.5.2**，点击 Install
7. 等待安装完成

### 3.2 安装 ESP-IDF（macOS / Linux 用户）

打开终端（Terminal），依次执行以下命令：

```bash
# 1. 创建目录
mkdir -p ~/esp && cd ~/esp

# 2. 克隆 ESP-IDF（包含所有子模块）
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git

# 3. 进入目录并安装工具链
cd esp-idf
./install.sh esp32s3

# 4. 设置环境变量（每次新终端都需要执行）
. ./export.sh

# 5. 验证
idf.py --version
# 应输出: ESP-IDF v5.5.2
```

> 💡 **提示**：`export.sh` 每次打开新终端都要执行。你可以把它加到 `~/.bashrc` 或 `~/.zshrc` 里，这样每次自动加载。

### 3.3 安装 Python（云端服务器需要）

云端服务器需要 Python 3.11 或更高版本。

**Windows**：
1. 访问 https://www.python.org/downloads/
2. 下载 Python 3.11.x
3. 安装时 **勾选 "Add Python to PATH"**
4. 打开 CMD，输入 `python --version` 验证

**macOS**：
```bash
brew install python@3.11
```

**Linux (Ubuntu)**：
```bash
sudo apt update
sudo apt install -y python3.11 python3.11-venv python3.11-dev
```

### 3.4 安装 Node.js（前端需要）

前端界面需要 Node.js 20 或更高版本。

**Windows/macOS**：
1. 访问 https://nodejs.org/
2. 下载 LTS 版本（推荐 20.x）
3. 安装，保持默认选项

**Linux (Ubuntu)**：
```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
```

验证安装：
```bash
node --version   # 应输出 v20.x.x
npm --version    # 应输出 10.x.x
```

---

## 4. 第三步：下载本项目代码

### 4.1 使用 Git 下载（推荐）

**Windows**：
1. 打开 **"ESP-IDF 5.5.2 CMD"**（开始菜单里找）
2. 执行以下命令：

```bash
# 进入你想放代码的目录（比如桌面）
cd C:\Users\你的用户名\Desktop

# 克隆代码
git clone https://github.com/你的仓库地址/fairy_nest.git

# 进入项目目录
cd fairy_nest
```

**macOS / Linux**：
```bash
# 进入你想放代码的目录
cd ~/Documents

# 克隆代码
git clone https://github.com/你的仓库地址/fairy_nest.git

# 进入项目目录
cd fairy_nest
```

### 4.2 如果没有 Git，直接下载 ZIP

1. 打开浏览器，访问项目 GitHub 页面
2. 点击绿色的 **"<> Code"** 按钮
3. 选择 **"Download ZIP"**
4. 解压到桌面或文档目录

---

## 5. 第四步：配置 WiFi 和服务器信息

### 5.1 进入配置界面

在 ESP-IDF 命令行中，进入固件目录：

```bash
cd fairy_nest/firmware_espidf
```

然后运行配置命令：

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

运行后会弹出一个蓝色/黑色的文本界面（类似下图）：

```
┌───────────────────────────── FairyNest Configuration ─────────────────────────────┐
│                                                                                   │
│  FairyNest Configuration  --->                                                    │
│  Serial flasher config  --->                                                     │
│                                                                                   │
│                                                                                   │
│                                                                                   │
│  ↑↓: 移动光标    Enter: 进入/选择    Esc: 返回    ?: 帮助    /: 搜索              │
│                                                                                   │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### 5.2 配置 WiFi 信息

1. 用 **方向键 ↓** 选中 **"FairyNest Configuration"**，按 **Enter** 进入
2. 你会看到以下选项：

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

3. 用 **方向键 ↓** 选中 **"WiFi SSID"**，按 **Enter**
4. 输入你家里的 **WiFi 名称**（注意大小写要完全一致），按 **Enter** 确认
5. 同样方法设置 **WiFi Password**（WiFi 密码）

> ⚠️ **注意**：ESP32-S3 只支持 **2.4GHz WiFi**，不支持 5GHz！请确保你的 WiFi 是 2.4GHz 的。

### 5.3 配置服务器信息

如果你暂时还没有服务器，可以先填你电脑的局域网 IP：

1. **查看你电脑的 IP 地址**：
   - **Windows**：打开 CMD，输入 `ipconfig`，找到 "IPv4 地址"（类似 `192.168.1.xxx`）
   - **macOS/Linux**：打开终端，输入 `ifconfig` 或 `ip addr`，找到 `inet` 后面的地址

2. 在 menuconfig 中设置：
   - **Cloud Server Host**：填你电脑的 IP，比如 `192.168.1.100`
   - **Cloud Server Port**：保持默认 `80`
   - **WebSocket Path**：保持默认 `/ws/device`
   - **Device API Key**：随便填一个密码，比如 `my-secret-key-123`

3. 按 **Esc** 退出，选择 **"Save"** 保存，再按 **Esc** 退出 menuconfig

### 5.4 安装 WebSocket 组件依赖

ESP-IDF 需要额外安装一个 WebSocket 组件，执行：

```bash
idf.py add-dependency "espressif/esp_websocket_client^1.2.3"
```

如果提示网络错误，多试几次，或者检查网络连接。

---

## 6. 第五步：编译并烧录固件到 ESP32

### 6.1 连接 ESP32-S3 到电脑

1. 用 USB 数据线把 ESP32-S3 开发板连接到电脑
2. **Windows**：打开"设备管理器" -> "端口(COM和LPT)"，查看出现的 COM 口号（比如 `COM3`）
3. **macOS**：打开终端，输入 `ls /dev/cu.*`，找到类似 `/dev/cu.usbserial-xxx` 的设备
4. **Linux**：输入 `ls /dev/ttyUSB*`，找到类似 `/dev/ttyUSB0` 的设备

### 6.2 编译固件

在 `firmware_espidf` 目录下执行：

```bash
idf.py build
```

第一次编译会比较慢（10-30分钟），因为需要下载很多依赖。请耐心等待，看到类似下面的输出就是成功了：

```
[100%] Built target fairy_nest.elf
[100%] Generating binary image from built executable...
Generated .../fairy_nest.bin
```

### 6.3 烧录固件

**Windows**（假设 COM 口是 COM3）：
```bash
idf.py -p COM3 flash
```

**macOS/Linux**（假设设备是 /dev/ttyUSB0）：
```bash
idf.py -p /dev/ttyUSB0 flash
```

烧录过程中会看到进度条，完成后显示：
```
Leaving...
Hard resetting via RTS pin...
Done
```

### 6.4 查看串口输出（验证是否成功）

烧录完成后，打开串口监视器查看启动日志：

```bash
# Windows
idf.py -p COM3 monitor

# macOS/Linux
idf.py -p /dev/ttyUSB0 monitor
```

如果一切正常，你会看到类似这样的输出：

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

> 🎉 **恭喜你！固件烧录成功！**
>
> 按 `Ctrl+]` 退出串口监视器。

### 6.5 一键命令（熟练后使用）

如果你已经配置好了，以后每次修改代码后可以用一条命令完成全部操作：

```bash
# 清理 + 设置目标 + 编译 + 烧录 + 监控
idf.py -p COM3 fullclean set-target esp32s3 build flash monitor
```

---

## 7. 第六步：部署云端服务器

云端服务器负责：语音识别（Whisper）、AI 对话（DeepSeek/OpenAI）、语音合成（Edge-TTS）、设备管理。

> 🌐 **本文以云服务器部署为例**
>
> 云服务器 IP：`38.76.182.229` | 系统：`Ubuntu 22.04`
>
> 使用云服务器的好处是 **24 小时在线**，无论你在家还是外出，ESP32 小夜灯都能随时连接到"云端大脑"。

### 7.1 连接到你的云服务器

#### 云服务器信息

| 项目 | 值 |
|------|-----|
| **公网 IP** | `38.76.182.229` |
| **操作系统** | Ubuntu 22.04 LTS |
| **登录账号** | `root`（或你创建的用户名） |
| **登录方式** | SSH |

#### Windows 用户连接方法

**方式一：使用 PowerShell（推荐，Windows 10/11 自带）**

1. 在电脑桌面左下角搜索框输入 `powershell`，打开 **Windows PowerShell**
2. 输入以下命令连接服务器（把 `root` 换成你的实际用户名）：
```bash
ssh root@38.76.182.229
```
3. 第一次连接会提示 `Are you sure you want to continue connecting?`，输入 `yes` 按回车
4. 输入密码（输入时不会显示任何字符，这是正常的），按回车

**方式二：使用 PuTTY（旧版 Windows）**

1. 下载 PuTTY：https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html
2. 打开 PuTTY，在 **Host Name** 填入 `38.76.182.229`
3. 端口保持 `22`，点击 **Open**
4. 登录名输入 `root`，然后输入密码

#### macOS / Linux 用户连接方法

打开终端，执行：
```bash
ssh root@38.76.182.229
```

> ✅ 连接成功后，你会看到类似下面的提示符，表示已经进入云服务器：
> ```
> root@ubuntu:~#
> ```
> 接下来所有的命令都在这个远程服务器的终端里执行！

### 7.2 安装系统依赖

连接上服务器后，依次执行以下命令。可以直接复制粘贴。

**第 1 步：更新系统软件包**
```bash
apt update && apt upgrade -y
```
> 这会更新系统的软件列表，可能需要 1-3 分钟。

**第 2 步：安装必要软件**
```bash
apt install -y python3.11 python3.11-venv python3.11-dev python3-pip ffmpeg git curl
```

| 软件 | 作用 |
|------|------|
| `python3.11` | 运行后端程序 |
| `python3.11-venv` | 创建虚拟环境 |
| `python3-pip` | Python 包管理器 |
| `ffmpeg` | 音频处理（语音功能必需） |
| `git` | 下载项目代码 |
| `curl` | 测试网络请求 |

### 7.3 下载 FairyNest 代码

**第 3 步：下载代码**

```bash
cd /opt
git clone https://github.com/你的用户名/fairy_nest.git
cd FairyNest/fairy_nest/cloud_server
```

> 💡 **提示**：如果你的项目代码在本地电脑上，还没传到 GitHub，可以用下面的方法从本地上传到服务器：
> ```bash
> # 在本地电脑的 PowerShell/CMD 中执行（不是服务器里！）
> scp -r C:\Users\你的用户名\Desktop\fairy_nest root@38.76.182.229:/opt/
> ```
> 这需要一些时间，上传完成后在服务器上执行 `cd /opt/FairyNest/fairy_nest/cloud_server`。

### 7.4 创建虚拟环境并安装依赖

**什么是虚拟环境？** 简单说就是一个"隔离的房间"，把项目需要的 Python 包装在里面，不会影响服务器上的其他程序。

**第 4 步：创建虚拟环境**
```bash
python3.11 -m venv venv
```

执行后会在当前目录创建一个 `venv` 文件夹。

**第 5 步：激活虚拟环境**
```bash
source venv/bin/activate
```

激活后，命令行前面会出现 `(venv)`：
```
(venv) root@ubuntu:/opt/FairyNest/fairy_nest/cloud_server#
```

**第 6 步：升级 pip、setuptools 并安装依赖**
```bash
pip install --upgrade pip setuptools wheel
pip install --no-build-isolation -r requirements.txt
```

> 💡 **说明**：`--no-build-isolation` 参数让 pip 使用当前虚拟环境中的 setuptools 来构建依赖包（如 openai-whisper），避免临时隔离环境中 `pkg_resources` 缺失导致的构建失败。

这会下载很多 Python 包（约 500MB，包括 Whisper 模型），**可能需要 5-15 分钟**，请耐心等待。看到 `Successfully installed ...` 就是成功了！

> **如果下载很慢**：可以换国内镜像源
> ```bash
> pip config set global.index-url https://pypi.tuna.tsinghua.edu.cn/simple
> pip install -r requirements.txt
> ```

### 7.5 配置环境变量

**第 7 步：创建 .env 配置文件**

```bash
nano .env
```

这会打开 nano 编辑器。把下面的内容复制进去（根据实际情况修改 API Key）：

```env
# ============================================
# FairyNest 云端服务器配置文件
# ============================================

# 服务器端口（默认 8080，不用改）
FAIRYNEST_PORT=8080

# 语音识别模型（base = 平衡型，推荐新手）
FAIRYNEST_WHISPER_MODEL=base

# 语音合成引擎（edge = 微软Edge语音，免费）
FAIRYNEST_TTS_ENGINE=edge
FAIRYNEST_TTS_VOICE=zh-CN-XiaoxiaoNeural

# AI 大模型配置（三选一）
FAIRYNEST_LLM_PROVIDER=deepseek
FAIRYNEST_DEEPSEEK_API_KEY=sk-你的deepseek密钥

# 如果你用 OpenAI，取消下面两行的注释
# FAIRYNEST_LLM_PROVIDER=openai
# FAIRYNEST_OPENAI_API_KEY=sk-你的openai密钥

# 如果你用百度文心一言
# FAIRYNEST_LLM_PROVIDER=baidu
# FAIRYNEST_BAIDU_API_KEY=你的百度key
# FAIRYNEST_BAIDU_SECRET_KEY=你的百度secret
```

**保存退出**：按 `Ctrl+O`（保存），然后按 `Enter` 确认，再按 `Ctrl+X`（退出）。

> 🔑 **如何获取 API Key？**
> - **DeepSeek（推荐）**：访问 https://platform.deepseek.com，注册账号 → 左侧 "API Keys" → 创建 API Key → 复制密钥
> - **OpenAI**：访问 https://platform.openai.com，注册并创建 API Key
> - **百度**：访问 https://console.bce.baidu.com，创建语音应用获取 Key

### 7.6 开放防火墙端口

云服务器默认会拦截外部访问，需要手动开放端口。

**第 8 步：开放服务器本地防火墙**

```bash
ufw allow 8080/tcp
ufw allow 80/tcp
ufw allow 443/tcp
```

**第 9 步：在云服务商控制台开放安全组（重要！）**

仅仅在服务器里开放端口还不够，云服务商（阿里云/腾讯云/华为云等）还有一层"安全组"防火墙，需要在网页控制台里配置：

1. 打开你的云服务商控制台网页
2. 找到你的服务器，点击 **"安全组"** 或 **"防火墙"**
3. 点击 **"添加规则"**，填入：
   - **端口范围**：`8080`
   - **授权对象**：`0.0.0.0/0`（允许所有 IP）
   - **协议**：`TCP`
4. 再添加一条规则：
   - **端口范围**：`80`
   - **授权对象**：`0.0.0.0/0`
   - **协议**：`TCP`

> ⚠️ **安全提醒**：生产环境不要开放所有 IP，应该限制为你家的公网 IP。

### 7.7 启动服务器

**第 10 步：启动后端服务**

确保虚拟环境已激活（命令行前面有 `(venv)`），然后执行：

```bash
python main.py
```

第一次启动会比较慢（Whisper 模型需要加载），看到类似下面的输出就是成功了：

```
==================================================
FairyNest Cloud Server Starting...
==================================================
Database initialized
Loading Whisper model: base
Whisper model loaded successfully
TTS Service initialized: engine=edge, voice=zh-CN-XiaoxiaoNeural
LLM Service initialized: provider=deepseek, model=deepseek-chat

INFO:     Started server process [12345]
INFO:     Waiting for application startup.
INFO:     Application startup complete.
INFO:     Uvicorn running on http://0.0.0.0:8080 (Press CTRL+C to quit)
```

> 🎉 **后端服务器启动成功！**

**按 `Ctrl+C` 先停止服务器**，我们先去部署前端，稍后配置成后台常驻运行。

### 7.8 测试后端是否正常工作

启动服务器后，在你的**本地电脑**浏览器中访问：

```
http://38.76.182.229:8080/api/health
```

如果看到：
```json
{"status":"ok","timestamp":"...","services":true,"devices_online":0}
```

说明后端一切正常！

---

## 8. 第七步：部署前端管理界面

前端是一个网页，用来控制设备、设置闹钟、查看 CSI 检测状态等。

因为使用的是云服务器，前端也需要部署到服务器上，才能在任何地方通过浏览器访问。

### 8.1 在云服务器上安装 Node.js

前端是用 React + Vite 构建的，需要 Node.js 环境。

**第 1 步：安装 Node.js 20.x**

在云服务器终端中执行：

```bash
# 下载 NodeSource 安装脚本
curl -fsSL https://deb.nodesource.com/setup_20.x | bash -

# 安装 Node.js
apt install -y nodejs
```

**验证安装**：
```bash
node -v
npm -v
```

应该看到类似 `v20.x.x` 和 `10.x.x` 的版本号。

### 8.2 构建前端代码

**第 2 步：进入前端目录**

```bash
cd /opt/FairyNest/app
```

**第 3 步：安装前端依赖**

```bash
npm install
```

这会下载前端需要的各种库（约 200MB），可能需要 2-5 分钟。

**第 4 步：构建生产版本**

```bash
npm run build
```

构建完成后，会生成一个 `dist/` 文件夹，里面就是可以部署的静态网页文件。

### 8.3 把前端挂载到后端

FairyNest 的后端（FastAPI）已经内置了静态文件服务功能，只要把构建好的前端文件放到 `cloud_server/static/` 目录下，访问服务器时就会自动显示前端页面。

**第 5 步：复制前端文件到后端目录**

```bash
# 创建 static 目录（如果不存在）
mkdir -p /opt/FairyNest/fairy_nest/cloud_server/static

# 复制构建好的文件
cp -r /opt/FairyNest/app/dist/* /opt/FairyNest/fairy_nest/cloud_server/static/
```

> 💡 **原理说明**：`cloud_server/main.py` 会自动检测 `static/` 目录是否存在，如果存在就会把前端页面挂载到根路径 `/`。

### 8.4 配置 Nginx（推荐，更专业）

如果你想用标准的 80 端口访问（不用带 `:8080`），或者以后想用域名，推荐安装 Nginx 做反向代理。

**第 6 步：安装 Nginx**

```bash
apt install -y nginx
```

**第 7 步：创建 Nginx 配置文件**

```bash
nano /etc/nginx/sites-available/fairynest
```

写入以下内容：

```nginx
server {
    listen 80;
    server_name 38.76.182.229;

    # 前端静态文件
    location / {
        root /opt/FairyNest/fairy_nest/cloud_server/static;
        try_files $uri $uri/ /index.html;
    }

    # API 接口反向代理到后端
    location /api/ {
        proxy_pass http://localhost:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }

    # WebSocket 反向代理
    location /ws/ {
        proxy_pass http://localhost:8080;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
    }
}
```

保存退出（`Ctrl+O`，`Enter`，`Ctrl+X`）。

**第 8 步：启用配置并重启 Nginx**

```bash
# 创建软链接启用站点
ln -s /etc/nginx/sites-available/fairynest /etc/nginx/sites-enabled/

# 删除默认配置（避免冲突）
rm -f /etc/nginx/sites-enabled/default

# 检查配置是否正确
nginx -t

# 重启 Nginx
systemctl restart nginx
```

> ✅ 现在访问 `http://38.76.182.229`（不带端口号）就能看到前端页面了！

### 8.5 让后端后台运行（systemd）

现在前端通过 Nginx 访问了，但后端还在用 `python main.py` 前台运行。我们需要把后端设置为系统服务，这样即使关闭 SSH 窗口也不会停止。

**第 9 步：创建 systemd 服务文件**

```bash
nano /etc/systemd/system/fairynest.service
```

写入以下内容：

```ini
[Unit]
Description=FairyNest Cloud Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/FairyNest/fairy_nest/cloud_server
Environment=PATH=/opt/FairyNest/fairy_nest/cloud_server/venv/bin
ExecStart=/opt/FairyNest/fairy_nest/cloud_server/venv/bin/python main.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

保存退出。

**第 10 步：启动并启用服务**

```bash
# 重新加载 systemd
systemctl daemon-reload

# 设置开机自启
systemctl enable fairynest

# 启动服务
systemctl start fairynest

# 查看状态
systemctl status fairynest
```

看到 `active (running)` 就是成功了！

**常用命令**：
```bash
# 查看实时日志
journalctl -u fairynest -f

# 重启服务
systemctl restart fairynest

# 停止服务
systemctl stop fairynest
```

### 8.6 在浏览器中打开前端

在你的**本地电脑**浏览器中访问：

**如果你配置了 Nginx（推荐）：**
```
http://38.76.182.229
```

**如果没有配置 Nginx：**
```
http://38.76.182.229:8080
```

你应该能看到 FairyNest 的管理界面！

> 🎉 **恭喜！前后端都部署成功了！**

### 8.7 修改 ESP32 连接到云服务器

还记得[第 5 步](#5-第四步配置-wifi-和服务器信息)中配置的服务器地址吗？当时如果你填的是本地 IP，现在需要改成云服务器的公网 IP。

**重新配置 ESP32：**

1. 在本地电脑打开终端，进入固件目录：
```bash
cd fairy_nest/firmware_espidf
```

2. 打开 menuconfig：
```bash
idf.py menuconfig
```

3. 修改以下配置：
   - **Cloud Server Host**：改成 `38.76.182.229`
   - **Cloud Server Port**：如果用了 Nginx 保持 `80`，如果直接访问后端改成 `8080`
   - 其他保持不变

4. 保存退出，重新编译烧录：
```bash
idf.py -p COM3 build flash
```

> 烧录完成后，ESP32 重启后就会自动连接到你的云服务器了！

### 8.8 部署完成检查清单

| 检查项 | 验证方法 |
|--------|---------|
| 后端服务运行中 | `systemctl status fairynest` 显示 active |
| 前端页面能打开 | 浏览器访问 `http://38.76.182.229` 正常 |
| API 接口正常 | 访问 `http://38.76.182.229/api/health` 返回 ok |
| ESP32 已连接 | 前端页面显示设备在线 |

---

## 9. 第八步：测试语音和夜灯功能

### 9.1 确认设备已连接

1. 确保 ESP32-S3 已经上电（USB 连接电脑或电源适配器）
2. 确保云端服务器正在运行
3. 打开前端页面，查看设备状态是否为 "在线"

### 9.2 测试语音唤醒

对着麦克风说：
```
Hi Fairy
```

如果听到提示音（"滴"的一声），说明唤醒成功！然后你可以说：
- "开灯" — 夜灯应该亮起
- "关灯" — 夜灯应该熄灭
- "调亮一点" — 亮度增加
- "现在几点了" — AI 会回答当前时间

### 9.3 测试 CSI 人体检测

1. 在前端页面点击 "CSI 检测" 标签
2. 查看 "人体检测状态"
3. 你走进房间，状态应该变为 "有人"
4. 离开房间 5 秒后，状态应该变回 "无人"

### 9.4 测试闹钟

1. 在前端页面点击 "闹钟" 标签
2. 点击 "添加闹钟"
3. 设置一个 1 分钟后的闹钟
4. 等待闹钟响起，夜灯会逐渐变亮，同时播放唤醒音

---

## 10. 常见问题排查

### 10.1 `pip install` 报错：`No module named 'pkg_resources'`

**现象**：安装 `openai-whisper` 时出现以下错误：
```
ModuleNotFoundError: No module named 'pkg_resources'
ERROR: Failed to build 'openai-whisper' when getting requirements to build wheel
```

**原因**：pip 在安装 `openai-whisper` 时会创建一个临时的隔离构建环境，在这个临时环境中 `setuptools` 没有正确提供 `pkg_resources`。

**解决**：使用 `--no-build-isolation` 参数，让 pip 使用当前虚拟环境中的 setuptools 来构建：
```bash
pip install --no-build-isolation -r requirements.txt
```

### 10.2 编译报错：`esp_websocket_client.h` 找不到

**原因**：WebSocket 组件没有安装。

**解决**：
```bash
cd firmware_espidf
idf.py add-dependency "espressif/esp_websocket_client^1.2.3"
```

### 10.2 编译报错：`driver/gpio.h` 找不到

**原因**：CMakeLists.txt 中缺少 `driver` 依赖。

**解决**：检查 `firmware_espidf/main/CMakeLists.txt`，确保 `REQUIRES` 列表中有 `driver`。

### 10.3 ESP32 无法连接 WiFi

**排查步骤**：
1. 检查 `menuconfig` 中的 WiFi 名称和密码是否正确（注意大小写）
2. 确认 WiFi 是 **2.4GHz**（ESP32-S3 不支持 5GHz）
3. 检查路由器是否开启 WPA2 加密
4. 尝试把 ESP32 靠近路由器
5. 查看串口日志中的错误码

### 10.4 设备显示离线

**排查步骤**：
1. 检查云端服务器是否正在运行
2. 检查 `menuconfig` 中的服务器 IP 是否正确
3. 确保 ESP32 和服务器在同一个局域网
4. 检查防火墙是否拦截了 8080 端口
5. 测试服务器健康：`curl http://服务器IP:8080/api/health`

### 10.5 CSI 检测不到人体 / 误报率高

**排查步骤**：
1. 检查 CSI 是否启用（看串口日志）
2. 观察串口输出的方差值
3. 在前端进行 "空房间校准"
4. 调节阈值（增大减少误报，减小增加灵敏度）
5. 检查 WiFi 连接是否稳定

### 10.6 麦克风无声 / 扬声器无输出

**排查步骤**：
1. 检查 I2S 引脚接线是否正确
2. 确认麦克风供电是 **3.3V**（不是 5V！）
3. 确认功放供电是 **5V**
4. 检查串口日志中的 I2S 初始化信息
5. 用万用表测量各引脚电压

### 10.7 语音识别率低

**优化方法**：
1. 降低环境噪音
2. 说话距离麦克风 0.5-1 米
3. 使用更高质量的麦克风（INMP441 推荐）
4. 在安静环境使用
5. 检查 Whisper 模型大小（base 比 tiny 更准但更慢）

### 10.8 前端页面打不开

**排查步骤**：
1. 确认 `npm run dev` 正在运行
2. 检查浏览器地址是否正确（`http://localhost:3000`）
3. 检查是否有其他程序占用了 3000 端口
4. 尝试刷新页面或重启开发服务器

---

## 附录 A：项目文件结构

```
fairy_nest/
├── firmware_espidf/                   # ESP32-S3 固件（ESP-IDF v5.5.2）
│   ├── CMakeLists.txt                 # 项目配置
│   ├── sdkconfig.defaults             # 默认 SDK 配置
│   ├── partitions.csv                 # 分区表
│   └── main/                          # 主程序
│       ├── fairy_nest_main.c          # 主入口 + 任务创建
│       ├── fairy_nest.h               # 公共头文件
│       ├── wifi_csi.c                 # WiFi 连接 + CSI 人体检测
│       ├── i2s_audio.c                # I2S 麦克风和扬声器
│       ├── led_pwm.c                  # LED PWM 调光控制
│       ├── websocket_mgr.c            # WebSocket 云端通信
│       ├── voice_fsm.c                # 语音状态机
│       ├── alarm_sys.c                # 闹钟系统
│       └── config_mgr.c               # NVS 配置管理
│
├── cloud_server/                      # 云端服务器
│   ├── main.py                        # FastAPI 主程序
│   ├── config.py                      # 配置管理
│   ├── requirements.txt               # Python 依赖
│   ├── services/                      # 服务模块
│   │   ├── speech_recognition.py      # 语音识别 (Whisper)
│   │   ├── tts_service.py             # 语音合成 (Edge-TTS)
│   │   ├── llm_service.py             # LLM 服务 (DeepSeek/OpenAI)
│   │   └── audio_processor.py         # 音频处理
│   └── database/                      # 数据库
│       ├── models.py                  # 数据模型
│       └── db.py                      # 数据库连接
│
├── frontend/                          # 前端静态文件（旧版）
│   ├── index.html
│   └── app.js
│
├── app/                               # 前端源码（React + Vite 新版）
│   ├── src/
│   │   ├── App.tsx                    # 主界面
│   │   └── components/ui/             # UI 组件
│   ├── package.json
│   └── vite.config.ts
│
└── docs/                              # 文档
    └── FairyNest完整使用文档.md
```

## 附录 B：技术架构

```
+--------------------------------------------------+
|                   用户层                          |
|  +----------------+  +-------------------------+ |
|  | 前端管理界面    |  | 语音交互 (唤醒词 + 对话) | |
|  +-------+--------+  +------------+------------+ |
+----------|--------------|-----------+-------------+
           |              |
+----------v--------------v-----------+-------------+
|            云端服务器 (FastAPI)                    |
|  +-------------+  +-------------+  +-----------+ |
|  | WebSocket   |  | REST API    |  | 设备管理   | |
|  | 实时通信     |  | 闹钟/配置    |  | 状态监控   | |
|  +------+------+  +------+------+  +-----+-----+ |
|         |                |               |       |
|  +------v------+  +------v------+  +-----v-----+ |
|  | Whisper STT |  | LLM 服务    |  | Edge-TTS  | |
|  | 语音识别     |  | 语义理解    |  | 语音合成   | |
|  +-------------+  +-------------+  +-----------+ |
+-------------------|--------------|----------------+
                    |              |
+---------|---------v------|-------v--------|------+
|         |                |                |      |
|  +------v------+  +------v------+  +------v-----+|
|  | WiFi CSI    |  | I2S 麦克风   |  | I2S 功放   ||
|  | 人体检测     |  | 音频采集     |  | 音频播放   ||
|  +------+------+  +------+------+  +------+-----+|
|         |                |                |      |
|  +------v----------------------------------v-----+|
|  |          ESP32-S3 主控制器 (ESP-IDF v5.5.2)   ||
|  |  - 双核 240MHz CPU                           ||
|  |  - WiFi 802.11 b/g/n                         ||
|  |  - 512KB SRAM + 8MB PSRAM                    ||
|  +----------------------------------------------+|
|              硬件层 (小夜灯形态)                    |
+--------------------------------------------------+
```

## 附录 C：FreeRTOS 任务架构

| 任务名 | 核心 | 优先级 | 功能 |
|--------|------|--------|------|
| main_task | 0 | 5 | 系统心跳、状态上报 |
| wifi_csi | 0 | 3 | WiFi 连接、CSI 数据采集 |
| websocket | 0 | 4 | WebSocket 云端通信 |
| alarm_sys | 0 | 3 | 闹钟检测、智能唤醒 |
| i2s_rx | 1 | 4 | 麦克风音频采集、VAD |
| i2s_tx | 1 | 4 | 扬声器音频播放 |
| led_pwm | 1 | 2 | PWM 调光、渐变效果 |
| voice_fsm | 1 | 4 | 语音状态机 |

## 附录 D：支持的语音指令

| 指令 | 功能 |
|------|------|
| "开灯" / "打开灯" | 打开夜灯 |
| "关灯" / "关闭灯" | 关闭夜灯 |
| "调亮一点" | 增加亮度 |
| "调暗一点" | 降低亮度 |
| "设置闹钟 7 点半" | 设置闹钟 |
| "停止闹钟" | 停止当前闹钟 |
| "贪睡" / "再睡一会" | 贪睡 5 分钟 |

## 附录 E：更新日志

| 版本 | 日期 | 更新内容 |
|------|------|---------|
| v1.0.0 | 2025-1 | 初始版本, 集成 CSI + 语音 + 夜灯 |

## 附录 F：参考资源

- **ESP32-S3 技术规格**: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- **ESP-IDF 编程指南**: https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/index.html
- **ESP-SR 语音框架**: https://docs.espressif.com/projects/esp-sr/en/latest/
- **WiFi CSI 原理**: https://github.com/ruvnet/RuView/blob/main/docs/CSI_Primer.md
- **OpenAI Whisper**: https://github.com/openai/whisper
- **DeepSeek API**: https://platform.deepseek.com/docs

---

> **技术支持**: 如有问题，请在 GitHub Issues 提交或联系项目维护者。
>
> **开源协议**: MIT License
