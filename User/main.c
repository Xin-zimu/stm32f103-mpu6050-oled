#include "stm32f10x.h"
#include "led.h"
#include "timing.h"
#include "light_sensor.h"
#include "oled.h"
#include "ds18b20.h"
#include "delay.h"
#include "app_light.h"
#include "app_temp.h"
#include "joystick.h"
#include "app_ui.h"
#include "usart.h"
#include "app_uart_practice.h"
#include "mpu6050.h"
#include "app_attitude_stream.h"

#define APP_UART_PRACTICE_ENABLE  0

int main(void)
{
    delay_init();

    LED_Init();
    Timing_Init();
    uart_init(115200);

    LightSensor_Init();
    LightSensor_ADC_Init();

    OLED_Init();
    DS18B20_Init();
    Joystick_Init();
    MPU6050_Init();

    OLED_Clear();

    App_Light_Init();
    App_Temp_Init();
    App_UI_Init();
#if APP_UART_PRACTICE_ENABLE
    App_UARTPractice_Init();
#endif
    App_AttitudeStream_Init();

    while (1)
    {
        App_Light_Task();
        App_Temp_Task();
        MPU6050_Task();
        App_AttitudeStream_Task();
        Joystick_Task();
        App_UI_Task();
#if APP_UART_PRACTICE_ENABLE
        App_UARTPractice_Task();
#endif
    }
}
