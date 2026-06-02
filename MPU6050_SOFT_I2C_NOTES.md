# MPU6050 软件 I2C 代码逐行注释

这份文档只讲 `User/mpu6050.c` 里的**软件 I2C 部分**。

当前软件 I2C 使用：

| 信号 | STM32 引脚 | 作用 |
| --- | --- | --- |
| SCL | PB10 | I2C 时钟线，由 STM32 控制 |
| SDA | PB11 | I2C 数据线，发送和接收都用它 |

软件 I2C 的核心思想：

```text
不用 STM32 内部 I2C 外设。
用普通 GPIO 手动控制 SCL/SDA 的高低电平。
按 I2C 协议模拟 START、STOP、写字节、读字节、ACK。
```

## 1. 引脚和基础配置

```c
#define MPU6050_GPIO_PORT        GPIOB
#define MPU6050_GPIO_CLK         RCC_APB2Periph_GPIOB
#define MPU6050_SCL_PIN          GPIO_Pin_10
#define MPU6050_SDA_PIN          GPIO_Pin_11
```

逐行解释：

```c
#define MPU6050_GPIO_PORT        GPIOB
```

MPU6050 的 SCL/SDA 都接在 GPIOB 端口。

```c
#define MPU6050_GPIO_CLK         RCC_APB2Periph_GPIOB
```

GPIOB 要先打开时钟，否则 GPIOB 不能工作。

```c
#define MPU6050_SCL_PIN          GPIO_Pin_10
```

PB10 用作 SCL 时钟线。

```c
#define MPU6050_SDA_PIN          GPIO_Pin_11
```

PB11 用作 SDA 数据线。

## 2. I2C 地址

```c
#define MPU6050_ADDR_LOW_WRITE   0xD0
#define MPU6050_ADDR_LOW_READ    0xD1
#define MPU6050_ADDR_HIGH_WRITE  0xD2
#define MPU6050_ADDR_HIGH_READ   0xD3
```

MPU6050 的地址由 `AD0` 引脚决定：

| AD0 | 7 位地址 | 写地址 | 读地址 |
| --- | --- | --- | --- |
| GND / 低电平 | 0x68 | 0xD0 | 0xD1 |
| 3.3V / 高电平 | 0x69 | 0xD2 | 0xD3 |

为什么写地址和读地址不一样？

I2C 实际发送的是：

```text
7 位设备地址 + 1 位读写位
```

例如 `0x68`：

```text
0x68 左移 1 位 = 0xD0
最低位 0 表示写
最低位 1 表示读
```

所以：

```text
0xD0 = 写地址
0xD1 = 读地址
```

## 3. 软件 I2C 延时

```c
#define MPU6050_I2C_DELAY_US     4
```

每次拉高或拉低 SCL/SDA 后，延时 `4us`。

软件 I2C 需要这个延时，因为：

```text
GPIO 电平变化需要稳定时间
MPU6050 读取 SDA 也需要时间
```

数值越大：

```text
通信越慢，但更稳定
```

数值越小：

```text
通信越快，但对接线和时序要求更高
```

## 4. 初始化软件 I2C 总线

```c
static void MPU6050_BusInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(MPU6050_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = MPU6050_SCL_PIN | MPU6050_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU6050_GPIO_PORT, &GPIO_InitStructure);

    MPU6050_SCL(1);
    MPU6050_SDA(1);
}
```

逐行解释：

```c
GPIO_InitTypeDef GPIO_InitStructure;
```

定义一个 GPIO 初始化结构体。

```c
RCC_APB2PeriphClockCmd(MPU6050_GPIO_CLK, ENABLE);
```

打开 GPIOB 时钟。

如果不打开 GPIOB 时钟，PB10/PB11 不会正常输出电平。

```c
GPIO_InitStructure.GPIO_Pin = MPU6050_SCL_PIN | MPU6050_SDA_PIN;
```

同时配置 PB10 和 PB11。

```c
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
```

配置为开漏输出。

I2C 必须使用开漏或类似开漏的方式，因为 SDA 可能由主机拉低，也可能由从机拉低。

开漏输出的特点：

```text
输出 0：主动拉低
输出 1：释放总线，靠上拉电阻变高
```

```c
GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
```

GPIO 输出速度设置为 50MHz。

