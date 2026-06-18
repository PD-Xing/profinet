# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

PROFINET 设备从站（device/slave），作为 **PLC 与六轴机械臂运动控制器之间的协议转换桥梁**。
基于 RT-Labs 的 **p-net 协议栈 v1.0.2**，实现 PROFINET v2.43 Conformance Class B、Real Time Class 1。

### 整体系统架构

```
┌──────────┐  PROFINET   ┌──────────────┐  PLCFrame(80B)  ┌─────────────────────┐  EtherCAT  ┌──────────────┐
│   PLC    │◄───────────►│   pn_dev     │◄───────────────►│  robot_software_     │◄──────────►│  六轴机械臂   │
│(TIA Portal)│ 周期帧 1ms  │  (本仓库)     │  TCP 31000/     │  control (ROS2)      │  CIA402    │  + 工具IO    │
│          │             │              │  共享内存        │  MoveIt2 + EtherCAT │            │              │
└──────────┘             └──────────────┘                 └─────────────────────┘            └──────────────┘
```

- **pn_dev**（本仓库）：接收 PLC 的 PROFINET 周期数据 → 转换为 PLCFrame → 发给运动控制器；反向将机器人状态打包回传 PLC
- **robot_software_control**（`/home/jiaoso/robot_software_control/`）：ROS2 + MoveIt2 + EtherCAT 运动控制器，控制六轴机械臂

目标平台：**OK-3576-C 开发板**（Rockchip RK3576，ARM aarch64，Linux 6.1.118 RT 内核）。
两个组件（pn_dev 和 robot_software_control）都部署在同一块 OK3576 开发板上，通过 **TCP localhost** 或 **POSIX 共享内存** 通信。

许可证：p-net 栈为 GPLv3 / 商业双许可，OSAL 为 BSD 3-Clause。

---

## 数据流与集成架构

### 完整数据流

```
PLC (TIA Portal)
  │ PROFINET 周期帧（~1ms）
  │ EtherType: 0x8892 (PROFINET RT)
  ▼
p-net 协议栈 (pf_cpm.c → pf_eth.c)
  │ 内部缓冲，验证 Frame ID / Cycle Counter
  ▼
pnet_output_get_data_and_iops()     ← sampleapp_common.c:1015
  │ 取出 PLC 发来的原始字节
  ▼
app_cyclic_data_callback()          ← sampleapp_common.c:991
  │ 对每个 plugged subslot 调用
  ▼
app_data_set_output_data()          ← app_data.c:197  ★ 核心集成点
  │
  │  PLC → Robot 方向：
  │    1. 将 PROFINET 数据解析组装为 PLCFrame (80字节)
  │    2. 写入共享内存 /dev/shm/pnet-plc-to-robot
  │       (或通过 TCP localhost:31000 发送)
  │
  │  Robot → PLC 方向：
  │    1. 读取共享内存 /dev/shm/pnet-robot-to-plc
  │       (或通过 TCP localhost:31000 响应)
  │    2. 解包 PLCFrame → 填充 PROFINET 输入数据
  │
  ▼
app_data_get_input_data()           ← app_data.c:117  ★ 核心集成点
  │ 返回机器人状态给 p-net 栈
  ▼
pnet_input_set_data_and_iops()     ← sampleapp_common.c:1069
  │ 将输入数据放入周期帧
  ▼
p-net 协议栈 (pf_ppm.c) → PROFINET 帧 → PLC
```

### 与运动控制器的通信接口

运动控制器（`robot_software_control`）已有的 PLC 通信接口：

**TCP Server 端口 31000**（`PLCCommunicator`，`plc_communicator.cpp`）：
- PLC 连接到此端口，发送 80 字节 PLCFrame
- Robot 收到后立即回复 80 字节 PLCFrame（当前状态）
- 然后触发 `handlePLCData()` 回调处理 PLC 命令

**PLCFrame 协议（80 字节）**（`plc_communicator.h`）：

```c
typedef struct {
    uint8_t  bool_data[32];  // 256 个 bool 信号: DATA_BOOL[0]~DATA_BOOL[255]
    int16_t  int_data[8];    // 8 个 16位有符号整数: DATA_INT[0]~DATA_INT[7]
    float    real_data[8];   // 8 个 32位浮点数: DATA_REAL[0]~DATA_REAL[7]
} PLCFrame;  // 32 + 16 + 32 = 80 字节
```

**PLC → Robot 信号映射**（`handlePLCData()` 中处理）：

| 信号 | 含义 |
|------|------|
| `DATA_BOOL[0]` 上升沿 | 启动 / 上电（调用 gpio_safety_start） |
| `DATA_BOOL[4]` 上升沿 | 紧急停止（调用 triggerStop） |
| `DATA_REAL[0]~DATA_REAL[3]` | 视觉引导偏移量（存入 plc_vision_offset_[4]） |

**Robot → PLC 信号映射**（`buildRobotToPLCFrame()` 中构建）：

| 信号 | 含义 |
|------|------|
| `DATA_BOOL[0]` | GPIO 电源状态 |
| `DATA_BOOL[1]` | 告警状态 |
| `DATA_BOOL[2]` | 已使能（Running） |
| `DATA_BOOL[6]` | Robot_Busy（轨迹执行中） |
| `DATA_BOOL[7]` | Robot_Moving（关节速度 > 阈值） |
| `DATA_BOOL[15]` | Action Busy（动作执行中） |
| `DATA_BOOL[16]` | Action Done（动作完成） |
| `DATA_BOOL[17/18]` | 左/右夹爪状态（AI0/AI1 > 阈值） |
| `DATA_BOOL[32/33]` | 左/右夹爪有料（工具 DI 位） |
| `DATA_BOOL[34~37]` | 板级 DI 传感器位 |

### pn_dev 与 robot_software_control 的通信方式选择

两个进程都运行在同一块 OK3576 开发板上，推荐以下两种方式之一：

**方案 A：TCP localhost（复用已有接口）**

pn_dev 作为 TCP 客户端连接 `127.0.0.1:31000`，按已有 PLCFrame 协议收发 80 字节帧。
- 优点：复用 robot_software_control 已有的 PLCCommunicator，无需修改运动控制器代码
- 缺点：TCP 延迟 ~50-200μs

