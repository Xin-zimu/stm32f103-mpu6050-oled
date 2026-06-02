#include "mpu6050.h"
#include "delay.h"
#include "timing.h"

/*
 * 软件 I2C 说明
 *
 * I2C 只有两根线:
 * SCL: 时钟线, 由 STM32 控制高低电平, 决定什么时候传一位数据。
 * SDA: 数据线, 既可以由 STM32 输出, 也可以由 MPU6050 拉低回应。
 *
 * 所谓软件 I2C, 就是不使用 STM32 内部的 I2C 外设,
 * 而是用普通 GPIO 手动模拟 I2C 时序:
 * 1. 用 GPIO_SetBits/GPIO_ResetBits 控制 SCL/SDA。
 * 2. 用 delay_us 控制每个电平保持的时间。
 * 3. 按 I2C 协议手动产生 START、STOP、写 8 位、读 8 位、ACK。
 *
 * 本工程选择 PB10/PB11:
 * PB10 接 MPU6050 SCL
 * PB11 接 MPU6050 SDA
 *
 * 注意:
 * SCL/SDA 必须有上拉电阻。很多 MPU6050 模块已经自带上拉,
 * 如果通信不稳定, 可以外接 4.7k 电阻到 3.3V。
 */

#define MPU6050_GPIO_PORT        GPIOB
#define MPU6050_GPIO_CLK         RCC_APB2Periph_GPIOB
#define MPU6050_SCL_PIN          GPIO_Pin_10
#define MPU6050_SDA_PIN          GPIO_Pin_11

#define MPU6050_ADDR_LOW_WRITE   0xD0
#define MPU6050_ADDR_LOW_READ    0xD1
#define MPU6050_ADDR_HIGH_WRITE  0xD2
#define MPU6050_ADDR_HIGH_READ   0xD3

#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_REG_PWR_MGMT_1   0x6B

#define MPU6050_TASK_MS          20
#define MPU6050_CALIB_SAMPLES    200
#define MPU6050_I2C_DELAY_US     4

#define MPU6050_ACC_SCALE        16384.0f
#define MPU6050_GYRO_SCALE       131.0f
#define MPU6050_FILTER_ALPHA     0.96f

static uint8_t s_status = MPU6050_STATUS_OFFLINE;
static uint8_t s_who_am_i = 0;
static uint8_t s_addr_write = MPU6050_ADDR_LOW_WRITE;
static uint8_t s_addr_read = MPU6050_ADDR_LOW_READ;
static uint32_t s_last_sample_time = 0;
static uint32_t s_last_calib_time = 0;
static int32_t s_gyro_x_offset = 0;
static int32_t s_gyro_y_offset = 0;
static int32_t s_gyro_z_offset = 0;
static int32_t s_calib_sum_x = 0;
static int32_t s_calib_sum_y = 0;
static int32_t s_calib_sum_z = 0;
static uint16_t s_calib_count = 0;
static uint16_t s_calib_valid_count = 0;
static MPU6050_RawData s_raw;
static float s_roll = 0.0f;
static float s_pitch = 0.0f;
static float s_yaw = 0.0f;

/* 设置 SCL 时钟线电平。level=1 输出高电平, level=0 输出低电平。 */
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

/* 设置 SDA 数据线电平。开漏输出模式下, 写 1 等于释放总线, 由上拉电阻拉高。 */
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

