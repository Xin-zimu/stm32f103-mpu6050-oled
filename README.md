# STM32F103 OLED 多传感器工程

本工程基于 STM32F103C8，已接入 OLED、光敏传感器、DS18B20、5D 摇杆、交通灯 LED、USART1 串口练习程序，以及本次新增的 MPU6050 三维角度传感器。

这个项目不是单独点亮一个模块，而是把 **传感器采集、姿态解算、OLED 菜单、串口协议和电脑端 3D 可视化** 串成一个完整闭环：MPU6050 采集六轴数据，STM32 计算 Roll/Pitch/Yaw，通过 USART1 输出姿态帧，电脑端上位机接收后驱动 3D 长方体旋转。

## 项目特点

- 多外设综合：OLED、MPU6050、光敏、DS18B20、5D 摇杆、交通灯 LED、USART1 同时工作。
- 模块化结构：传感器驱动、应用任务、UI、串口姿态流分开维护，避免所有逻辑堆在 `main.c`。
- 非阻塞任务轮询：主循环按任务拆分，MPU6050 校准、温度读取、UI 刷新和串口输出都尽量不长时间占用主循环。
- 硬件 I2C 分离：OLED 使用 `I2C1 PB6/PB7`，MPU6050 使用 `I2C2 PB10/PB11`，互不抢总线。
- 姿态数据协议清晰：默认 USART1 只输出 `ATT,roll10,pitch10,yaw10`，三个整数的单位都是 `0.1°`。
- 上位机可视化：`mpu_viewer` 用 Python 启动本地页面，3D 长方体实时跟随 MPU6050 姿态变化。
- 调试信息明确：MPU6050 未准备好时会输出 `STA,CAL` 或 `STA,OFF`，能区分“正在校准”“传感器离线”和“程序没跑”。

## 曾遇到的问题与处理

本项目开发中遇到过几类容易让 STM32 看起来“卡死”的问题，当前版本已经针对这些点做了处理：

| 问题 | 表现 | 当前处理 |
| --- | --- | --- |
| ADC 等待无超时 | 烧录后可能停在 ADC 校准或采样等待 | `User/light_sensor.c` 给 ADC 校准、EOC 等待加超时，采样失败返回上一次有效 AO |
| 串口 `printf` 等待无超时 | 串口发送标志异常时主循环可能被卡住 | `SYSTEM/usart/usart.c` 给 `fputc()` 加发送超时 |
| AO 抖动太快 | OLED 频繁刷新，主循环压力变大 | `User/app_light.c` 对 AO 做平滑和变化阈值过滤，并降低采样刷新压力 |
| I2C 外设异常 | OLED 或 MPU6050 接线异常时容易像卡死 | OLED 和 MPU6050 的 I2C 等待都带超时，失败后返回，不无限等硬件标志位 |
| MPU6050 未就绪时上位机无数据 | 电脑端看起来像没连接或板子没跑 | 姿态流任务输出 `STA,CAL` / `STA,OFF` 状态帧 |
| 开机欢迎信息干扰上位机协议 | 上位机期望 `ATT` 帧，串口练习欢迎文本会干扰解析 | 默认关闭串口练习任务，USART1 主要输出姿态数据流 |
| 姿态输出使用多次 `printf` | 小单片机库格式化输出开销较大，也不利于排查 | `User/app_attitude_stream.c` 改为轻量逐字符 USART 发送 |
| 上位机路径运行错误 | 在 `C:\Users\86139` 直接运行找不到 `mpu_viewer\main.py` | README 明确要求先 `cd` 到工程目录，或使用完整路径 |
| 上位机 Ctrl+C 不退出 | Windows PowerShell 下本地服务退出不干净 | `mpu_viewer/main.py` 改为标准 `KeyboardInterrupt` 退出流程 |

如果后续再次出现“烧录后卡死”，优先用 `COM9 / 115200` 看是否有 `STA,CAL`、`STA,OFF` 或 `ATT` 输出；如果完全没有串口输出，再用 ST-Link 暂停查看 PC 是否停在 `HardFault_Handler`、时钟初始化、OLED 初始化或 MPU6050 初始化附近。

## 当前功能

- 128x64 OLED 状态显示和菜单界面
- 5D 摇杆菜单控制
- 光敏 AO/DO 读取和阈值设置
- DS18B20 温度读取
- 红黄绿交通灯控制
- USART1 文本命令交互
- MPU6050 姿态读取，显示 Roll/Pitch/Yaw
- USART1 周期输出 `ATT,roll10,pitch10,yaw10` 姿态帧
- Python 上位机 3D 长方体姿态显示
- 非阻塞主循环任务轮询

