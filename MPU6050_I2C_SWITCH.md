# MPU6050 软件 I2C / 硬件 I2C 切换说明

当前分支：`codex/mpu6050-hw-i2c`

这个分支保留了软件 I2C，同时新增了硬件 I2C2 实现。

## 默认状态

默认仍然使用软件 I2C：

```c
#define MPU6050_USE_HW_I2C       0
```

位置：

```text
User/mpu6050.c
```

默认接线不变：

| MPU6050 | STM32F103 |
| --- | --- |
| SCL | PB10 |
| SDA | PB11 |
| VCC | 3.3V |
| GND | GND |
| AD0 | GND |

## 切换到硬件 I2C2

把 `User/mpu6050.c` 顶部的宏改成：

```c
#define MPU6050_USE_HW_I2C       1
```

硬件 I2C2 仍然使用同一组引脚：

```text
PB10 -> I2C2_SCL
PB11 -> I2C2_SDA
```

所以接线不用改。

## 两种方式的区别

| 方式 | 优点 | 缺点 |
| --- | --- | --- |
| 软件 I2C | 容易理解，调试直观，不容易被硬件外设状态卡住 | CPU 手动模拟时序，速度较低 |
| 硬件 I2C2 | 外设自动产生时序，后续可以扩展中断/DMA | STM32F1 的 I2C 外设更容易遇到 BUSY、ACK、最后一字节处理问题 |

## 学习建议

先用软件 I2C 确认：

```text
OLED 显示 MPU:OK
Roll/Pitch 会随倾斜变化
```

再切换硬件 I2C2。

硬件 I2C2 的第一目标不是角度显示，而是先确认：

```text
WHO_AM_I 能读到 0x68 或 0x69
```

确认通信稳定后，再继续看 Roll/Pitch/Yaw。

## 如果硬件 I2C2 显示 MPU:OFF

优先检查：

- PB10 是否接 SCL
- PB11 是否接 SDA
- SCL/SDA 是否有上拉
- AD0 是否接 GND
- MPU6050 是否接 3.3V
- GND 是否共地

如果硬件 I2C2 不稳定，可以把宏改回：

```c
#define MPU6050_USE_HW_I2C       0
```

这样马上恢复软件 I2C。