**方案 B：POSIX 共享内存（更低延迟）**

复用 `pn_dev_lan9662/app_shm.c` 的实现，创建两个共享内存区域：
- `/dev/shm/pnet-plc-to-robot`（80 字节）— PLC→Robot
- `/dev/shm/pnet-robot-to-plc`（80 字节）— Robot→PLC

加上命名信号量做读写同步。
- 优点：延迟 < 1μs，适合 1ms PROFINET 周期
- 缺点：需要在 robot_software_control 侧新增共享内存读写线程

### PROFINET I/O 模块大小的确定

PROFINET 周期数据需要能承载 PLCFrame 的 80 字节。
当前 GSDML 的 DIO 8xLogicLevel 只有 1 字节输入 + 1 字节输出，需要扩展：

- **Output 模块（PLC→Robot）**：至少 80 字节（PLCFrame）
- **Input 模块（Robot→PLC）**：至少 80 字节（PLCFrame）

建议在 GSDML 中添加一个 80+ 字节的自定义 I/O 模块，或在现有模块基础上增加多个 Echo 模块拼接。

---

## 0. 运动控制器参考架构（robot_software_control）

运动控制器代码位于 `/home/jiaoso/robot_software_control/`，以下是与本项目集成相关的关键信息：

### 关键模块

| 模块 | 路径 | 用途 |
|------|------|------|
| `robot_bringup` | `src/robot_bringup/` | 主应用节点，含 PLCCommunicator、命令处理、TCP 服务器 |
| `robot_msgs` | `src/robot_msgs/` | 自定义 ROS2 消息和服务定义 |
| `robot_controllers` | `src/robot_controllers/` | ros2_control 控制器（工具 IO、板级 IO、基板、动力学） |
| `ethercat_driver_ros2` | `src/ethercat_driver_ros2/` | EtherCAT 主站驱动（控制 11 个从站） |
| `robot_moveit_config` | `src/robot_moveit_config/` | MoveIt2 运动规划配置 |

### EtherCAT 从站拓扑

```
Slave 0:  ZPT-8080      — EtherCAT 耦合器（无 PDO）
Slave 1:  ZDM-E0016P    — 16 路数字输出
Slave 2:  ZDM-E1600     — 16 路数字输入
Slave 3:  Base Board    — IMU 姿态传感器
Slave 4~9: J1~J6 电机   — CIA402 伺服驱动
Slave 10: Tool GPIO     — 末端工具 IO 板
```

### 运动控制命令方式

运动控制器支持以下命令接口（pn_dev 可通过 TCP JSON 或 ROS2 Service 调用）：

| 命令 | 说明 |
|------|------|
| `MovJ` | 关节空间 PTP 运动（6 个关节角 + 速度/加速度） |
| `MovL` | 笛卡尔直线运动 |
| `MovP` | 笛卡尔 PTP 运动 |
| `MovC` | 圆弧运动 |
| `MovJIO` | 关节运动 + 工具动作 |
| `EnableRobot` | 使能（TODO 桩） |
| `DisableRobot` | 去使能（TODO 桩） |
| `EmergencyStop` | 紧急停止 |
| `ClearError` | 清除错误（TODO 桩） |
| `GetAngle` | 读取当前关节角度 |
| `GetPose` | 读取当前 TCP 位姿 |
| `GetRobotMode` | 读取机器人状态（INIT/BRAKE_ON/DISABLED/ENABLED/RUNNING/ALARM/PAUSE） |
| `DO`/`DI`/`AO`/`AI` | 读写板级 I/O |

### 机器人状态（RealTimeData 结构体，1496 字节）

关键反馈字段（`command.h`）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `q_actual[6]` | double×6 | 实际关节角度（rad） |
| `qd_actual[6]` | double×6 | 实际关节速度（rad/s） |
| `m_actual[6]` | double×6 | 实际关节扭矩 |
| `tool_vector_actual[6]` | double×6 | TCP 位姿 [x,y,z,rx,ry,rz]（m, rad） |
| `robot_mode` | uint64 | 0=INIT,1=BRAKE_ON,2=DISABLED,3=ENABLED,8=RUNNING,9=ALARM,10=PAUSE |
| `digital_input_bits` | uint64 | 板级 DI 位掩码 |
| `digital_outputs` | uint64 | 板级 DO 位掩码 |
| `motor_temperatures[6]` | double×6 | 电机温度 |
| `joint_current[6]` | double×6 | 关节电流（A） |

### 启动流程

1. `robot_bringup_ros2` 节点启动（GPIO 初始化、加载 param.json、初始化服务）
2. 等待 5s → 检查 11 个 EtherCAT 从站全部进入 PREOP+（最长等 90s）
3. 依次激活控制器：JointState → ToolGPIO → BoardIO → BaseBoard → ArmController
4. 启动 MoveIt2 move_group
5. PLCCommunicator 在端口 31000 开始监听

### 与本项目的关键差异

- `EnableRobot` / `DisableRobot` / `ClearError` — **均为桩函数（TODO）**，不实际控制 EtherCAT CIA402 状态机
- `disableMotors()` / `enableMotors()` — **也是桩函数（TODO）**
- 三种速度系统（speed_scaling / velocityRatio / MoveIt velocity_scaling）未统一，仅 `SetVelocityScalingFactor` 实际影响运动规划

---

## 开发板集成部署方案

两个程序都部署在 OK3576 开发板的 `/home/default/` 下。

### 开发板目录布局

```
/home/default/
├── start_all.sh                          # ★ 总启动脚本（一键启动全部）
├── profinet/
│   ├── pn_dev                            # PROFINET 从站程序
│   ├── GSDML-V2.43-*.xml                 # 设备描述文件
│   └── kill_pn_dev.sh                    # 进程清理脚本
├── robot_software_control/
│   └── install/                          # ROS2 workspace（交叉编译产物）
│       ├── local_setup.bash
│       └── robot_bringup/
│           └── lib/robot_bringup/
│               └── robot_bringup_node    # 运动控制器主程序
└── config/
    └── param.json                        # 运动控制器配置文件
```

### 启动顺序