## 引脚接线表

| 模块 | 信号 | STM32F103 引脚 | 说明 |
| --- | --- | --- | --- |
| OLED | SCL | PB6 / I2C1_SCL | 硬件 I2C1 |
| OLED | SDA | PB7 / I2C1_SDA | 硬件 I2C1 |
| OLED | VCC | 3.3V | 不建议接 5V |
| OLED | GND | GND | 共地 |
| MPU6050 | SCL | PB10 / I2C2_SCL | 硬件 I2C2 |
| MPU6050 | SDA | PB11 / I2C2_SDA | 硬件 I2C2 |
| MPU6050 | VCC | 3.3V | 使用 3.3V 供电 |
| MPU6050 | GND | GND | 共地 |
| MPU6050 | AD0 | GND 或悬空 | 默认地址 0x68；代码也兼容 AD0=1 的 0x69 |
| 光敏传感器 | AO | PA0 | ADC 输入 |
| 光敏传感器 | DO | PA1 | 数字输入 |
| 5D 摇杆 | UP | PA2 | 上拉输入，按下为低 |
| 5D 摇杆 | DOWN | PA3 | 上拉输入，按下为低 |
| 5D 摇杆 | LEFT | PA4 | 上拉输入，按下为低 |
| 5D 摇杆 | RIGHT | PB5 | 上拉输入，按下为低 |
| 5D 摇杆 | PRESS/MID | PB1 | 上拉输入，按下为低 |
| 5D 摇杆 | COM | GND | 公共端 |
| DS18B20 | DQ | PB0 | 需要上拉 |
| 交通灯 LED | 红灯 | PA5 | 输出 |
| 交通灯 LED | 黄灯 | PA6 | 输出 |
| 交通灯 LED | 绿灯 | PA7 | 输出 |
| USART1 | TX | PA9 | 接 USB-TTL RXD |
| USART1 | RX | PA10 | 接 USB-TTL TXD |
| USART1 | GND | GND | USB-TTL 必须共地 |

本次 MPU6050 使用剩余的 `PB10/PB11`，没有占用 OLED 的 `PB6/PB7`，也避开了光敏、温度、摇杆、交通灯和串口已经使用的引脚。

## MPU6050 说明

新增文件：

- `User/mpu6050.c`
- `User/mpu6050.h`

MPU6050 配置：

- SCL：PB10 / I2C2_SCL
- SDA：PB11 / I2C2_SDA
- 硬件 I2C2
- I2C 速度：100 kHz
- 默认地址：0x68
- 兼容地址：0x69
- 加速度量程：+-2g
- 陀螺仪量程：+-250 dps
- 更新周期：20 ms
- 启动校准：200 次陀螺仪零偏采样

当前 `codex/mpu6050-hw-i2c` 分支使用硬件 I2C2。接线是 `PB10=SCL`、`PB11=SDA`。

上电后 OLED 会短暂显示 `MPU6050 INIT` 和 `KEEP STILL`。这时请保持模块静止，校准完成后进入主界面。

角度输出：

- `ROLL`：横滚角
- `PITCH`：俯仰角
- `YAW`：航向角

注意：MPU6050 没有磁力计，`YAW` 只能靠陀螺仪积分，会随时间漂移；`ROLL/PITCH` 使用加速度计和陀螺仪互补滤波，稳定性更好。

## 学习文档

| 文档 | 内容 |
| --- | --- |
| `MPU6050_I2C_SWITCH.md` | 软件 I2C 和硬件 I2C2 的切换方法 |
| `MPU6050_UI_QUESTIONS.md` | MPU6050 接入、OLED 页面、状态变量、事件处理、非阻塞设计问题表 |

## OLED 界面

主界面上半部分显示：

- MPU 状态：`OK`、`CAL`、`OFF`
- Roll/Pitch/Yaw 摘要
- 光照状态
- 光敏 AO
- DS18B20 温度

主菜单：

- `STATUS`
- `SET TH`
- `TRAFFIC`
- `MPU6050`

操作：

- 上/下：移动光标
- 按压：进入页面或确认
- 左：部分页面返回

### STATUS 页面

显示光照、AO、温度、MPU 状态、Roll/Pitch。

按压或左键返回主菜单。

### SET TH 页面

显示当前 AO 和光敏阈值。

- 左：阈值减 50
- 右：阈值加 50
- 按压：保存并返回主菜单