这不是 I2C 速度，只是 GPIO 电平切换能力。

```c
GPIO_Init(MPU6050_GPIO_PORT, &GPIO_InitStructure);
```

把上面的配置应用到 GPIOB。

```c
MPU6050_SCL(1);
MPU6050_SDA(1);
```

让 I2C 总线进入空闲状态。

I2C 空闲状态是：

```text
SCL = 1
SDA = 1
```

## 5. 控制 SCL

```c
static void MPU6050_SCL(uint8_t level)
{
    if (level)
    {
        GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
    }
    else
    {
        GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
    }
}
```

这个函数用来控制 PB10，也就是 SCL 时钟线。

逐行解释：

```c
static void MPU6050_SCL(uint8_t level)
```

定义一个只在本文件使用的函数。

参数 `level` 表示要输出的电平：

```text
level = 1：SCL 高电平
level = 0：SCL 低电平
```

```c
if (level)
```

如果 `level` 不为 0，就输出高电平。

```c
GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
```

把 PB10 置 1。

```c
GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
```

把 PB10 置 0。

SCL 的作用：

```text
SCL 每产生一个高低变化，就传输 1 位数据。
SCL 高电平期间，SDA 上的数据被读取。
```

## 6. 控制 SDA

```c
static void MPU6050_SDA(uint8_t level)
{
    if (level)
    {
        GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    }
    else
    {
        GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    }
}
```

这个函数用来控制 PB11，也就是 SDA 数据线。

参数 `level`：

```text
level = 1：释放 SDA，让上拉电阻拉高
level = 0：主动把 SDA 拉低
```

注意：

```text
开漏输出时，写 1 不是强行输出高电平，而是释放总线。
```

这非常重要。

因为在读 ACK 或读数据时，MPU6050 也需要控制 SDA。

如果 STM32 一直强推 SDA，高低电平就会冲突。

## 7. 读取 SDA

```c
static uint8_t MPU6050_ReadSDA(void)
{
    return (uint8_t)GPIO_ReadInputDataBit(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
}
```

这个函数读取 PB11 当前电平。

用途：

```text
1. STM32 等 MPU6050 回 ACK 时读取 SDA
2. STM32 从 MPU6050 读数据位时读取 SDA
```

返回值：

```text
0：SDA 是低电平
1：SDA 是高电平
```

## 8. I2C 节拍延时

```c
static void MPU6050_I2C_Delay(void)
{
    delay_us(MPU6050_I2C_DELAY_US);
}
```

这个函数给软件 I2C 每个步骤之间留一点时间。

例如：

```text
先设置 SDA
延时
再拉高 SCL
```

这样 MPU6050 才能可靠地读取 SDA 上的数据。

## 9. START 起始信号

```c
static void MPU6050_I2C_Start(void)
{
    MPU6050_SDA(1);
    MPU6050_SCL(1);
    MPU6050_I2C_Delay();
    MPU6050_SDA(0);
    MPU6050_I2C_Delay();
    MPU6050_SCL(0);
}
```

I2C 起始信号规则：

```text
当 SCL 为高电平时，SDA 从高变低，就是 START。
```

逐行解释：

```c
MPU6050_SDA(1);
MPU6050_SCL(1);
```

先让总线处于空闲状态：

```text
SDA = 1
SCL = 1
```

```c
MPU6050_I2C_Delay();
```

等待电平稳定。

```c
MPU6050_SDA(0);
```

在 SCL 保持高电平时，把 SDA 拉低。

这一步就是 START。

```c
MPU6050_I2C_Delay();
```

保持一小段时间，让 MPU6050 识别到 START。

```c
MPU6050_SCL(0);
```

拉低 SCL，准备开始发送数据位。

## 10. STOP 停止信号

```c
static void MPU6050_I2C_Stop(void)
{
    MPU6050_SDA(0);
    MPU6050_SCL(1);
    MPU6050_I2C_Delay();
    MPU6050_SDA(1);
    MPU6050_I2C_Delay();
}
```

I2C 停止信号规则：

```text
当 SCL 为高电平时，SDA 从低变高，就是 STOP。
```

逐行解释：

```c
MPU6050_SDA(0);
```

先把 SDA 拉低。

```c
MPU6050_SCL(1);
```