运动控制器的启动流程（`robot_complete.launch.py`）：

```
0s    robot_bringup_node 启动
        ├─ 加载 param.json（PLC 端口 31000 配置）
        ├─ GPIO 安全初始化
        ├─ 初始化 ROS2 服务和 MoveIt2 接口
        ├─ 启动示教器 TCP（30005/30006）
        ├─ 启动 PLCCommunicator（★ 端口 31000 开始监听）
        └─ 启动紧急停止服务（30007）

5s    开始检查 EtherCAT 从站
        └─ 等待 11 个从站进入 PREOP+ 状态（最长 90s）

~30s  EtherCAT 从站就绪
        ├─ 启动 ros2_control_node + robot_state_publisher
        ├─ 依次激活控制器：
        │   20s: joint_state_broadcaster
        │   22s: tool_gpio + base_board + board_io
        │   24s: motor_state + effort_controller
        │   27s: arm_controller（★ 此时可执行运动）
        └─ 启动 move_group (MoveIt2)
```

**推荐启动方式**：pn_dev 自带 TCP 重连，和运动控制器同时启动即可：

```bash
#!/bin/bash
# start_all.sh — 部署在 /home/default/start_all.sh

echo "=== 启动运动控制器 ==="
source /opt/ros/humble/setup.bash
source /home/default/robot_software_control/install/local_setup.bash
export ROBOT_TYPE=w5_c
ros2 launch robot_moveit_config robot_complete.launch.py &

echo "=== 等待 PLC 通信端口就绪 ==="
for i in $(seq 1 60); do
    if ss -tln | grep -q 31000; then
        echo "PLCCommunicator 端口 31000 已就绪"
        break
    fi
    sleep 1
done

echo "=== 启动 PROFINET 从站 ==="
cd /home/default/profinet
sudo ./kill_pn_dev.sh 2>/dev/null
sudo ./pn_dev -i eth0 -s rt-labs-dev -p /tmp/pnet &

echo "=== 系统启动完成 ==="
```

### pn_dev 需要新增的代码：TCP 桥接模块

在 `pn_dev/app_data.c` 中新增一个 TCP 客户端，连接 `127.0.0.1:31000`：

```c
// 新增：TCP 连接到运动控制器的 PLCCommunicator
static int plc_sock_fd = -1;
static PLCFrame latest_robot_status;   // 缓存最新机器人状态
static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;

// 后台线程：维持 TCP 连接，接收机器人状态
static void * plc_tcp_thread(void * arg)
{
    while (1) {
        plc_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(31000),
            .sin_addr.s_addr = inet_addr("127.0.0.1")
        };
        if (connect(plc_sock_fd, &addr, sizeof(addr)) == 0) {
            APP_LOG_INFO("已连接运动控制器 PLCCommunicator\n");
            // 连接后持续接收状态帧
            while (1) {
                PLCFrame rx;
                ssize_t n = recv(plc_sock_fd, &rx, sizeof(PLCFrame), MSG_WAITALL);
                if (n != sizeof(PLCFrame)) break;
                pthread_mutex_lock(&status_mutex);
                latest_robot_status = rx;
                pthread_mutex_unlock(&status_mutex);
            }
        }
        close(plc_sock_fd);
        plc_sock_fd = -1;
        sleep(1);  // 1 秒后重试
    }
    return NULL;
}
```

### 修改 app_data_set_output_data() — PLC 命令 → 运动控制器

```c
int app_data_set_output_data(uint16_t slot_nbr, uint16_t subslot_nbr,
                              uint32_t submodule_id, uint8_t *data, uint16_t size)
{
    // 将 PROFINET 数据直接作为 PLCFrame 发送给运动控制器
    PLCFrame tx;
    memset(&tx, 0, sizeof(tx));
    memcpy(&tx, data, (size < sizeof(PLCFrame)) ? size : sizeof(PLCFrame));

    if (plc_sock_fd > 0) {
        send(plc_sock_fd, &tx, sizeof(PLCFrame), MSG_NOSIGNAL);
    }
    return 0;
}
```

### 修改 app_data_get_input_data() — 机器人状态 → PLC

```c
uint8_t * app_data_get_input_data(uint16_t slot_nbr, uint16_t subslot_nbr,
                                   uint32_t submodule_id, bool button_pressed,
                                   uint16_t *size, uint8_t *iops)
{
    static uint8_t status_buffer[sizeof(PLCFrame)];
    
    pthread_mutex_lock(&status_mutex);
    memcpy(status_buffer, &latest_robot_status, sizeof(PLCFrame));
    pthread_mutex_unlock(&status_mutex);

    if (plc_sock_fd > 0) {
        *iops = PNET_IOXS_GOOD;
    } else {
        *iops = PNET_IOXS_BAD;   // 运动控制器未连接
    }
    *size = sizeof(PLCFrame);
    return status_buffer;
}
```

### 时序说明

```
运动控制器                         pn_dev                        PLC
─────────                         ─────                        ───
31000 监听开始                     TCP 连接 → 成功
                                  PROFINET 初始化               
                                  ← 等待 PLC 连接 ──────────── DCP 发现
                                  ← 建立 AR ────────────────── CONNECT
                                  ← PrmEnd ─────────────────── 参数化完成
                                  ← 周期数据(1ms) ──────────── IO 数据
PLCFrame 响应 ──────→ recv() ←──  send(PLCFrame) ←── app_data_set_output_data()
                   ←── send(状态) ──→ recv(状态) ──→ app_data_get_input_data()
                                                             → PROFINET 周期帧 → PLC
```

### 需要修改的配置

**GSDML 模块扩展**：当前 DIO 8xLogicLevel 模块只有 1 字节，需要扩展为至少 80 字节的模块承载 PLCFrame。

**param.json**：确认 PLC 端口配置正确：
```json
"plc_control": {
    "listen_ip": "127.0.0.1",    // 只监听本地（pn_dev 在 localhost 连接）
    "port": 31000
}
```

### 部署命令

