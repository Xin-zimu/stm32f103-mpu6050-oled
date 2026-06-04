#include "mpu6050.h"
#include "timing.h"

/*
 * MPU6050 uses STM32 hardware I2C2.
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


static uint8_t MPU6050_I2C_WaitEvent(uint32_t event)
{
    uint32_t timeout;

    timeout = MPU6050_I2C_TIMEOUT;

    while (I2C_CheckEvent(MPU6050_I2C, event) != SUCCESS)
    {
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

static void MPU6050_BusInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef I2C_InitStructure;

    RCC_APB2PeriphClockCmd(MPU6050_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(MPU6050_I2C_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = MPU6050_SCL_PIN | MPU6050_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU6050_GPIO_PORT, &GPIO_InitStructure);

    I2C_DeInit(MPU6050_I2C);

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
        return 0;
    }

    I2C_GenerateSTART(MPU6050_I2C, ENABLE);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_Send7bitAddress(MPU6050_I2C, s_addr_write, I2C_Direction_Transmitter);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_SendData(MPU6050_I2C, reg);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_SendData(MPU6050_I2C, data);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
    return 1;
}

static uint8_t MPU6050_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    if ((buf == 0) || (len == 0))
    {
        return 0;
    }

    if (MPU6050_I2C_WaitNotBusy() == 0)
    {
        return 0;
    }

    I2C_AcknowledgeConfig(MPU6050_I2C, ENABLE);
    I2C_GenerateSTART(MPU6050_I2C, ENABLE);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_Send7bitAddress(MPU6050_I2C, s_addr_write, I2C_Direction_Transmitter);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_SendData(MPU6050_I2C, reg);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_GenerateSTART(MPU6050_I2C, ENABLE);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    I2C_Send7bitAddress(MPU6050_I2C, s_addr_read, I2C_Direction_Receiver);

    if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) == 0)
    {
        I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        if (i == (uint8_t)(len - 1u))
        {
            I2C_AcknowledgeConfig(MPU6050_I2C, DISABLE);
            I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
        }

        if (MPU6050_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_RECEIVED) == 0)
        {
            I2C_GenerateSTOP(MPU6050_I2C, ENABLE);
            I2C_AcknowledgeConfig(MPU6050_I2C, ENABLE);
            return 0;
        }

        buf[i] = I2C_ReceiveData(MPU6050_I2C);
    }

    I2C_AcknowledgeConfig(MPU6050_I2C, ENABLE);
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

//���㸡����ƽ����
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
 * ��ȡ MPU6050 ԭʼ����
 *
 * �� 0x3B ��ʼ������ 14 �ֽ�:
 * 0~1:   ���ٶ� X
 * 2~3:   ���ٶ� Y
 * 4~5:   ���ٶ� Z
 * 6~7:   �¶�
 * 8~9:   ������ X
 * 10~11: ������ Y
 * 12~13: ������ Z
 *
 * MPU6050 ÿ�����ݶ��� 16 λ�з�����, ���ֽ���ǰ, ���ֽ��ں�
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
 * ����ԭʼ���ݸ��� Roll/Pitch/Yaw
 *
 * ����ʹ�û����˲�:
 * - ���ٶȼ��ʺ����� Roll/Pitch �ĳ����ȶ��Ƕȡ�
 * - �������ʺ��ṩ��ʱ����ƽ���Ľ��ٶȻ��֡�
 *
 * Roll/Pitch:
 * angle = 0.96 * (old_angle + gyro * dt) + 0.04 * accel_angle
 *
 * Yaw:
 * MPU6050 û�д�����, û�о��Է���ο�, ��������ֻ���������� Z ����֡�
 * Yaw ����ʱ��Ư��, ������������
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
 * ��ʼ��������ƫУ׼
 *
 * �����Ǿ�ֹʱ������Ӧ����� 0, ��ʵ�ʻ���ƫ�
 * У׼�����ھ�ֹʱ�ɼ���� gyro_x/y/z, ��ƽ��ֵ��Ϊ offset��
 *
 * ע��: У׼�ڼ� MPU6050 ���뱣�־�ֹ��
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
 * ������У׼����
 *
 * ��ǰ����� MPU6050_Init ����һ���� delay+���� 200 ��,
 * ����ʱ�Ῠסһ��ʱ��, ���ģ���쳣���������Ų顣
 *
 * ����ÿ�ν��� MPU6050_Task ֻУ׼һ���:
 * - ÿ 3ms ���Բ���һ�Ρ�
 * - �ۼƵ� 200 �κ������ƫ��
 * - У׼�ڼ���ѭ����Ȼ����ˢ�� OLED��ɨ�谴�����������ڡ�
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
 * Initialize MPU6050 through hardware I2C2.
 * PB10/PB11 are configured as alternate-function open-drain pins.
 */
void MPU6050_Init(void)
{
    MPU6050_BusInit();

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
 * MPU6050 ��������
 *
 * main.c �� while(1) ��һֱ�������������
 * �������У׼, ��ִ��У׼����
 * ����Ѿ�׼����, ��ÿ 20ms ��ȡһ�����ݲ����½Ƕȡ�
 * ��� MPU6050 ����, ֱ�ӷ���, ��Ӱ������ģ�����С�
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