拉高 SCL。

```c
MPU6050_I2C_Delay();
```

等待电平稳定。

```c
MPU6050_SDA(1);
```

在 SCL 保持高电平时释放 SDA，让 SDA 变高。

这一步就是 STOP。

```c
MPU6050_I2C_Delay();
```

保持一小段时间，让 MPU6050 识别到通信结束。

## 11. 等待从机 ACK

```c
static uint8_t MPU6050_I2C_WaitAck(void)
{
    uint8_t ack;

    MPU6050_SDA(1);
    MPU6050_I2C_Delay();
    MPU6050_SCL(1);
    MPU6050_I2C_Delay();
    ack = (MPU6050_ReadSDA() == 0) ? 1 : 0;
    MPU6050_SCL(0);

    return ack;
}
```

这个函数用于：

```text
STM32 写完 1 个字节后，等待 MPU6050 回应。
```

I2C 规则：

```text
发送方发送 8 位数据后，第 9 个时钟由接收方发送 ACK。
ACK = SDA 被拉低
NACK = SDA 保持高
```

逐行解释：

```c
uint8_t ack;
```

保存 ACK 判断结果。

```c
MPU6050_SDA(1);
```

STM32 释放 SDA。

因为现在要让 MPU6050 控制 SDA 回 ACK。

```c
MPU6050_I2C_Delay();
```

等待 SDA 释放稳定。

```c
MPU6050_SCL(1);
```

拉高 SCL，产生第 9 个 ACK 时钟。

```c
MPU6050_I2C_Delay();
```

等待 MPU6050 把 ACK 电平放到 SDA 上。

```c
ack = (MPU6050_ReadSDA() == 0) ? 1 : 0;
```

读取 SDA：

```text
SDA = 0：收到 ACK，返回 1
SDA = 1：没有 ACK，返回 0
```

```c
MPU6050_SCL(0);
```

拉低 SCL，结束 ACK 位。

```c
return ack;
```

返回 ACK 结果。

## 12. 主机发送 ACK / NACK

```c
static void MPU6050_I2C_SendAck(uint8_t ack)
{
    MPU6050_SDA(ack ? 0 : 1);
    MPU6050_I2C_Delay();
    MPU6050_SCL(1);
    MPU6050_I2C_Delay();
    MPU6050_SCL(0);
    MPU6050_SDA(1);
}
```

这个函数用于：

```text
STM32 从 MPU6050 读完 1 个字节后，告诉 MPU6050 还要不要继续发。
```

I2C 规则：

```text
谁接收数据，谁发送 ACK。
```

所以：

```text
STM32 写数据给 MPU6050：MPU6050 回 ACK。
STM32 读数据 from MPU6050：STM32 回 ACK。
```

参数 `ack`：

```text
ack = 1：发送 ACK，表示继续读
ack = 0：发送 NACK，表示读完了
```

逐行解释：

```c
MPU6050_SDA(ack ? 0 : 1);
```

如果 `ack = 1`：

```text
SDA 拉低，发送 ACK。
```

如果 `ack = 0`：

```text
SDA 释放为高，发送 NACK。
```

```c
MPU6050_I2C_Delay();
```

等待 SDA 稳定。

```c
MPU6050_SCL(1);
```

拉高 SCL，让 MPU6050 读取这个 ACK/NACK。

```c
MPU6050_I2C_Delay();
```

保持 SCL 高电平一小段时间。

```c
MPU6050_SCL(0);
```

拉低 SCL，结束 ACK/NACK 位。

```c
MPU6050_SDA(1);
```

释放 SDA，避免一直占用数据线。

## 13. 写 1 个字节

```c
static uint8_t MPU6050_I2C_WriteByte(uint8_t data)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        MPU6050_SDA((data & 0x80) ? 1 : 0);
        data <<= 1;
        MPU6050_I2C_Delay();
        MPU6050_SCL(1);
        MPU6050_I2C_Delay();
        MPU6050_SCL(0);
    }

    return MPU6050_I2C_WaitAck();
}
```

这个函数用于：

```text
STM32 通过 I2C 发送 1 个字节给 MPU6050。
```

I2C 规则：