```bash
# 交叉编译 pn_dev（在 x86 主机上）
cd ~/profinet/p-net-1.0.2-samples
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake
make -j$(nproc)

# 部署 pn_dev 到开发板
sshpass -p 'default' scp build/pn_dev/pn_dev default@192.168.1.100:/home/default/profinet/
sshpass -p 'default' scp ../pn_dev/GSDML*.xml default@192.168.1.100:/home/default/profinet/
sshpass -p 'default' scp ../kill_pn_dev.sh default@192.168.1.100:/home/default/profinet/

# 在开发板上启动全部
sshpass -p 'default' ssh default@192.168.1.100
cd /home/default
./start_all.sh
```

---

## 1. 工程目录结构

```
~/profinet/
├── 使用说明.md                              # 完整中文使用指南（274行，含编译、部署、PLC配置）
├── p-net-public-v1.0.2.tar.gz              # p-net 协议栈源码压缩包
├── p-net-1.0.2-samples.zip                # 示例应用源码压缩包
├── p-net-1.0.2-Linux-aarch64.zip          # 预编译 aarch64 SDK 压缩包
├── osal-master.zip                         # OSAL 源码压缩包
│
├── cmake-3.28.0-linux-x86_64/              # 自包含 CMake 3.28.0（x86_64 二进制发行版）
│   └── bin/cmake                            # 需显式使用此版本（≥3.28），非系统 CMake
│
├── toolchain/                              # 交叉编译工具链
│   └── gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/
│       ├── bin/aarch64-none-linux-gnu-*     # GCC 10.3.1, Binutils, GDB
│       ├── aarch64-none-linux-gnu/libc/     # glibc 2.33 sysroot
│       └── aarch64-none-linux-gnu/lib64/    # 目标运行时库（libstdc++, libgcc_s 等）
│
├── p-net-public-v1.0.2/                    # p-net 协议栈源码（见 §2 详细模块说明）
│   ├── src/common/                          # 通用协议模块（16 个模块）
│   ├── src/device/                          # 设备端协议机（22 个模块）
│   ├── src/drivers/lan9662/                # LAN9662 硬件卸载驱动参考实现
│   ├── include/                             # 公共 API 头文件（pnet_api.h, pnal.h）
│   ├── test/                                # Google Test 单元测试（33 个文件）
│   └── tools/                               # 辅助脚本（Wireshark pcap 过滤器）
│
├── osal/                                   # OS 抽象层源码（见 §3 详细说明）
│   ├── src/linux/                           # Linux pthread 后端 ← 本项目使用
│   ├── src/rt-kernel/                       # RT-Kernel 后端
│   ├── src/windows/                         # Win32 后端
│   ├── src/freertos/                        # FreeRTOS 后端（含 STM32F769 / iMX8MM BSP）
│   ├── include/                             # 公共 API（osal.h, osal_log.h）
│   └── test/                                # Google Test 单元测试
│
├── p-net-1.0.2-samples/                    # 示例应用 ← 本项目主工作区
│   ├── pn_dev/                              # 从站程序（见 §4 源文件清单）
│   ├── ports/linux/sampleapp_main.c         # Linux 平台入口
│   ├── cmake/                               # 平台 CMake 配置（5 个平台文件）
│   ├── pn_shm_tool/                         # 共享内存调试工具
│   ├── pn_dev_lan9662/                      # LAN9662 交换机变体
│   ├── build/                               # 已完成的 aarch64 交叉编译产物
│   └── build-aarch64/                       # 另一次构建（使用不同工具链路径）
│
└── p-net-1.0.2-Linux-aarch64/              # 预编译 SDK（供 samples 链接）
    ├── lib/libpnet.a                        # 5.3 MB 静态库
    ├── lib/libosal.a                        # 65 KB 静态库
    ├── include/                             # 公共头文件
    ├── bin/pn_dev                           # 预编译可执行文件
    ├── cmake/                               # CMake 包配置（PNetConfig.cmake, OsalConfig.cmake）
    └── share/profinet/                      # 辅助脚本（set_network_parameters, set_profinet_leds）
```

---

## 2. p-net 协议栈模块详解

### 2.1 Common 层（`src/common/`）—— 角色无关的协议实现

| 文件 | 用途 |
|------|------|
| `pf_ppm.c` | **Provider Protocol Machine**：周期发送输入数据（从站→PLC），管理发送缓冲区、Cycle Counter、数据状态位 |
| `pf_ppm_driver_sw.c` | PPM 的默认软件驱动：通过内部 Scheduler 定时发送 Ethernet 帧 |
| `pf_cpm.c` | **Consumer Protocol Machine**：周期接收输出数据（PLC→从站），验证帧/源/Cycle Counter，管理 Data Hold Timer |
| `pf_cpm_driver_sw.c` | CPM 的默认软件驱动：注册 Ethernet 帧处理器，缓冲最新接收数据 |
| `pf_dcp.c` | **Discovery and Configuration Protocol**：GET/SET/IDENTIFY/HELLO，处理站名、IP 参数、设备属性查询，管理 SAM（Source Address Match）3 秒保持 |
| `pf_alarm.c` | **Alarm 处理**：APMS/APMR/ALPMI/ALPMR 协议机，高/低优先级告警队列，TACK 重传（可配置超时和重试次数） |
| `pf_eth.c` | **Ethernet 组件**：初始化管理口和物理口，Frame ID Map 路由（VLAN 标签跳过），LLDP/IP 报文分发 |
| `pf_lldp.c` | **Link Layer Discovery Protocol**：构建/解析 LLDP 帧，Peer 超时看门狗，别名生成与匹配 |
| `pf_scheduler.c` | **内部调度器**：基于链表优先级队列的超时/回调调度，与栈周期对齐的延迟规整 |
| `pf_file.c` | **文件持久化**：带 Magic（"PNET"）和版本号的二进制文件加载/保存，支持 `save_if_modified` 减少闪存磨损 |
| `pf_udp.c` | **UDP 封装**：对 PNAL UDP 函数的薄封装 |
| `pf_snmp.c` | **SNMP 辅助**：提供 SNMP Agent 所需的 getter/setter，数据持久化 |
| `pf_ptcp.c` | **PTCP 桩**：空实现占位 |
| `pf_bg_worker.c` | **后台工作线程**：异步处理端口状态更新、NVM 保存、文件清理 |

### 2.2 Device 层（`src/device/`）—— 设备端协议机