阈值限制在 `300` 到 `3800`。

### TRAFFIC 页面

可选择交通灯模式：

- `AUTO`
- `RED`
- `YELLOW`
- `GREEN`
- `OFF`

按压应用当前模式，左键返回主菜单。

### MPU6050 页面

显示：

- MPU 状态
- Roll
- Pitch
- Yaw
- MPU6050 内部温度
- WHO_AM_I 芯片 ID

按压或左键返回主菜单。

## 主循环

主循环位于 `User/main.c`：

```c
while (1)
{
    App_Light_Task();
    App_Temp_Task();
    MPU6050_Task();
    App_AttitudeStream_Task();
    Joystick_Task();
    App_UI_Task();
}
```

默认已经关闭串口练习命令任务，串口主要用于上位机姿态数据流。这样烧录后 USART1 会尽量只输出 `ATT` 数据，便于电脑软件解析。

如果需要重新打开 `HELP`、`STATUS`、`LED` 等串口命令，把 `User/main.c` 里的宏改为：

```c
#define APP_UART_PRACTICE_ENABLE  1
```

## 串口命令

USART1 参数：

```text
TX: PA9
RX: PA10
波特率: 115200
数据格式: 8N1
```

当前默认串口输出姿态帧：

```text
ATT,roll10,pitch10,yaw10
```

示例：

```text
ATT,123,-45,876
```

三个整数的单位都是 `0.1°`，例如 `123` 表示 `12.3°`。

发送周期：`50ms`，约 `20Hz`。

MPU6050 还没准备好时会输出调试状态帧：

```text
STA,CAL
STA,OFF
```

- `STA,CAL`：正在校准，请保持 MPU6050 静止。
- `STA,OFF`：没有读到 MPU6050，优先检查 VCC/GND/SCL/SDA 和上拉电阻。

串口练习命令默认关闭。如果开启 `APP_UART_PRACTICE_ENABLE`，可使用：

常用命令：

```text
HELP
PING
ECHO hello
STATUS
LED RED
LED YELLOW
LED GREEN
LED OFF
LED AUTO
TH?
TH +
TH -
TH 2000
```

`STATUS` 会输出光敏、温度、交通灯模式和 MPU6050 角度状态。

## 电脑端 3D 姿态上位机

上位机目录：

```text
mpu_viewer/
```

你的串口号是 `COM9`，推荐直接这样运行：

```powershell
cd "D:\stm32f103实例\mpu6050硬件i2c"
py -m pip install -r mpu_viewer\requirements.txt
py mpu_viewer\main.py --port COM9
```

如果只想先看电脑端 3D 效果，不接 STM32：

```powershell
cd "D:\stm32f103实例\mpu6050硬件i2c"
py mpu_viewer\main.py --demo
```

程序会打开本地网页，显示 3D 长方体和 Roll/Pitch/Yaw 数值。MPU6050 没有磁力计，`YAW` 会漂移，第一版主要看 `ROLL/PITCH` 是否跟随实物倾斜。

## 修改引脚的位置

| 外设 | 修改文件 |
| --- | --- |
| OLED | `User/oled.c` |
| MPU6050 | `User/mpu6050.c` |
| 光敏传感器 | `User/light_sensor.c` |
| DS18B20 | `User/ds18b20.c` |
| 交通灯 LED | `User/led.c` |
| 5D 摇杆 | `User/joystick.h` |
| USART1 | `SYSTEM/usart/usart.c` |

改引脚后要重新检查是否和已有外设冲突。

## Keil 工程

Keil 工程文件：

```text
Project/led.uvprojx
```

本次已加入工程文件：

- `User/mpu6050.c`
- `User/mpu6050.h`
- `User/app_attitude_stream.c`
- `User/app_attitude_stream.h`

## 调试检查

如果 OLED 显示 `MPU:OFF`：

- 检查 MPU6050 VCC 是否接 3.3V。
- 检查 GND 是否和 STM32 共地。
- 检查 SCL 是否接 PB10。
- 检查 SDA 是否接 PB11。
- 检查模块是否自带 I2C 上拉电阻；没有上拉时建议 SCL/SDA 各接 4.7k 到 3.3V。
- 如果 AD0 接高电平，代码会尝试 0x69 地址；如果仍失败，优先检查接线。

如果角度抖动或偏移明显：

- 上电校准时保持 MPU6050 静止。
- 模块固定后再上电。
- 远离强振动源。
- `YAW` 长时间漂移是正常现象，MPU6050 单独无法消除航向漂移。