```text
1 个字节 = 8 位。
先发送最高位 bit7，再发送 bit6，一直到 bit0。
每发送 1 位，SCL 拉高一次，让从机读取。
8 位发完后，等待从机 ACK。
```

逐行解释：

```c
uint8_t i;
```

循环变量，用来发送 8 位。

```c
for (i = 0; i < 8; i++)
```

循环 8 次，因为一个字节有 8 位。

```c
MPU6050_SDA((data & 0x80) ? 1 : 0);
```

取 `data` 的最高位。

`0x80` 的二进制是：

```text
1000 0000
```

所以：

```c
data & 0x80
```

就是判断最高位是否为 1。

如果最高位是 1：

```text
SDA = 1
```

如果最高位是 0：

```text
SDA = 0
```

```c
data <<= 1;
```

左移一位。

这样下一位就会移动到最高位，方便下一轮继续用 `data & 0x80` 判断。

例子：

```text
原始 data = 1101 0000
第一次发送 bit7 = 1
左移后 data = 1010 0000
第二次发送 bit7 = 1
再左移 data = 0100 0000
第三次发送 bit7 = 0
```

```c
MPU6050_I2C_Delay();
```

等待 SDA 电平稳定。

```c
MPU6050_SCL(1);
```

拉高 SCL。

I2C 规则：

```text
SCL 高电平期间，从机读取 SDA。
```

```c
MPU6050_I2C_Delay();
```

保持 SCL 高电平一小段时间。

```c
MPU6050_SCL(0);
```

拉低 SCL，结束这一位传输。

```c
return MPU6050_I2C_WaitAck();
```

8 位全部发送完后，等待 MPU6050 回 ACK。

返回值：

```text
1：发送成功，MPU6050 回 ACK
0：发送失败，没有 ACK
```

## 14. 读 1 个字节

```c
static uint8_t MPU6050_I2C_ReadByte(uint8_t ack)
{
    uint8_t i;
    uint8_t data;

    data = 0;
    MPU6050_SDA(1);

    for (i = 0; i < 8; i++)
    {
        data <<= 1;
        MPU6050_SCL(1);
        MPU6050_I2C_Delay();

        if (MPU6050_ReadSDA())
        {
            data |= 0x01;
        }

        MPU6050_SCL(0);
        MPU6050_I2C_Delay();
    }

    MPU6050_I2C_SendAck(ack);
    return data;
}
```

这个函数用于：

```text
STM32 从 MPU6050 读取 1 个字节。
```

读数据时：

```text
MPU6050 控制 SDA
STM32 控制 SCL
```

逐行解释：

```c
uint8_t i;
uint8_t data;
```

`i` 是循环变量。

`data` 用来保存读到的 8 位。

```c
data = 0;
```

先把结果清零。

```c
MPU6050_SDA(1);
```

释放 SDA。

因为现在是 MPU6050 往 STM32 发数据，STM32 不能占用 SDA。

```c
for (i = 0; i < 8; i++)
```

循环 8 次，读取 8 位。

```c
data <<= 1;
```

先左移一位，给新读到的 bit 腾出最低位。

```c
MPU6050_SCL(1);
```

拉高 SCL。

SCL 高电平期间，SDA 上的数据有效。

```c
MPU6050_I2C_Delay();
```

等待 MPU6050 把当前 bit 放到 SDA 上。

```c
if (MPU6050_ReadSDA())
{
    data |= 0x01;
}
```

读取 SDA。

如果 SDA 是高电平，说明当前 bit 是 1，就把 `data` 的最低位置 1。

如果 SDA 是低电平，说明当前 bit 是 0，`data` 不需要改变。

```c
MPU6050_SCL(0);
```

拉低 SCL，结束当前 bit 读取。

```c
MPU6050_I2C_Delay();
```

等待一小段时间，准备下一位。

```c
MPU6050_I2C_SendAck(ack);
```

读完 8 位后，STM32 发送 ACK 或 NACK。

```text
ack = 1：继续读下一个字节
ack = 0：这是最后一个字节，读完了
```

```c
return data;
```

返回读到的字节。

## 15. 写 MPU6050 寄存器