| 文件 | 用途 |
|------|------|
| `pnet_api.c` | **公共 API 实现**：所有 `pnet_*()` 函数的实现，是应用层唯一入口 |
| `pf_cmdev.c` | **CMDEV**：AR（Application Relation）生命周期管理——CONNECT/RELEASE/ABORT/DControl |
| `pf_cmsu.c` | **CMSU**：启动序列状态机，协调各协议机（CPM/PPM/ALPMI/ALPMR/CMDEV）间的错误信号 |
| `pf_cmsm.c` | **CMSM**：连接监控（看门狗），基于 Data Hold Factor 的周期性检查 |
| `pf_cmio.c` | **CMIO**：I/O 数据对象映射，将 Slot/Subslot 映射到周期数据帧的偏移位置 |
| `pf_cmina.c` | **CMINA**：IP 地址、站名、MAC 地址管理，处理 DCP 的 GET/SET |
| `pf_cmrpc.c` | **CMRPC**：RPC 设备协议机（UDP），处理 RPC Bind/Lookup、分片请求/响应 |
| `pf_cmrpc_epm.c` | **RPC Endpoint Mapper**：DCE/RPC EPM 服务 |
| `pf_cmrpc_helpers.c` | **RPC 辅助**：NDR 格式编解码工具 |
| `pf_cmrdr.c` | **CMRDR**：处理控制器的 Read Record 请求 |
| `pf_cmwrr.c` | **CMWRR**：处理控制器的 Write Record 请求，验证数据并写入 Subslot |
| `pf_cmpbe.c` | **CMPBE**：Parameter Begin/End 协议机，确保参数化在 Begin/End 边界内 |
| `pf_cmrs.c` | **CMRS** 桩：Read Responder 最小实现 |
| `pf_cmdmc.c` | **CMDMC**：多播通信（最小实现） |
| `pf_fspm.c` | **FSPM**：设备配置中心存储——产品名、I&M 数据、端口配置、日志簿、Signal LED 指示 |
| `pf_port.c` | **端口管理**：端口迭代、状态查询、MAC 地址查找 |
| `pf_pdport.c` | **PDPort 数据管理**：每端口 MAU 类型、Peer 信息、信号延迟，数据持久化 |
| `pf_plugsm.c` | **Plug State Machine**：模块/子模块的插拔管理，Ident Number 验证，所有权管理 |
| `pf_diag.c` | **诊断实现**：Channel-based 和 USI-based 诊断，严重度和维护状态计算 |
| `pf_block_reader.c` | **缓冲区解析**：`pf_get_uint8/16/32()`、`pf_get_block_header()` 等 |
| `pf_block_writer.c` | **缓冲区构造**：`pf_put_uint16/32()`、`pf_put_alarm_block()` 等 |

### 2.3 内部类型定义

- **`src/pf_types.h`**（3107 行）—— 整个栈的内部数据结构：`pnet_t` 主实例、AR/IOCR/Subslot/Port/定时器结构体、所有协议机状态
- **`src/pf_includes.h`** —— 主 include 文件，按顺序包含所有模块头文件
- **`src/pf_driver.h`** —— 硬件卸载驱动接口（`pf_driver_init/ppm_init/cpm_init`），仅在 `PNET_OPTION_DRIVER_ENABLE=1` 时使用

### 2.4 编译时配置选项（`CMakeLists.txt` cache variables）

**资源上限**（所有默认值）：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `PNET_MAX_AR` | 16 | 最大连接数 |
| `PNET_MAX_API` | 1 | 最大 Application Process 数 |
| `PNET_MAX_CR` | 2 | 每个 AR 的最大 CR（1 Input + 1 Output） |
| `PNET_MAX_SLOTS` | 4 | 每个 API 的最大槽位数（≥2，含 DAP） |
| `PNET_MAX_SUBSLOTS` | 4 | 每个 Slot 的最大子槽位数（DAP 需要 3） |
| `PNET_MAX_PHYSICAL_PORTS` | 1 | 最大物理端口数 |
| `PNET_MAX_LOG_BOOK_ENTRIES` | 10 | 最大日志条目 |
| `PNET_MAX_ALARMS` | 2 | 每个 AR 每个队列的最大告警数 |
| `PNET_MAX_ALARM_PAYLOAD_DATA_SIZE` | 1460 | 告警负载最大字节 |
| `PNET_MAX_DIAG_ITEMS` | 100 | 设备总诊断条目数 |
| `PNET_MAX_DIAG_MANUF_DATA_SIZE` | 50 | 制造商诊断数据最大字节 |
| `PNET_MAX_SESSION_BUFFER_SIZE` | 1500 | 分片 RPC 请求/响应最大长度 |
| `PNET_MAX_DIRECTORYPATH_SIZE` | 256 | 目录路径最大长度 |
| `PNET_MAX_FILENAME_SIZE` | 256 | 文件名最大长度 |
| `PNET_MAX_PORT_DESCRIPTION_SIZE` | 256 | 端口描述最大长度 |

**功能开关**（0=禁用，1=启用）：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `PNET_OPTION_SNMP` | **1** | SNMP 支持（Conformance Class B 必需） |
| `PNET_OPTION_DRIVER_ENABLE` | 0 | 硬件卸载驱动（LAN9662 用） |
| `PNET_OPTION_FAST_STARTUP` | 0 | 快速启动 |
| `PNET_OPTION_PARAMETER_SERVER` | 0 | iPar 参数服务器 |
| `PNET_OPTION_IR` | 0 | 等时实时 |
| `PNET_OPTION_SR` | 0 | 调度实时 |
| `PNET_OPTION_REDUNDANCY` | 0 | 冗余/MRP |
| `PNET_OPTION_AR_VENDOR_BLOCKS` | 0 | AR 厂商自定义块 |
| `PNET_OPTION_RS` | 0 | Resource Server |
| `PNET_OPTION_MC_CR` | 0 | 多播 CR |
| `PNET_OPTION_SRL` | 0 | SRL 子集 |
| `PNET_USE_ATOMICS` | 1 | 使用原子操作（GCC `__atomic_*`） |