/* 读取 SDA 当前电平。读 ACK 或读数据位时会用到。 */
static uint8_t MPU6050_ReadSDA(void)
{
    return (uint8_t)GPIO_ReadInputDataBit(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
}

/* 软件 I2C 的节拍延时。数值越大, I2C 越慢但越容易稳定。 */
static void MPU6050_I2C_Delay(void)
{
    delay_us(MPU6050_I2C_DELAY_US);
}

/*
 * I2C 起始信号 START
 *
 * 总线空闲时 SCL=1, SDA=1。
 * 当 SCL 保持高电平时, SDA 从高变低, 就表示一次 I2C 通信开始。
 *
 * 时序:
 * SDA=1, SCL=1
 * SDA=0
 * SCL=0
 */
static void MPU6050_I2C_Start(void)
{
    MPU6050_SDA(1);
    MPU6050_SCL(1);
    MPU6050_I2C_Delay();
    MPU6050_SDA(0);
    MPU6050_I2C_Delay();
    MPU6050_SCL(0);
}

/*
 * I2C 停止信号 STOP
 *
 * 当 SCL 保持高电平时, SDA 从低变高, 就表示一次 I2C 通信结束。
 *
 * 时序:
 * SDA=0
 * SCL=1
 * SDA=1
 */
static void MPU6050_I2C_Stop(void)
{
    MPU6050_SDA(0);
    MPU6050_SCL(1);
    MPU6050_I2C_Delay();
    MPU6050_SDA(1);
    MPU6050_I2C_Delay();
}

/*
 * 等待从机 ACK
 *
 * STM32 每发送 8 位后, 第 9 个时钟要释放 SDA。
 * 如果 MPU6050 收到了这个字节, 它会把 SDA 拉低, 这就是 ACK。
 *
 * 返回值:
 * 1: 收到 ACK, 说明设备回应了。
 * 0: 没收到 ACK, 可能是接线错误、地址错误、模块没上电或总线异常。
 */
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

/*
 * 主机发送 ACK 或 NACK
 *
 * STM32 读 MPU6050 数据时:
 * - 如果后面还要继续读, 发送 ACK, 表示"我还要下一字节"。
 * - 如果这是最后一个字节, 发送 NACK, 表示"读完了"。
 *
 * 参数 ack:
 * 1: 发送 ACK, SDA 拉低。
 * 0: 发送 NACK, SDA 释放为高。
 */
static void MPU6050_I2C_SendAck(uint8_t ack)
{
    MPU6050_SDA(ack ? 0 : 1);
    MPU6050_I2C_Delay();
    MPU6050_SCL(1);
    MPU6050_I2C_Delay();
    MPU6050_SCL(0);
    MPU6050_SDA(1);
}

/*
 * 通过软件 I2C 写 1 个字节
 *
 * I2C 数据按高位在前发送, 所以从 bit7 开始。
 * 每一位的过程:
 * 1. 先把 SDA 设置成当前数据位。
 * 2. SCL 拉高, 让 MPU6050 读取这一位。
 * 3. SCL 拉低, 准备下一位。
 *
 * 8 位发完后等待 MPU6050 回 ACK。
 */
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

/*
 * 通过软件 I2C 读 1 个字节
 *
 * 每一位的过程:
 * 1. SCL 拉高。
 * 2. STM32 读取 SDA。
 * 3. SCL 拉低, 准备下一位。
 *
 * 8 位读完后, STM32 根据 ack 参数回复 ACK 或 NACK。
 */
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

/*
 * 写 MPU6050 单个寄存器
 *
 * 例如要唤醒 MPU6050:
 * MPU6050_WriteReg(0x6B, 0x00);
 *
 * I2C 顺序:
 * START
 * 发送设备写地址
 * 发送寄存器地址
 * 发送要写入的数据
 * STOP
 */
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

/*
 * 从 MPU6050 连续读取多个寄存器
 *
 * MPU6050 的加速度、温度、陀螺仪数据从 0x3B 开始连续排列。
 * 一次读取 14 字节, 就能拿到:
 * AccX AccY AccZ Temp GyroX GyroY GyroZ
 *
 * I2C 顺序:
 * START
 * 发送设备写地址
 * 发送起始寄存器地址
 * 再发一次 START, 叫重复起始
 * 发送设备读地址
 * 连续读取 len 个字节
 * STOP
 */
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

/* 读取单个寄存器, 本质上就是连续读 1 个字节。 */
static uint8_t MPU6050_ReadReg(uint8_t reg, uint8_t *data)
{
    return MPU6050_ReadRegs(reg, data, 1);
}

static float MPU6050_AbsF(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float MPU6050_SqrtF(float value)
{
    float result;
    uint8_t i;

    if (value <= 0.0f)
    {
        return 0.0f;
    }

    result = value;

    for (i = 0; i < 8; i++)
    {
        result = 0.5f * (result + value / result);
    }

    return result;
}

static float MPU6050_Atan2Deg(float y, float x)
{
    float abs_y;
    float r;
    float angle;

    if ((x == 0.0f) && (y == 0.0f))
    {
        return 0.0f;
    }

    abs_y = MPU6050_AbsF(y) + 0.000001f;

    if (x >= 0.0f)
    {
        r = (x - abs_y) / (x + abs_y);
        angle = 45.0f - (45.0f * r);
    }
    else
    {
        r = (x + abs_y) / (abs_y - x);
        angle = 135.0f - (45.0f * r);
    }

    if (y < 0.0f)
    {
        angle = -angle;
    }

    return angle;
}

static int16_t MPU6050_ToInt16(uint8_t high, uint8_t low)
{
    return (int16_t)(((uint16_t)high << 8) | low);
}

/*
 * 读取 MPU6050 原始数据
 *
 * 从 0x3B 开始连续读 14 字节:
 * 0~1:   加速度 X
 * 2~3:   加速度 Y
 * 4~5:   加速度 Z
 * 6~7:   温度
 * 8~9:   陀螺仪 X
 * 10~11: 陀螺仪 Y
 * 12~13: 陀螺仪 Z
 *
 * MPU6050 每个数据都是 16 位有符号数, 高字节在前, 低字节在后。
 */
static uint8_t MPU6050_ReadRaw(void)
{
    uint8_t data[14];

    if (MPU6050_ReadRegs(MPU6050_REG_ACCEL_XOUT_H, data, 14) == 0)
    {
        return 0;
    }

    s_raw.accel_x = MPU6050_ToInt16(data[0], data[1]);
    s_raw.accel_y = MPU6050_ToInt16(data[2], data[3]);
    s_raw.accel_z = MPU6050_ToInt16(data[4], data[5]);
    s_raw.temp10 = (int16_t)(((int32_t)MPU6050_ToInt16(data[6], data[7]) * 10) / 340 + 365);
    s_raw.gyro_x = MPU6050_ToInt16(data[8], data[9]);
    s_raw.gyro_y = MPU6050_ToInt16(data[10], data[11]);
    s_raw.gyro_z = MPU6050_ToInt16(data[12], data[13]);

    return 1;
}

/*
 * 根据原始数据更新 Roll/Pitch/Yaw
 *
 * 这里使用互补滤波:
 * - 加速度计适合修正 Roll/Pitch 的长期稳定角度。
 * - 陀螺仪适合提供短时间内平滑的角速度积分。
 *
 * Roll/Pitch:
 * angle = 0.96 * (old_angle + gyro * dt) + 0.04 * accel_angle
 *
 * Yaw:
 * MPU6050 没有磁力计, 没有绝对方向参考, 所以这里只能用陀螺仪 Z 轴积分。
 * Yaw 会随时间漂移, 这是正常现象。
 */
static void MPU6050_UpdateAngles(uint32_t now)
{
    float dt;
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
    float roll_acc;
    float pitch_acc;
    float pitch_den;

    if (s_last_sample_time == 0)
    {
        dt = (float)MPU6050_TASK_MS / 1000.0f;
    }
    else
    {
        dt = (float)(now - s_last_sample_time) / 1000.0f;
    }

    s_last_sample_time = now;

    ax = (float)s_raw.accel_x / MPU6050_ACC_SCALE;
    ay = (float)s_raw.accel_y / MPU6050_ACC_SCALE;
    az = (float)s_raw.accel_z / MPU6050_ACC_SCALE;

    gx = (float)((int32_t)s_raw.gyro_x - s_gyro_x_offset) / MPU6050_GYRO_SCALE;
    gy = (float)((int32_t)s_raw.gyro_y - s_gyro_y_offset) / MPU6050_GYRO_SCALE;
    gz = (float)((int32_t)s_raw.gyro_z - s_gyro_z_offset) / MPU6050_GYRO_SCALE;

    roll_acc = MPU6050_Atan2Deg(ay, az);
    pitch_den = MPU6050_SqrtF((ay * ay) + (az * az));
    pitch_acc = MPU6050_Atan2Deg(-ax, pitch_den);

    s_roll = (MPU6050_FILTER_ALPHA * (s_roll + gx * dt)) +
             ((1.0f - MPU6050_FILTER_ALPHA) * roll_acc);
    s_pitch = (MPU6050_FILTER_ALPHA * (s_pitch + gy * dt)) +
              ((1.0f - MPU6050_FILTER_ALPHA) * pitch_acc);
    s_yaw += gz * dt;

    if (s_yaw > 180.0f)
    {
        s_yaw -= 360.0f;
    }
    else if (s_yaw < -180.0f)
    {
        s_yaw += 360.0f;
    }
}

/*
 * 开始陀螺仪零偏校准
 *
 * 陀螺仪静止时理论上应该输出 0, 但实际会有偏差。
 * 校准就是在静止时采集多次 gyro_x/y/z, 求平均值作为 offset。
 *
 * 注意: 校准期间 MPU6050 必须保持静止。
 */
static void MPU6050_StartCalibrate(void)
{
    s_calib_sum_x = 0;
    s_calib_sum_y = 0;
    s_calib_sum_z = 0;
    s_calib_count = 0;
    s_calib_valid_count = 0;
    s_last_calib_time = Timing_GetTick();
    s_status = MPU6050_STATUS_CALIBRATING;
}

/*
 * 非阻塞校准任务
 *
 * 以前如果在 MPU6050_Init 里面一次性 delay+采样 200 次,
 * 开机时会卡住一段时间, 如果模块异常还不利于排查。
 *
 * 现在每次进入 MPU6050_Task 只校准一点点:
 * - 每 3ms 尝试采样一次。
 * - 累计到 200 次后计算零偏。
 * - 校准期间主循环仍然可以刷新 OLED、扫描按键、处理串口。
 */
static void MPU6050_CalibrateTask(uint32_t now)
{
    if (now - s_last_calib_time < 3)
    {
        return;
    }

    s_last_calib_time = now;
    s_calib_count++;

    if (MPU6050_ReadRaw())
    {
        s_calib_sum_x += s_raw.gyro_x;
        s_calib_sum_y += s_raw.gyro_y;
        s_calib_sum_z += s_raw.gyro_z;
        s_calib_valid_count++;
    }

    if (s_calib_count < MPU6050_CALIB_SAMPLES)
    {
        return;
    }

    if (s_calib_valid_count == 0)
    {
        s_status = MPU6050_STATUS_OFFLINE;
        return;
    }

    s_gyro_x_offset = s_calib_sum_x / s_calib_valid_count;
    s_gyro_y_offset = s_calib_sum_y / s_calib_valid_count;
    s_gyro_z_offset = s_calib_sum_z / s_calib_valid_count;

    if (MPU6050_ReadRaw())
    {
        s_roll = MPU6050_Atan2Deg((float)s_raw.accel_y, (float)s_raw.accel_z);
        s_pitch = MPU6050_Atan2Deg(-(float)s_raw.accel_x,
                                   MPU6050_SqrtF(((float)s_raw.accel_y * (float)s_raw.accel_y) +
                                                 ((float)s_raw.accel_z * (float)s_raw.accel_z)));
        s_yaw = 0.0f;
        s_last_sample_time = now;
        s_status = MPU6050_STATUS_READY;
    }
    else
    {
        s_status = MPU6050_STATUS_OFFLINE;
    }
}

/*
 * MPU6050 初始化
 *
 * 这里只做"必须马上完成"的事情:
 * 1. 初始化 PB10/PB11 为开漏输出, 用作软件 I2C。
 * 2. 读取 WHO_AM_I 判断 MPU6050 是否在线。
 * 3. 配置采样率、低通滤波、陀螺仪量程、加速度量程。
 * 4. 启动非阻塞校准。
 *
 * 不在这里做长时间 delay, 避免开机卡死。
 */
void MPU6050_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(MPU6050_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = MPU6050_SCL_PIN | MPU6050_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU6050_GPIO_PORT, &GPIO_InitStructure);

    MPU6050_SCL(1);
    MPU6050_SDA(1);

    s_status = MPU6050_STATUS_OFFLINE;
    s_who_am_i = 0;

    s_addr_write = MPU6050_ADDR_LOW_WRITE;
    s_addr_read = MPU6050_ADDR_LOW_READ;

    if (MPU6050_ReadReg(MPU6050_REG_WHO_AM_I, &s_who_am_i) == 0)
    {
        s_addr_write = MPU6050_ADDR_HIGH_WRITE;
        s_addr_read = MPU6050_ADDR_HIGH_READ;

        if (MPU6050_ReadReg(MPU6050_REG_WHO_AM_I, &s_who_am_i) == 0)
        {
            return;
        }
    }

    if ((s_who_am_i != 0x68) && (s_who_am_i != 0x69))
    {
        return;
    }

    MPU6050_WriteReg(MPU6050_REG_PWR_MGMT_1, 0x00);
    MPU6050_WriteReg(MPU6050_REG_SMPLRT_DIV, 0x09);
    MPU6050_WriteReg(MPU6050_REG_CONFIG, 0x03);
    MPU6050_WriteReg(MPU6050_REG_GYRO_CONFIG, 0x00);
    MPU6050_WriteReg(MPU6050_REG_ACCEL_CONFIG, 0x00);
    MPU6050_StartCalibrate();
}

/*
 * MPU6050 周期任务
 *
 * main.c 的 while(1) 会一直调用这个函数。
 * 如果正在校准, 就执行校准任务。
 * 如果已经准备好, 就每 20ms 读取一次数据并更新角度。
 * 如果 MPU6050 离线, 直接返回, 不影响其它模块运行。
 */
void MPU6050_Task(void)
{
    uint32_t now;

    now = Timing_GetTick();

    if (s_status == MPU6050_STATUS_CALIBRATING)
    {
        MPU6050_CalibrateTask(now);
        return;
    }

    if (s_status != MPU6050_STATUS_READY)
    {
        return;
    }

    if (now - s_last_sample_time < MPU6050_TASK_MS)
    {
        return;
    }

    if (MPU6050_ReadRaw())
    {
        MPU6050_UpdateAngles(now);
    }
    else
    {
        s_status = MPU6050_STATUS_OFFLINE;
    }
}

uint8_t MPU6050_IsOnline(void)
{
    return (s_status == MPU6050_STATUS_READY) ? 1 : 0;
}

uint8_t MPU6050_GetStatus(void)
{
    return s_status;
}

uint8_t MPU6050_GetWhoAmI(void)
{
    return s_who_am_i;
}

int16_t MPU6050_GetRoll10(void)
{
    return (int16_t)(s_roll * 10.0f);
}

int16_t MPU6050_GetPitch10(void)
{
    return (int16_t)(s_pitch * 10.0f);
}

int16_t MPU6050_GetYaw10(void)
{
    return (int16_t)(s_yaw * 10.0f);
}

int16_t MPU6050_GetTemp10(void)
{
    return s_raw.temp10;
}

void MPU6050_GetRawData(MPU6050_RawData *raw)
{
    if (raw == 0)
    {
        return;
    }

    *raw = s_raw;
}