```c
static uint8_t MPU6050_WriteReg(uint8_t reg, uint8_t data)
{
    uint8_t ok;

    ok = 1;
    MPU6050_I2C_Start();
    ok &= MPU6050_I2C_WriteByte(s_addr_write);
    ok &= MPU6050_I2C_WriteByte(reg);
    ok &= MPU6050_I2C_WriteByte(data);
    MPU6050_I2C_Stop();

    return ok;
}
```

这个函数用于：

```text
往 MPU6050 的某个寄存器写 1 个字节。
```

例如：

```c
MPU6050_WriteReg(MPU6050_REG_PWR_MGMT_1, 0x00);
```

意思是：

```text
往电源管理寄存器 0x6B 写入 0x00，唤醒 MPU6050。
```

逐行解释：

```c
uint8_t ok;
```

保存通信是否成功。

```c
ok = 1;
```

先假设通信成功。

```c
MPU6050_I2C_Start();
```

发送 START，开始一次 I2C 通信。

```c
ok &= MPU6050_I2C_WriteByte(s_addr_write);
```

发送 MPU6050 写地址。

如果 MPU6050 回 ACK，返回 1。

如果没回 ACK，返回 0。

`ok &=` 的意思是：

```text
只要其中一步失败，ok 最后就是 0。
```

```c
ok &= MPU6050_I2C_WriteByte(reg);
```

发送寄存器地址。

告诉 MPU6050：

```text
我要操作哪个寄存器。
```

```c
ok &= MPU6050_I2C_WriteByte(data);
```

发送要写入寄存器的数据。

```c
MPU6050_I2C_Stop();
```

发送 STOP，结束通信。

```c
return ok;
```

返回整体写入是否成功。

完整时序：

```text
START
发送设备写地址
发送寄存器地址
发送数据
STOP
```

## 16. 连续读取 MPU6050 寄存器

```c
static uint8_t MPU6050_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t ok;

    if (len == 0)
    {
        return 0;
    }

    ok = 1;
    MPU6050_I2C_Start();
    ok &= MPU6050_I2C_WriteByte(s_addr_write);
    ok &= MPU6050_I2C_WriteByte(reg);

    MPU6050_I2C_Start();
    ok &= MPU6050_I2C_WriteByte(s_addr_read);

    if (ok == 0)
    {
        MPU6050_I2C_Stop();
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        buf[i] = MPU6050_I2C_ReadByte((i + 1u) < len);
    }

    MPU6050_I2C_Stop();
    return 1;
}
```

这个函数用于：

```text
从某个寄存器开始，连续读取 len 个字节。
```

例如：

```c
MPU6050_ReadRegs(0x3B, data, 14);
```

意思是：

```text
从 0x3B 开始连续读 14 个字节。
```

逐行解释：

```c
uint8_t i;
uint8_t ok;
```

`i` 用于循环读取多个字节。

`ok` 用于记录通信是否成功。

```c
if (len == 0)
{
    return 0;
}
```

如果要读取 0 个字节，没有意义，直接返回失败。

```c
ok = 1;
```

先假设通信成功。

```c
MPU6050_I2C_Start();
```

发送 START。

```c
ok &= MPU6050_I2C_WriteByte(s_addr_write);
```

先发送写地址。

为什么读数据前先写？

因为要先告诉 MPU6050：

```text
我要从哪个寄存器开始读。
```

```c
ok &= MPU6050_I2C_WriteByte(reg);
```

发送起始寄存器地址。

例如 `reg = 0x3B`，意思是从加速度 X 高字节开始读。

```c
MPU6050_I2C_Start();
```

再次发送 START。

这叫重复起始信号。

作用是：

```text
不释放总线，直接从写模式切换到读模式。
```

```c
ok &= MPU6050_I2C_WriteByte(s_addr_read);
```

发送 MPU6050 读地址。

告诉 MPU6050：

```text
现在我要开始读数据。
```

```c
if (ok == 0)
{
    MPU6050_I2C_Stop();
    return 0;
}
```

如果前面任何一步没收到 ACK，就停止通信并返回失败。

```c
for (i = 0; i < len; i++)
```

循环读取 `len` 个字节。

```c
buf[i] = MPU6050_I2C_ReadByte((i + 1u) < len);
```

读取 1 个字节，并存入 `buf[i]`。

关键是：

```c
(i + 1u) < len
```

它用来判断当前字节是不是最后一个字节。

如果不是最后一个字节：