**日志控制**（默认全 OFF，`LOG_LEVEL` 默认 INFO）：
`PF_ETH_LOG`、`PF_LLDP_LOG`、`PF_SNMP_LOG`、`PF_CPM_LOG`、`PF_PPM_LOG`、`PF_DCP_LOG`、`PF_RPC_LOG`、`PF_ALARM_LOG`、`PF_AL_BUF_LOG`、`PF_PNAL_LOG`、`PNET_LOG`

---

## 3. OSAL 模块详解

### 3.1 功能

OSAL 提供 OS 抽象，使 p-net 栈能在多平台运行：

| 功能 | Linux 实现 | 备注 |
|------|-----------|------|
| 线程 | `pthread_create` + `SCHED_FIFO` | 支持 FIFO 实时调度 |
| 互斥锁 | `pthread_mutex_t`（PRIO_INHERIT + RECURSIVE） | 优先级继承防反转 |
| 信号量 | `condvar + mutex + 计数` | 带超时支持 |
| 事件（标志组） | `condvar + mutex + uint32_t flags` | 位掩码等待 |
| 邮箱（消息队列） | `condvar + mutex + ring buffer` | 固定容量 |
| 定时器 | Linux `timer_create()` + `SIGEV_THREAD_ID` + `SIGALRM` | 使用 `syscall(SYS_gettid)` |
| 时间 | `clock_gettime(CLOCK_MONOTONIC)` | 微秒精度 |
| 日志 | 时间戳 + 级别标签 + `printf` 到 stdout | `LOG_LEVEL` 编译时过滤 |

### 3.2 编译器抽象（`include/sys/osal_cc.h` → `osal_cc_gcc.h`）

提供 GCC/Clang 属性宏：`CC_PACKED`、`CC_ALIGNED(n)`、字节序转换（`CC_TO_BE16/32/64`、`CC_FROM_BE16/32/64`）、原子操作（`CC_ATOMIC_GET/SET`）、`CC_STATIC_ASSERT`（`_Static_assert`）。

### 3.3 其他支持的后端（本项目未使用，但代码中存在）

- **rt-kernel**：委托给 rt-kernel 原生 API（`task_spawn`、`mtx_*`、`sem_*`、`flags_*`、`mbox_*`、`tmr_*`）
- **Windows**：Win32 API（`CreateThread`、`CreateMutex`、条件变量+临界区、`timeSetEvent`）
- **FreeRTOS + lwIP**：含 STM32F769I-DISCO 和 NXP i.MX8MM 的完整 BSP

---

## 4. 示例应用源文件详解（`p-net-1.0.2-samples/pn_dev/`）

### 4.1 每个文件的责任

| 文件 | 用途 |
|------|------|
| `sampleapp_common.c/h` | **核心应用逻辑**：`app_data_t` 状态机、所有 p-net 回调（`app_connect_ind`、`app_release_ind`、`app_state_ind`、`app_alarm_ind` 等）、DAP 插拔流程、周期数据处理、告警/诊断/日志簿 Demo（通过 Button2 切换）、主事件循环（`app_loop_forever`，使用事件标志驱动） |
| `app_utils.c/h` | **工具函数**：AR 管理（增/删/遍历/事件操作）、Slot/Subslot 插拔管理、周期数据轮询（`app_utils_cyclic_data_poll`）、`pnet_cfg_t` 默认配置初始化、网络接口解析（逗号分隔字符串）、IP/MAC/IOXS/错误码的字符串转换 |
| `app_gsdml.c/h` | **GSDML 模块目录**：静态定义所有可用模块和子模块（DAP、DI 8x1byte、DO 8x1byte、DIO 8x1byte、Echo Module）及其 Ident Number、数据方向、大小、参数；提供查找函数供栈调用 |
| `app_data.c/h` | **I/O 数据处理**：`app_data_get_input_data()`（从站→PLC）、`app_data_set_output_data()`（PLC→从站）、`app_data_write/read_parameter()`、`app_data_set_default_outputs()`、**本项目新增**`app_data_init()` 初始化 `/dev/ttyFIQ0` 串口调试输出 |
| `app_log.c/h` | **应用日志**：运行时可调级别的 printf 日志（DEBUG/INFO/WARNING/ERROR/FATAL），默认 FATAL；字节数组 hexdump 函数 |
| `GSDML-V2.43-*.xml` | **设备描述文件**：PROFINET V2.43，VendorID=0x0493(RT-Labs)，DeviceID=0x0002，ConformanceClass B，Slot 0=DAP + Slot 1-4 用户槽位 |
| `GSDML-0493-0002-RT-LABS-STACK.bmp` | 栈图标位图（47KB） |

### 4.2 GSDML 模块/子模块目录

| 模块 | Ident Number | 子模块 | Ident Number | 数据大小 | 方向 | 参数 |
|------|-------------|--------|-------------|---------|------|------|
| DAP | 0x00000001 | Interface + Port | — | — | — | — |
| DI 8xLogicLevel | IDM_30 (0x30) | Digital Input 1x8 | 0x00000020 | 1 字节 | Input | — |
| DO 8xLogicLevel | IDM_31 (0x31) | Digital Output 1x8 | 0x00000021 | 1 字节 | Output | — |
| DIO 8xLogicLevel | **IDM_32** (0x32) | Digital I/O 1x8 | 0x00000022 | 1字节输入+1字节输出 | Input+Output | Index 123, 124 (RecordData) |
| Echo Module | IDM_40 (0x40) | Echo 1x8 | 0x00000030 | 8字节输入+8字节输出 | Input+Output | Index 125 (Gain) |

### 4.3 本项目相对于官方示例的修改

1. **`pn_dev/app_data.c`** — 添加了 `/dev/ttyFIQ0` 串口调试输出（115200 8N1），**仅在 I/O 数据变化时**打印 `I1.0~I1.7` 和 `Q1.0~Q1.7` 状态；`I1.0` 跟随 `Q1.0`（echo），`I1.1~I1.6` 恒为 0，`I1.7` 跟随按钮状态
2. **`pn_dev/app_data.h`** — 添加了 `app_data_init()` 函数声明
3. **`ports/linux/sampleapp_main.c`** — 在初始化阶段调用 `app_data_init()`；默认设备名 `rt-labs-dev`，默认存储路径 `/tmp/pnet`

