#include "app_attitude_stream.h"
#include "timing.h"
#include "mpu6050.h"

#define ATT_STREAM_PERIOD_MS  50
#define ATT_STATUS_PERIOD_MS  500
#define ATT_TX_TIMEOUT        100000u

static uint32_t s_last_send_time = 0;
static uint32_t s_last_status_time = 0;

static void App_AttitudeStream_SendChar(char ch)
{
    uint32_t timeout;

    timeout = ATT_TX_TIMEOUT;

    while ((USART1->SR & 0X40) == 0)
    {
        if (timeout == 0)
        {
            return;
        }

        timeout--;
    }

    USART1->DR = (uint8_t)ch;
}

static void App_AttitudeStream_SendString(const char *text)
{
    while (*text != '\0')
    {
        App_AttitudeStream_SendChar(*text);
        text++;
    }
}

static void App_AttitudeStream_SendUInt(uint16_t value)
{
    char buf[5];
    uint8_t pos;

    pos = 0;

    if (value == 0)
    {
        App_AttitudeStream_SendChar('0');
        return;
    }

    while ((value > 0) && (pos < sizeof(buf)))
    {
        buf[pos] = (char)('0' + (value % 10u));
        value /= 10u;
        pos++;
    }

    while (pos > 0)
    {
        pos--;
        App_AttitudeStream_SendChar(buf[pos]);
    }
}

static void App_AttitudeStream_SendAngle(int16_t angle10)
{
    uint16_t value;
    uint8_t decimal;

    if (angle10 < 0)
    {
        App_AttitudeStream_SendChar('-');
        value = (uint16_t)(0 - angle10);
    }
    else
    {
        value = (uint16_t)angle10;
    }

    decimal = (uint8_t)(value % 10u);

    App_AttitudeStream_SendUInt(value / 10u);
    App_AttitudeStream_SendChar('.');
    App_AttitudeStream_SendChar((char)('0' + decimal));
    App_AttitudeStream_SendChar('0');
}

void App_AttitudeStream_Init(void)
{
    s_last_send_time = Timing_GetTick();
    s_last_status_time = s_last_send_time;
}

void App_AttitudeStream_Task(void)
{
    uint32_t now;
    uint8_t status;

    now = Timing_GetTick();
    status = MPU6050_GetStatus();

    if (status != MPU6050_STATUS_READY)
    {
        if (now - s_last_status_time >= ATT_STATUS_PERIOD_MS)
        {
            s_last_status_time = now;

            if (status == MPU6050_STATUS_CALIBRATING)
            {
                App_AttitudeStream_SendString("STA,CAL\r\n");
            }
            else
            {
                App_AttitudeStream_SendString("STA,OFF\r\n");
            }
        }

        return;
    }

    if (now - s_last_send_time < ATT_STREAM_PERIOD_MS)
    {
        return;
    }

    s_last_send_time = now;

    App_AttitudeStream_SendString("ATT,");
    App_AttitudeStream_SendAngle(MPU6050_GetRoll10());
    App_AttitudeStream_SendChar(',');
    App_AttitudeStream_SendAngle(MPU6050_GetPitch10());
    App_AttitudeStream_SendChar(',');
    App_AttitudeStream_SendAngle(MPU6050_GetYaw10());
    App_AttitudeStream_SendString("\r\n");
}