```text
发送 ACK，告诉 MPU6050 继续发。
```

如果是最后一个字节：

```text
发送 NACK，告诉 MPU6050 读完了。
```

例如读取 14 字节：

```text
第 1 ~ 第 13 字节：ACK
第 14 字节：NACK
```

```c
MPU6050_I2C_Stop();
```

发送 STOP，结束通信。

```c
return 1;
```

返回读取成功。

完整时序：

```text
START
发送设备写地址
发送起始寄存器地址
重复 START
发送设备读地址
读取第 1 字节，ACK
读取第 2 字节，ACK
...
读取最后 1 字节，NACK
STOP
```

## 17. 读取单个寄存器

```c
static uint8_t MPU6050_ReadReg(uint8_t reg, uint8_t *data)
{
    return MPU6050_ReadRegs(reg, data, 1);
}
```

这个函数用于：

```text
读取 MPU6050 的 1 个寄存器。
```

它直接调用 `MPU6050_ReadRegs()`。

因为：

```text
读 1 个寄存器，本质就是连续读取 1 个字节。
```

例如：

```c
MPU6050_ReadReg(MPU6050_REG_WHO_AM_I, &s_who_am_i);
```

意思是：

```text
读取 WHO_AM_I 寄存器，判断 MPU6050 是否在线。
```

## 18. 软件 I2C 写寄存器整体流程

调用：

```c
MPU6050_WriteReg(0x6B, 0x00);
```

实际流程：

```text
MPU6050_I2C_Start()
MPU6050_I2C_WriteByte(0xD0)
    发送设备写地址
    等 MPU6050 ACK
MPU6050_I2C_WriteByte(0x6B)
    发送寄存器地址
    等 MPU6050 ACK
MPU6050_I2C_WriteByte(0x00)
    发送写入数据
    等 MPU6050 ACK
MPU6050_I2C_Stop()
```

## 19. 软件 I2C 读 14 字节整体流程

调用：

```c
MPU6050_ReadRegs(0x3B, data, 14);
```

实际流程：

```text
MPU6050_I2C_Start()
MPU6050_I2C_WriteByte(0xD0)
    发送设备写地址
    等 ACK
MPU6050_I2C_WriteByte(0x3B)
    发送起始寄存器地址
    等 ACK

MPU6050_I2C_Start()
MPU6050_I2C_WriteByte(0xD1)
    发送设备读地址
    等 ACK

MPU6050_I2C_ReadByte(1)
    读第 1 字节，回 ACK
...
MPU6050_I2C_ReadByte(1)
    读第 13 字节，回 ACK
MPU6050_I2C_ReadByte(0)
    读第 14 字节，回 NACK

MPU6050_I2C_Stop()
```

## 20. 初学者记忆版

软件 I2C 就记住这几句话：

```text
SCL 是时钟，STM32 控制。
SDA 是数据，谁发送数据谁控制。
SCL 高电平时，SDA 数据有效。
SCL 高电平时，SDA 高变低是 START。
SCL 高电平时，SDA 低变高是 STOP。
每传 8 位，第 9 位是 ACK。
ACK 是 SDA 低电平。
NACK 是 SDA 高电平。
```

## 21. 最容易错的地方

| 问题 | 现象 |
| --- | --- |
| SCL/SDA 接反 | `MPU:OFF` |
| 没有共地 | `MPU:OFF` 或数据乱跳 |
| 没有上拉电阻 | 通信不稳定 |
| AD0 地址不清楚 | 可能读不到 WHO_AM_I |
| SDA 没释放 | 读 ACK 或读数据失败 |
| 最后一个字节没发 NACK | 连续读取异常 |

## 22. 推荐学习顺序

先看这些函数：

```text
MPU6050_SCL()
MPU6050_SDA()
MPU6050_I2C_Start()
MPU6050_I2C_Stop()
MPU6050_I2C_WriteByte()
MPU6050_I2C_WaitAck()
MPU6050_I2C_ReadByte()
MPU6050_I2C_SendAck()
MPU6050_WriteReg()
MPU6050_ReadRegs()
```

最后再看：

```text
MPU6050_ReadRaw()
MPU6050_UpdateAngles()
```

也就是：

```text
先学会怎么通信
再学会怎么读数据
最后再学角度算法
```