### 4.4 pn_dev vs pn_dev_lan9662 差异

| 方面 | pn_dev（本项目） | pn_dev_lan9662 |
|------|-----------------|---------------|
| 目标硬件 | 通用 Linux + Ethernet | Microchip LAN9662 交换机 + RTE FPGA |
| HW 卸载 | 无 | 3 种模式：`none`(共享内存)、`cpu`(RTE+SRAM)、`full`(RTE+QSPI/FPGA) |
| I/O 传输 | 文件读按钮 + 脚本设 LED | POSIX 共享内存（`/dev/shm/`） + FPGA MMIO |
| 链接库 | pnet + osal + pthread + rt | pnet + **mera**（LAN9662 RTE 驱动）+ pthread + rt |
| 槽位 | 1 DAP + 1-4 用户槽（可插拔） | 1 DAP + 12 固定槽（固定模块） |
| Device ID | 0x0002 | 0x9662 |
| 默认站名 | rt-labs-dev | lan9662-dev |
| ConformanceClass | B | A |
| 端口 | 1 | 2（始终启用） |
| AR 管理 | 数组 `ar[PNET_MAX_AR]` | 单 AR |
| Demo | 完整告警/诊断/日志簿 Demo | 简化，无 Button2 Demo |
| 应用就绪 | 立即 | 延迟 256 ticks（等待 RTE 启动） |

### 4.5 共享内存工具（`pn_shm_tool/`）

架构：POSIX 命名共享内存（`/dev/shm/pnet-{in,out}-<slot>-<subslot>-<name>`）+ 命名信号量同步。

| 文件 | 用途 |
|------|------|
| `pn_shm_tool.c` | CLI：`-r` 读、`-w` 写、`-c` 创建、`-b` 读特定位 |
| `shm_button_handler.sh` | 读取 GPIO 39，写入共享内存输入 |
| `shm_LED_handler.sh` | 读取共享内存输出 bit7，写入 LED sysfs |
| `shm_read_all.sh` | hexdump 所有 10 个共享内存区域 |
| `shm_write_all_inputs.sh` | 向所有 5 个输入区写入用户指定字节 |
| `shm_echo_all.sh` | 将所有输出环回至对应输入 |
| `test.sh` | pn_shm_tool 功能测试脚本 |

---

## 5. 构建系统详解

### 5.1 构建架构

**关键事实：本项目的构建只编译示例应用的源码，不编译 p-net 协议栈。** 协议栈以预编译的静态库（`libpnet.a`、`libosal.a`）形式从 `p-net-1.0.2-Linux-aarch64/` 导入。

CMakeLists.txt 通过 `add_library(pnet STATIC IMPORTED)` 创建导入目标，指向 `../p-net-1.0.2-Linux-aarch64/lib/libpnet.a`。

### 5.2 构建步骤

```bash
# 交叉编译（x86 → aarch64），在 ~/profinet/p-net-1.0.2-samples/ 下执行
rm -rf build && mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake
make -j$(nproc)
# 产物：build/pn_dev/pn_dev
```

