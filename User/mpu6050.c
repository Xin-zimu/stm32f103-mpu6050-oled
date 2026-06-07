#include "mpu6050.h"
#include "timing.h"

/*
 * MPU6050 使用 STM32 硬件 I2C2。
 * PB10 -> I2C2_SCL
 * PB11 -> I2C2_SDA
 */

#define MPU6050_GPIO_PORT        GPIOB
#define MPU6050_GPIO_CLK         RCC_APB2Periph_GPIOB
#define MPU6050_SCL_PIN          GPIO_Pin_10
#define MPU6050_SDA_PIN          GPIO_Pin_11


#define MPU6050_I2C              I2C2
#define MPU6050_I2C_CLK          RCC_APB1Periph_I2C2
#define MPU6050_I2C_SPEED        100000
#define MPU6050_I2C_TIMEOUT      10000
#define MPU6050_I2C_ERROR_MASK   (I2C_SR1_BERR | I2C_SR1_ARLO | \
                                  I2C_SR1_AF | I2C_SR1_OVR)

#define MPU6050_ADDR_LOW         0xD0
#define MPU6050_ADDR_HIGH        0xD2

#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_REG_PWR_MGMT_1   0x6B

#define MPU6050_TASK_MS          20
#define MPU6050_CALIB_SAMPLES    200

#define MPU6050_ACC_SCALE        16384.0f
#define MPU6050_GYRO_SCALE       131.0f
#define MPU6050_FILTER_ALPHA     0.96f

static uint8_t s_status = MPU6050_STATUS_OFFLINE;
static uint8_t s_who_am_i = 0;
static uint8_t s_address = MPU6050_ADDR_LOW;
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


static uint8_t MPU6050_I2C_WaitEvent(uint32_t event)
{
    uint32_t timeout;

    timeout = MPU6050_I2C_TIMEOUT;

    while (I2C_CheckEvent(MPU6050_I2C, event) != SUCCESS)
    {
        if ((MPU6050_I2C->SR1 & MPU6050_I2C_ERROR_MASK) != 0)
        {
            return 0;
        }

        if (timeout == 0)
        {
            return 0;
        }

        timeout--;
    }

    return 1;
}

static uint8_t MPU6050_I2C_WaitFlagSet(uint32_t flag)
{
    uint32_t timeout;

    timeout = MPU6050_I2C_TIMEOUT;

    while (I2C_GetFlagStatus(MPU6050_I2C, flag) == RESET)
    {
        if ((MPU6050_I2C->SR1 & MPU6050_I2C_ERROR_MASK) != 0)
        {
            return 0;
        }

        if (timeout == 0)
        {
            return 0;
        }

        timeout--;
    }

    return 1;
}

static uint8_t MPU6050_I2C_WaitNotBusy(void)
{
    uint32_t timeout;

    timeout = MPU6050_I2C_TIMEOUT;

    while (I2C_GetFlagStatus(MPU6050_I2C, I2C_FLAG_BUSY) == SET)
    {
        if (timeout == 0)
        {
            return 0;
        }

        timeout--;
    }

    return 1;
}

static uint8_t MPU6050_I2C_WaitStopCleared(void)
{
    uint32_t timeout;

    timeout = MPU6050_I2C_TIMEOUT;

    while ((MPU6050_I2C->CR1 & I2C_CR1_STOP) != 0)
    {
        if (timeout == 0)
        {
            return 0;
        }

        timeout--;
    }

    return 1;
}

static void MPU6050_I2C_ClearErrors(void)
{
    MPU6050_I2C->SR1 &= (uint16_t)~MPU6050_I2C_ERROR_MASK;
}

static void MPU6050_I2C_Abort(void)
{
    I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
    I2C_AcknowledgeConfig(MPU6050_I2C, ENABLE);
    I2C_NACKPositionConfig(MPU6050_I2C, I2C_NACKPosition_Current);
    MPU6050_I2C_ClearErrors();
}

static void MPU6050_BusDelay(void)
{
    volatile uint16_t delay;

    for (delay = 0; delay < 100; delay++)
    {
    }
}

static void MPU6050_BusRecover(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t pulse;

    GPIO_InitStructure.GPIO_Pin = MPU6050_SCL_PIN | MPU6050_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU6050_GPIO_PORT, &GPIO_InitStructure);

    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN | MPU6050_SDA_PIN);
    MPU6050_BusDelay();

    for (pulse = 0; pulse < 9; pulse++)
    {
        if (GPIO_ReadInputDataBit(MPU6050_GPIO_PORT, MPU6050_SDA_PIN) != Bit_RESET)
        {
            break;
        }

        GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
        MPU6050_BusDelay();
        GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
        MPU6050_BusDelay();
    }

    GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    MPU6050_BusDelay();
    GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
    MPU6050_BusDelay();
    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
    MPU6050_BusDelay();
    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    MPU6050_BusDelay();
}

