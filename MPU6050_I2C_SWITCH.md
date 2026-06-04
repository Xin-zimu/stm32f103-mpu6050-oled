# MPU6050 硬件 I2C2 说明

当前项目已经改为 MPU6050 完全使用 STM32F103 的硬件 I2C2，不再保留软件 I2C 切换宏。

## 当前接线

| MPU6050 | STM32F103 |
| --- | --- |
| SCL | PB10 / I2C2_SCL |
| SDA | PB11 / I2C2_SDA |
| VCC | 3.3V |
| GND | GND |
| AD0 | GND 或悬空 |

OLED 仍然使用硬件 I2C1：

| OLED | STM32F103 |
| --- | --- |
| SCL | PB6 / I2C1_SCL |
| SDA | PB7 / I2C1_SDA |

## MPU6050 配置

- 总线：硬件 I2C2
- 速度：100 kHz
- 默认地址：0x68
- 兼容地址：0x69
- 代码位置：`User/mpu6050.c`

注意：STM32 标准库 `I2C_Send7bitAddress()` 这里继续使用左移后的地址格式，所以代码中写地址仍为 `0xD0` / `0xD2`，读地址为 `0xD1` / `0xD3`。

## 如果 OLED 显示 MPU:OFF

优先检查：

- PB10 是否接 MPU6050 SCL。
- PB11 是否接 MPU6050 SDA。
- SCL/SDA 是否有上拉电阻，常用 4.7k 到 3.3V。
- AD0 是否接 GND 或保持悬空。
- MPU6050 是否接 3.3V。
- MPU6050 和 STM32 是否共地。

硬件 I2C2 第一目标是确认 WHO_AM_I 能读到 `0x68` 或 `0x69`。确认通信稳定后，再看 Roll/Pitch/Yaw。
