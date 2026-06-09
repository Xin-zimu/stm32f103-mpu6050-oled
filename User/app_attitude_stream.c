#include "app_attitude_stream.h"
#include "timing.h"
#include "mpu6050.h"
#include "uart_tx.h"

#define ATT_STREAM_PERIOD_MS  50
#define ATT_STATUS_PERIOD_MS  500
#define ATT_FRAME_MAX_LEN     32

static uint32_t s_last_send_time = 0;
static uint32_t s_last_status_time = 0;

static uint16_t App_AttitudeStream_AppendText(uint8_t *frame,
                                              uint16_t pos,
                                              const char *text)
{
    while (*text != '\0')
    {
        frame[pos++] = (uint8_t)*text;
        text++;
    }

    return pos;
}

static uint16_t App_AttitudeStream_AppendInt16(uint8_t *frame,
                                               uint16_t pos,
                                               int16_t value)
{
    uint8_t digits[5];
    uint8_t count;
    int32_t magnitude;

    magnitude = value;
    if (magnitude < 0)
    {
        frame[pos++] = '-';
        magnitude = -magnitude;
    }

    count = 0;

    do
    {
        digits[count++] = (uint8_t)('0' + (magnitude % 10));
        magnitude /= 10;
    }
    while (magnitude > 0);

    while (count > 0)
    {
        frame[pos++] = digits[--count];
    }

    return pos;
}

static void App_AttitudeStream_SendStatus(const char *status)
{
    uint8_t frame[12];
    uint16_t len;

    len = App_AttitudeStream_AppendText(frame, 0, status);
    (void)UartTx_TryWrite(frame, len);
}

static void App_AttitudeStream_SendAttitude(void)
{
    uint8_t frame[ATT_FRAME_MAX_LEN];
    uint16_t len;

    len = App_AttitudeStream_AppendText(frame, 0, "ATT,");
    len = App_AttitudeStream_AppendInt16(frame, len, MPU6050_GetRoll10());
    frame[len++] = ',';
    len = App_AttitudeStream_AppendInt16(frame, len, MPU6050_GetPitch10());
    frame[len++] = ',';
    len = App_AttitudeStream_AppendInt16(frame, len, MPU6050_GetYaw10());
    frame[len++] = '\r';
    frame[len++] = '\n';

    (void)UartTx_TryWrite(frame, len);
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
                App_AttitudeStream_SendStatus("STA,CAL\r\n");
            }
            else
            {
                App_AttitudeStream_SendStatus("STA,OFF\r\n");
            }
        }

        return;
    }

    if (now - s_last_send_time < ATT_STREAM_PERIOD_MS)
    {
        return;
    }

    s_last_send_time = now;

    App_AttitudeStream_SendAttitude();
}
