# STM32F103 OLED 多传感器工程

本工程基于 STM32F103C8，已接入 OLED、光敏传感器、DS18B20、5D 摇杆、交通灯 LED、USART1 串口练习程序，以及本次新增的 MPU6050 三维角度传感器。

## 当前功能

- 128x64 OLED 状态显示和菜单界面
- 5D 摇杆菜单控制
- 光敏 AO/DO 读取和阈值设置
- DS18B20 温度读取
- 红黄绿交通灯控制
- USART1 文本命令交互
- MPU6050 姿态读取，显示 Roll/Pitch/Yaw
- 非阻塞主循环任务轮询

## 引脚接线表

| 模块 | 信号 | STM32F103 引脚 | 说明 |
| --- | --- | --- | --- |
| OLED | SCL | PB6 / I2C1_SCL | 硬件 I2C1 |
| OLED | SDA | PB7 / I2C1_SDA | 硬件 I2C1 |
| OLED | VCC | 3.3V | 不建议接 5V |
| OLED | GND | GND | 共地 |
| MPU6050 | SCL | PB10 | 新增，软件 I2C |
| MPU6050 | SDA | PB11 | 新增，软件 I2C |
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
- `MPU6050_SOFT_I2C_NOTES.md`

MPU6050 配置：

- SCL：PB10
- SDA：PB11
- 默认软件 I2C
- 默认地址：0x68
- 兼容地址：0x69
- 加速度量程：+-2g
- 陀螺仪量程：+-250 dps
- 更新周期：20 ms
- 启动校准：200 次陀螺仪零偏采样

当前 `codex/mpu6050-hw-i2c` 分支同时保留软件 I2C 和硬件 I2C2。默认仍然使用软件 I2C，如果要试硬件 I2C2，把 `User/mpu6050.c` 里的宏改成：

```c
#define MPU6050_USE_HW_I2C       1
```

接线仍然是 `PB10=SCL`、`PB11=SDA`。

上电后 OLED 会短暂显示 `MPU6050 INIT` 和 `KEEP STILL`。这时请保持模块静止，校准完成后进入主界面。

角度输出：

- `ROLL`：横滚角
- `PITCH`：俯仰角
- `YAW`：航向角

注意：MPU6050 没有磁力计，`YAW` 只能靠陀螺仪积分，会随时间漂移；`ROLL/PITCH` 使用加速度计和陀螺仪互补滤波，稳定性更好。

## 学习文档

| 文档 | 内容 |
| --- | --- |
| `MPU6050_SOFT_I2C_NOTES.md` | 软件 I2C 每个函数的逐行注释，适合初学者学习 SCL/SDA、START、STOP、ACK、读写字节 |
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
    Joystick_Task();
    App_UI_Task();
    App_UARTPractice_Task();
}
```

## 串口命令

USART1 参数：

```text
TX: PA9
RX: PA10
波特率: 115200
数据格式: 8N1
```

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