static void MPU6050_BusInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef I2C_InitStructure;

    RCC_APB2PeriphClockCmd(MPU6050_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(MPU6050_I2C_CLK, ENABLE);

    I2C_Cmd(MPU6050_I2C, DISABLE);
    MPU6050_BusRecover();

    GPIO_InitStructure.GPIO_Pin = MPU6050_SCL_PIN | MPU6050_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU6050_GPIO_PORT, &GPIO_InitStructure);

    I2C_DeInit(MPU6050_I2C);
    I2C_SoftwareResetCmd(MPU6050_I2C, ENABLE);
    I2C_SoftwareResetCmd(MPU6050_I2C, DISABLE);

    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = MPU6050_I2C_SPEED;

    I2C_Init(MPU6050_I2C, &I2C_InitStructure);
    I2C_Cmd(MPU6050_I2C, ENABLE);
}

static uint8_t MPU6050_WriteReg(uint8_t reg, uint8_t data)
{
    if (MPU6050_I2C_WaitNotBusy() == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_GenerateSTART(MPU6050_I2C, ENABLE);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_Send7bitAddress(MPU6050_I2C, s_address, I2C_Direction_Transmitter);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_SendData(MPU6050_I2C, reg);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_SendData(MPU6050_I2C, data);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
    return 1;
}

static uint8_t MPU6050_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t remaining;
    uint8_t index;
    uint32_t primask;

    if ((buf == 0) || (len == 0))
    {
        return 0;
    }

    if (MPU6050_I2C_WaitNotBusy() == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_AcknowledgeConfig(MPU6050_I2C, ENABLE);

    if (len == 2)
    {
        I2C_NACKPositionConfig(MPU6050_I2C, I2C_NACKPosition_Next);
    }
    else
    {
        I2C_NACKPositionConfig(MPU6050_I2C, I2C_NACKPosition_Current);
    }

    I2C_GenerateSTART(MPU6050_I2C, ENABLE);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_Send7bitAddress(MPU6050_I2C, s_address, I2C_Direction_Transmitter);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_SendData(MPU6050_I2C, reg);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_GenerateSTART(MPU6050_I2C, ENABLE);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_Send7bitAddress(MPU6050_I2C, s_address, I2C_Direction_Receiver);

    /*
     * 此处不能使用 I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED。
     * I2C_CheckEvent() 会读取 SR2 并清除 ADDR，而 STM32F1 在接收单字节
     * 或双字节时，必须先配置 ACK、POS 和 STOP，再清除 ADDR。
     */
    if (MPU6050_I2C_WaitFlagSet(I2C_FLAG_ADDR) == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    if (len == 1)
    {
        primask = __get_PRIMASK();
        __disable_irq();
        I2C_AcknowledgeConfig(MPU6050_I2C, DISABLE);
        (void)MPU6050_I2C->SR1;
        (void)MPU6050_I2C->SR2;
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        __set_PRIMASK(primask);

        if (MPU6050_I2C_WaitFlagSet(I2C_FLAG_RXNE) == 0)
        {
            MPU6050_I2C_Abort();
            return 0;
        }

        buf[0] = I2C_ReceiveData(MPU6050_I2C);
    }
    else if (len == 2)
    {
        primask = __get_PRIMASK();
        __disable_irq();
        (void)MPU6050_I2C->SR1;
        (void)MPU6050_I2C->SR2;
        I2C_AcknowledgeConfig(MPU6050_I2C, DISABLE);
        __set_PRIMASK(primask);

        if (MPU6050_I2C_WaitFlagSet(I2C_FLAG_BTF) == 0)
        {
            MPU6050_I2C_Abort();
            return 0;
        }

        primask = __get_PRIMASK();
        __disable_irq();
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        buf[0] = I2C_ReceiveData(MPU6050_I2C);
        buf[1] = I2C_ReceiveData(MPU6050_I2C);
        __set_PRIMASK(primask);
    }
    else
    {
        (void)MPU6050_I2C->SR1;
        (void)MPU6050_I2C->SR2;
        remaining = len;
        index = 0;

        while (remaining > 3)
        {
            if (MPU6050_I2C_WaitFlagSet(I2C_FLAG_RXNE) == 0)
            {
                MPU6050_I2C_Abort();
                return 0;
            }

            buf[index++] = I2C_ReceiveData(MPU6050_I2C);
            remaining--;
        }

        if (MPU6050_I2C_WaitFlagSet(I2C_FLAG_BTF) == 0)
        {
            MPU6050_I2C_Abort();
            return 0;
        }

        primask = __get_PRIMASK();
        __disable_irq();
        I2C_AcknowledgeConfig(MPU6050_I2C, DISABLE);
        buf[index++] = I2C_ReceiveData(MPU6050_I2C);

        if (MPU6050_I2C_WaitFlagSet(I2C_FLAG_BTF) == 0)
        {
            __set_PRIMASK(primask);
            MPU6050_I2C_Abort();
            return 0;
        }

        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        buf[index++] = I2C_ReceiveData(MPU6050_I2C);
        __set_PRIMASK(primask);
        buf[index] = I2C_ReceiveData(MPU6050_I2C);
    }

    if (MPU6050_I2C_WaitStopCleared() == 0)
    {
        MPU6050_I2C_Abort();
        return 0;
    }

    I2C_AcknowledgeConfig(MPU6050_I2C, ENABLE);
    I2C_NACKPositionConfig(MPU6050_I2C, I2C_NACKPosition_Current);
    return 1;
}

static uint8_t MPU6050_ReadReg(uint8_t reg, uint8_t *data)
{
    return MPU6050_ReadRegs(reg, data, 1);
}

static float MPU6050_AbsF(float value)
{
    return (value < 0.0f) ? -value : value;
}

/* 使用牛顿迭代法计算浮点数平方根。 */
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
 * 读取 MPU6050 原始数据。
 *
 * 从 0x3B 开始连续读取 14 字节：
 * 0~1:   加速度 X
 * 2~3:   加速度 Y
 * 4~5:   加速度 Z
 * 6~7:   温度
 * 8~9:   陀螺仪 X
 * 10~11: 陀螺仪 Y
 * 12~13: 陀螺仪 Z
 *
 * 每项数据都是 16 位有符号数，高字节在前，低字节在后。
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
 * 根据原始数据更新 Roll、Pitch 和 Yaw。
 *
 * Roll 和 Pitch 使用互补滤波：
 * - 加速度计提供长期稳定的倾角参考。
 * - 陀螺仪提供短时间内平滑的角速度积分。
 *
 * Roll/Pitch:
 * angle = 0.96 * (old_angle + gyro * dt) + 0.04 * accel_angle
 *
 * Yaw:
 * MPU6050 没有磁力计，无法提供绝对航向参考，因此这里只积分陀螺仪 Z 轴。
 * Yaw 会随时间漂移，这是正常现象。
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
 * 开始陀螺仪零偏校准。
 *
 * 传感器静止时角速度理论上应接近 0，但实际存在零偏。
 * 校准期间采集 gyro_x、gyro_y 和 gyro_z，并将平均值作为偏移量。
 *
 * 注意：校准期间 MPU6050 必须保持静止。
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
 * 非阻塞校准任务。
 *
 * 不在 MPU6050_Init 中延时并连续采样 200 次，避免初始化阶段长时间阻塞。
 *
 * 每次进入 MPU6050_Task 最多校准一个样本：
 * - 每 3ms 尝试采样一次。
 * - 累计 200 次后计算零偏。
 * - 校准期间主循环仍可刷新 OLED、扫描按键并处理串口。
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
 * 通过硬件 I2C2 初始化 MPU6050。
 * PB10 和 PB11 配置为复用开漏输出。
 */
void MPU6050_Init(void)
{
    MPU6050_BusInit();

    s_status = MPU6050_STATUS_OFFLINE;
    s_who_am_i = 0;

    s_address = MPU6050_ADDR_LOW;

    if (MPU6050_ReadReg(MPU6050_REG_WHO_AM_I, &s_who_am_i) == 0)
    {
        s_address = MPU6050_ADDR_HIGH;

        if (MPU6050_ReadReg(MPU6050_REG_WHO_AM_I, &s_who_am_i) == 0)
        {
            return;
        }
    }

    if ((s_who_am_i != 0x68) && (s_who_am_i != 0x69))
    {
        return;
    }

    if ((MPU6050_WriteReg(MPU6050_REG_PWR_MGMT_1, 0x00) == 0) ||
        (MPU6050_WriteReg(MPU6050_REG_SMPLRT_DIV, 0x09) == 0) ||
        (MPU6050_WriteReg(MPU6050_REG_CONFIG, 0x03) == 0) ||
        (MPU6050_WriteReg(MPU6050_REG_GYRO_CONFIG, 0x00) == 0) ||
        (MPU6050_WriteReg(MPU6050_REG_ACCEL_CONFIG, 0x00) == 0))
    {
        return;
    }

    MPU6050_StartCalibrate();
}

/*
 * MPU6050 周期任务。
 *
 * main.c 在 while(1) 中持续调用本函数。
 * 正在校准时执行校准任务；准备完成后每 20ms 读取数据并更新角度。
 * 如果 MPU6050 离线则直接返回，不影响其他模块运行。
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