### 5.3 工具链配置（`cmake/aarch64-linux-gnu.cmake`）

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(TOOLCHAIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../toolchain/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

### 5.4 可选的 CMake 参数

```bash
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake \
  -DUSE_SCHED_FIFO=ON \       # 启用 FIFO 实时调度
  -DBUILD_TESTING=OFF          # 跳过测试
```

### 5.5 CMake Presets（`CMakePresets.json`）

3 个预设（均用 Ninja 生成器）：`debug`（Debug）、`test`（Coverage）、`release`（RelWithDebInfo）。
注意：实际 `build/` 目录使用的是 Unix Makefiles，非预设。

### 5.6 可用平台 CMake 文件（`cmake/`）

| 文件 | 目标平台 |
|------|---------|
| `aarch64-linux-gnu.cmake` | OK3576 aarch64 交叉编译 ← 本项目使用 |
| `Linux.cmake` | 通用 Linux（x86_64 本地编译） |
| `rt-kernel.cmake` | RT-Kernel 实时 OS |
| `STM32Cube.cmake` | STM32Cube + FreeRTOS + lwIP |
| `iMX8MM.cmake` | NXP i.MX8M Mini + FreeRTOS + lwIP |

### 5.7 编译器标志（来自 `cmake/Linux.cmake`）

```
-Wall -Wextra -Werror -Wno-unused-parameter
-ffunction-sections -fdata-sections
-Wl,--gc-sections
```

### 5.8 运行环境依赖

目标板仅需三个共享库（均在标准 Linux 系统中）：
- `libpthread.so.0` — POSIX 线程
- `librt.so.1` — 实时扩展（timer_create 等）
- `libc.so.6` — glibc 2.33

p-net 协议栈和 OSAL 已静态链接入 `pn_dev`。

---

## 6. Callback 机制

应用层通过 **回调函数指针**（在 `pnet_cfg_t` 结构体中注册）与 p-net 栈交互：

### 6.1 I/O 数据回调（`pn_dev/app_data.c` 中实现）

| 回调 | 调用方 | 用途 |
|------|--------|------|
| `app_data_get_input_data(slot, subslot, submodule_id, button, *size, *iops)` | 栈（周期） | 获取从站→PLC 的输入数据，返回数据指针和 IOPS |
| `app_data_set_output_data(slot, subslot, submodule_id, *data, size)` | 栈（周期） | 写入 PLC→从站的输出数据 |
| `app_data_write_parameter(slot, subslot, submodule_id, index, *data, length)` | 栈（参数化） | PLC 写入参数（RecordData） |
| `app_data_read_parameter(slot, subslot, submodule_id, index, **data, *length)` | 栈（参数化） | PLC 读取参数 |
| `app_data_set_default_outputs()` | 栈（断开时） | 连接断开时设置安全默认值 |

### 6.2 事件回调（`pn_dev/sampleapp_common.c` 中实现）

| 回调 | 触发时机 |
|------|---------|
| `app_connect_ind()` | IO-Controller 建立连接 |
| `app_release_ind()` | IO-Controller 释放连接 |
| `app_state_ind()` | 栈状态变化 |
| `app_alarm_ind()` | 收到 PLC 的告警 |
| `app_alarm_cnf()` | 告警发送确认 |
| `app_alarm_ack_cnf()` | 告警应答确认 |
| `app_exp_module_ind()` | 期望模块插入通知 |
| `app_exp_submodule_ind()` | 期望子模块插入通知 |
| `app_new_data_status_ind()` | 新数据状态指示 |
| `app_signal_led_ind()` | Signal LED 状态变化 |
| `app_reset_ind()` | 复位请求 |
| `app_im_changed_ind()` | I&M 数据变更 |

---

## 7. 开发板环境

| 项目 | 值 |
|------|-----|
| 开发板 | OK-3576-C（Rockchip RK3576） |
| Linux 内核 | 6.1.118 RT |
| IP 地址 | 192.168.1.100 |
| 用户名/密码 | default / default |
| 调试串口 | `/dev/ttyFIQ0` |
| 部署目录 | `/home/default/profinet/` |
| 数据存储目录 | `/tmp/pnet/` |
| 运行方式 | 需 root 权限（Ethernet 原始帧收发） |

### 部署命令

```bash
cd ~/profinet/p-net-1.0.2-samples
sshpass -p 'default' scp build/pn_dev/pn_dev default@192.168.1.100:/home/default/profinet/
sshpass -p 'default' scp pn_dev/GSDML*.xml default@192.168.1.100:/home/default/profinet/
sshpass -p 'default' scp kill_pn_dev.sh default@192.168.1.100:/home/default/profinet/
sshpass -p 'default' ssh default@192.168.1.100 "chmod +x /home/default/profinet/pn_dev /home/default/profinet/kill_pn_dev.sh"
```

### 运行命令

```bash
sshpass -p 'default' ssh default@192.168.1.100
cd /home/default/profinet
sudo ./kill_pn_dev.sh
sudo ./pn_dev -i eth0 -s rt-labs-dev
# 或后台运行
sudo ./pn_dev -i eth0 -s rt-labs-dev -vvv > /tmp/pn_dev.log 2>&1 &
```

### 运行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-i` | 网络接口 | eth0（单口）/ br0,eth0,eth1（多口） |
| `-s` | 设备站名（需与 TIA 配置一致） | rt-labs-dev |
| `-p` | 存储目录 | 当前目录 |
| `-v` / `-vvv` | 日志详细级别 | FATAL |
| `-f` | 恢复出厂设置并退出 | — |
| `-r` | 删除存储文件并退出 | — |
| `-g` | 显示栈信息并退出 | — |
| `-b` / `-d` | Button1/2 文件路径（GPIO 模拟用） | — |

---

## 8. 构建产物验证

```bash
file build/pn_dev/pn_dev
# ELF 64-bit LSB executable, ARM aarch64, dynamically linked,
# interpreter /lib/ld-linux-aarch64.so.1, with debug_info, not stripped

readelf -d build/pn_dev/pn_dev | grep NEEDED
# 0x0000000000000001 (NEEDED)  Shared library: [libpthread.so.0]
# 0x0000000000000001 (NEEDED)  Shared library: [librt.so.1]
# 0x0000000000000001 (NEEDED)  Shared library: [libc.so.6]
```

---

## 9. 调试与故障排除

### 9.1 串口输出

程序通过 `/dev/ttyFIQ0`（OK3576 调试串口，115200 8N1）输出 I/O 变化，格式：
```
I1.0=0,I1.1=0,...,I1.7=0 | Q1.0=1,Q1.1=0,...,Q1.7=0
```
打开串口失败时自动回退到 stdout。

### 9.2 进程管理

```bash
sudo ./kill_pn_dev.sh   # 先 SIGTERM → 等 2s → 残留进程 SIGKILL（含子线程）
```

### 9.3 重置状态

```bash
rm -f /tmp/pnet/*.db    # 清除持久化存储（IP、I&M、SNMP 数据等）
sudo ./pn_dev -f         # 恢复出厂设置
```

### 9.4 修改代码后须知

修改 `pn_dev/` 下的任何 `.c/.h` 文件后，只需重新 `make -j$(nproc)`，不需要重新编译协议栈。如果修改了 `CMakeLists.txt`，需重新运行 `cmake ..`。

### 9.5 常见问题

- **"Do you have enough Ethernet interface permission?"** — 需要 root 权限发送原始 Ethernet Layer 2 帧
- **"The given storage directory does not exist"** — 确保 `/tmp/pnet/` 存在或通过 `-p` 指定其他路径
- **设备名不匹配** — `-s` 参数必须与 TIA Portal 中配置的站名完全一致
- **工具链路径** — `cmake/aarch64-linux-gnu.cmake` 中的 `TOOLCHAIN_PATH` 使用相对于 `CMAKE_CURRENT_SOURCE_DIR` 的路径，即 `../toolchain/...`

---

## 10. PLC 侧配合

1. TIA Portal → 选项 → 管理全局设备描述文件（GSD）→ 导入 `GSDML-V2.43-RT-Labs-P-Net-Sample-App-20240530.xml`
2. 硬件目录找到 `RT-Labs P-Net Sample App`，添加到项目
3. 设备名称设置为 `rt-labs-dev`（与 `-s` 参数一致）
4. **Slot 1** 配置 **DIO 8xLogicLevel**（IDM_32，8 位输入 + 8 位输出组合模块）
5. 下载硬件配置到 PLC 并启动 Profinet 通讯

---

## 11. 注意事项

1. **IP 自动分配**：首次启动 IP 为 0.0.0.0，PLC 连接后通过 DCP 协议自动分配
2. **实时性**：已适配 Linux 6.1.118 RT 内核，定时器使用 `SCHED_FIFO` 实时线程
3. **编码风格**：`.clang-format` 配置为 ColumnLimit=80、IndentWidth=3、PointerAlignment=Middle、单参数单行
4. **许可证**：p-net 为 GPLv3/商业双许可，OSAL 为 BSD 3-Clause；商业产品需购买许可
5. **p-net 已知限制**：不支持 MRP、RT_CLASS_UDP、DHCP、Fast Start-up、MC 多播、Shared Input、ProfiDrive/ProfiSafe
