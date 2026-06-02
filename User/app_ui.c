#include "app_ui.h"
#include "oled.h"
#include "timing.h"
#include "joystick.h"
#include "app_light.h"
#include "app_temp.h"
#include "led.h"
#include "mpu6050.h"

#define UI_STATUS_REFRESH_MS    500
#define UI_CURSOR_BLINK_MS      500
#define UI_THRESHOLD_STEP       50

#define UI_SCREEN_MENU          0
#define UI_SCREEN_STATUS        1
#define UI_SCREEN_THRESHOLD     2
#define UI_SCREEN_TRAFFIC       3
#define UI_SCREEN_MPU           4

#define UI_TRAFFIC_AUTO         0
#define UI_TRAFFIC_RED          1
#define UI_TRAFFIC_YELLOW       2
#define UI_TRAFFIC_GREEN        3
#define UI_TRAFFIC_OFF          4

static uint8_t s_screen = UI_SCREEN_MENU;
static uint8_t s_menu_index = 0;
static uint8_t s_traffic_index = UI_TRAFFIC_AUTO;
static uint8_t s_cursor_visible = 1;
static uint8_t s_screen_dirty = 1;
static uint8_t s_status_dirty = 1;
static uint8_t s_cursor_dirty = 1;
static uint32_t s_last_status_time = 0;
static uint32_t s_last_cursor_time = 0;

static void UI_ClearLine(uint8_t page)
{
    OLED_ClearPage(page);
}

static void UI_ShowValue4(uint8_t page, uint8_t column, uint16_t value)
{
    OLED_ShowString(page, column, "    ");
    OLED_ShowNum(page, column, value, 4);
}

static void UI_ShowTemp10(uint8_t page, uint8_t column)
{
    int16_t temp10;
    uint16_t value;
    uint16_t integer;
    uint8_t decimal;
    uint8_t pos;

    OLED_ShowString(page, column, "       ");

    if (App_Temp_IsValid() == 0)
    {
        OLED_ShowString(page, column, "----");
        return;
    }

    temp10 = App_Temp_GetTemp10();
    pos = column;

    if (temp10 < 0)
    {
        OLED_ShowChar(page, pos, '-');
        pos += 6;
        value = (uint16_t)(0 - temp10);
    }
    else
    {
        value = (uint16_t)temp10;
    }

    integer = value / 10;
    decimal = (uint8_t)(value % 10);

    if (integer >= 100)
    {
        OLED_ShowNum(page, pos, integer, 3);
        pos += 18;
    }
    else if (integer >= 10)
    {
        OLED_ShowNum(page, pos, integer, 2);
        pos += 12;
    }
    else
    {
        OLED_ShowNum(page, pos, integer, 1);
        pos += 6;
    }

    OLED_ShowChar(page, pos, '.');
    pos += 6;
    OLED_ShowNum(page, pos, decimal, 1);
    pos += 6;
    OLED_ShowChar(page, pos, 'C');
}

static void UI_ShowSignedDeg(uint8_t page, uint8_t column, int16_t angle10, uint8_t show_decimal)
{
    uint16_t value;
    uint16_t integer;
    uint8_t decimal;
    uint8_t pos;

    if (show_decimal)
    {
        OLED_ShowString(page, column, "       ");
    }
    else
    {
        OLED_ShowString(page, column, "    ");
    }

    pos = column;

    if (angle10 < 0)
    {
        OLED_ShowChar(page, pos, '-');
        value = (uint16_t)(0 - angle10);
    }
    else
    {
        OLED_ShowChar(page, pos, '+');
        value = (uint16_t)angle10;
    }

    pos += 6;
    integer = value / 10;
    decimal = (uint8_t)(value % 10);

    if (integer > 999)
    {
        integer = 999;
    }

    OLED_ShowNum(page, pos, integer, 3);
    pos += 18;

    if (show_decimal)
    {
        OLED_ShowChar(page, pos, '.');
        pos += 6;
        OLED_ShowNum(page, pos, decimal, 1);
    }
}

static void UI_ShowMpuStatus(uint8_t page, uint8_t column)
{
    OLED_ShowString(page, column, "     ");

    if (MPU6050_GetStatus() == MPU6050_STATUS_READY)
    {
        OLED_ShowString(page, column, "OK");
    }
    else if (MPU6050_GetStatus() == MPU6050_STATUS_CALIBRATING)
    {
        OLED_ShowString(page, column, "CAL");
    }
    else
    {
        OLED_ShowString(page, column, "OFF");
    }
}

static void UI_ShowMpuTemp10(uint8_t page, uint8_t column)
{
    int16_t temp10;
    uint16_t value;
    uint16_t integer;
    uint8_t decimal;
    uint8_t pos;

    OLED_ShowString(page, column, "       ");

    if (MPU6050_IsOnline() == 0)
    {
        OLED_ShowString(page, column, "----");
        return;
    }

    temp10 = MPU6050_GetTemp10();
    pos = column;

    if (temp10 < 0)
    {
        OLED_ShowChar(page, pos, '-');
        pos += 6;
        value = (uint16_t)(0 - temp10);
    }
    else
    {
        value = (uint16_t)temp10;
    }

    integer = value / 10;
    decimal = (uint8_t)(value % 10);

    OLED_ShowNum(page, pos, integer, 2);
    pos += 12;
    OLED_ShowChar(page, pos, '.');
    pos += 6;
    OLED_ShowNum(page, pos, decimal, 1);
    pos += 6;
    OLED_ShowChar(page, pos, 'C');
}

static void UI_RenderStatusArea(void)
{
    OLED_ShowString(0, 0, "MPU:");
    UI_ShowMpuStatus(0, 24);
    OLED_ShowString(0, 48, "R:");
    UI_ShowSignedDeg(0, 60, MPU6050_GetRoll10(), 0);

    OLED_ShowString(1, 0, "P:");
    UI_ShowSignedDeg(1, 12, MPU6050_GetPitch10(), 0);
    OLED_ShowString(1, 48, "Y:");
    UI_ShowSignedDeg(1, 60, MPU6050_GetYaw10(), 0);

    OLED_ShowString(2, 0, "LIGHT:");
    OLED_ShowString(2, 42, "      ");

    if (App_Light_IsDark())
    {
        OLED_ShowString(2, 42, "DARK");
    }
    else
    {
        OLED_ShowString(2, 42, "BRIGHT");
    }

    OLED_ShowString(3, 0, "AO:");
    UI_ShowValue4(3, 18, App_Light_GetAO());
    OLED_ShowString(3, 54, "T:");
    UI_ShowTemp10(3, 66);
}

static void UI_RenderMenuItems(void)
{
    const char *items[4];
    uint8_t i;
    uint8_t page;

    items[0] = "STATUS";
    items[1] = "SET TH";
    items[2] = "TRAFFIC";
    items[3] = "MPU6050";

    for (i = 0; i < 4; i++)
    {
        page = 4 + i;
        UI_ClearLine(page);

        if ((i == s_menu_index) && s_cursor_visible)
        {
            OLED_ShowChar(page, 0, '>');
        }
        else
        {
            OLED_ShowChar(page, 0, ' ');
        }

        OLED_ShowString(page, 12, items[i]);
    }
}

static void UI_RenderMenuScreen(void)
{
    OLED_Clear();
    UI_RenderStatusArea();
    UI_RenderMenuItems();
}

static void UI_RenderStatusScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "STATUS");
    OLED_ShowString(2, 0, "LIGHT:");
    OLED_ShowString(3, 0, "AO:");
    OLED_ShowString(4, 0, "TEMP:");
    OLED_ShowString(5, 0, "MPU:");
    OLED_ShowString(6, 0, "R:");
    OLED_ShowString(6, 54, "P:");
    OLED_ShowString(7, 0, "PRESS BACK");
}

static void UI_RenderStatusPageData(void)
{
    OLED_ShowString(2, 42, "      ");

    if (App_Light_IsDark())
    {
        OLED_ShowString(2, 42, "DARK");
    }
    else
    {
        OLED_ShowString(2, 42, "BRIGHT");
    }

    UI_ShowValue4(3, 18, App_Light_GetAO());
    UI_ShowTemp10(4, 30);
    UI_ShowMpuStatus(5, 30);
    UI_ShowSignedDeg(6, 12, MPU6050_GetRoll10(), 0);
    UI_ShowSignedDeg(6, 66, MPU6050_GetPitch10(), 0);
}

static void UI_RenderThresholdScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "SET THRESH");
    OLED_ShowString(2, 0, "AO:");
    OLED_ShowString(3, 0, "TH:");
    OLED_ShowString(5, 0, "L:- R:+");
    OLED_ShowString(7, 0, "PRESS SAVE");
}

static void UI_RenderThresholdData(void)
{
    UI_ShowValue4(2, 18, App_Light_GetAO());
    UI_ShowValue4(3, 18, App_Light_GetThreshold());
}

static void UI_RenderTrafficItems(void)
{
    const char *items[5];
    uint8_t i;
    uint8_t page;

    items[UI_TRAFFIC_AUTO] = "AUTO";
    items[UI_TRAFFIC_RED] = "RED";
    items[UI_TRAFFIC_YELLOW] = "YELLOW";
    items[UI_TRAFFIC_GREEN] = "GREEN";
    items[UI_TRAFFIC_OFF] = "OFF";

    for (i = 0; i < 5; i++)
    {
        page = 2 + i;
        UI_ClearLine(page);

        if ((i == s_traffic_index) && s_cursor_visible)
        {
            OLED_ShowChar(page, 0, '>');
        }
        else
        {
            OLED_ShowChar(page, 0, ' ');
        }

        OLED_ShowString(page, 12, items[i]);
    }
}

static void UI_RenderTrafficScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "TRAFFIC");
    UI_RenderTrafficItems();
    OLED_ShowString(7, 0, "LEFT BACK");
}

static void UI_RenderMpuScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "MPU6050");
    OLED_ShowString(1, 0, "STAT:");
    OLED_ShowString(2, 0, "ROLL:");
    OLED_ShowString(3, 0, "PITCH:");
    OLED_ShowString(4, 0, "YAW:");
    OLED_ShowString(5, 0, "TEMP:");
    OLED_ShowString(6, 0, "ID:");
    OLED_ShowString(7, 0, "PRESS BACK");
}

static void UI_RenderMpuPageData(void)
{
    UI_ShowMpuStatus(1, 36);
    UI_ShowSignedDeg(2, 36, MPU6050_GetRoll10(), 1);
    UI_ShowSignedDeg(3, 42, MPU6050_GetPitch10(), 1);
    UI_ShowSignedDeg(4, 30, MPU6050_GetYaw10(), 1);
    UI_ShowMpuTemp10(5, 36);
    OLED_ShowString(6, 18, "   ");
    OLED_ShowNum(6, 18, MPU6050_GetWhoAmI(), 3);
}

static void UI_EnterScreen(uint8_t screen)
{
    s_screen = screen;
    s_cursor_visible = 1;
    s_screen_dirty = 1;
    s_status_dirty = 1;
    s_cursor_dirty = 1;
}

static void UI_ApplyTrafficSelection(void)
{
    if (s_traffic_index == UI_TRAFFIC_AUTO)
    {
        App_Light_SetAutoTraffic(1);
    }
    else
    {
        App_Light_SetAutoTraffic(0);

        if (s_traffic_index == UI_TRAFFIC_RED)
        {
            Traffic_RedOn();
        }
        else if (s_traffic_index == UI_TRAFFIC_YELLOW)
        {
            Traffic_YellowOn();
        }
        else if (s_traffic_index == UI_TRAFFIC_GREEN)
        {
            Traffic_GreenOn();
        }
        else
        {
            Traffic_AllOff();
        }
    }
}

static void UI_ProcessMenuEvents(uint8_t events)
{
    if ((events & JOY_EVENT_UP) && (s_menu_index > 0))
    {
        s_menu_index--;
        s_cursor_dirty = 1;
    }

    if ((events & JOY_EVENT_DOWN) && (s_menu_index < 3))
    {
        s_menu_index++;
        s_cursor_dirty = 1;
    }

    if (events & JOY_EVENT_PRESS)
    {
        if (s_menu_index == 0)
        {
            UI_EnterScreen(UI_SCREEN_STATUS);
        }
        else if (s_menu_index == 1)
        {
            UI_EnterScreen(UI_SCREEN_THRESHOLD);
        }
        else if (s_menu_index == 2)
        {
            UI_EnterScreen(UI_SCREEN_TRAFFIC);
        }
        else
        {
            UI_EnterScreen(UI_SCREEN_MPU);
        }
    }
}

static void UI_ProcessThresholdEvents(uint8_t events)
{
    uint16_t threshold;

    threshold = App_Light_GetThreshold();

    if (events & JOY_EVENT_LEFT)
    {
        if (threshold > UI_THRESHOLD_STEP)
        {
            threshold -= UI_THRESHOLD_STEP;
        }

        App_Light_SetThreshold(threshold);
        s_status_dirty = 1;
    }

    if (events & JOY_EVENT_RIGHT)
    {
        threshold += UI_THRESHOLD_STEP;
        App_Light_SetThreshold(threshold);
        s_status_dirty = 1;
    }

    if (events & JOY_EVENT_PRESS)
    {
        UI_EnterScreen(UI_SCREEN_MENU);
    }
}

static void UI_ProcessTrafficEvents(uint8_t events)
{
    if ((events & JOY_EVENT_UP) && (s_traffic_index > 0))
    {
        s_traffic_index--;
        s_cursor_dirty = 1;
    }

    if ((events & JOY_EVENT_DOWN) && (s_traffic_index < UI_TRAFFIC_OFF))
    {
        s_traffic_index++;
        s_cursor_dirty = 1;
    }

    if (events & JOY_EVENT_PRESS)
    {
        UI_ApplyTrafficSelection();
    }

    if (events & JOY_EVENT_LEFT)
    {
        UI_EnterScreen(UI_SCREEN_MENU);
    }
}

static void UI_ProcessEvents(uint8_t events)
{
    if (events == 0)
    {
        return;
    }

    if (s_screen == UI_SCREEN_MENU)
    {
        UI_ProcessMenuEvents(events);
    }
    else if (s_screen == UI_SCREEN_STATUS)
    {
        if ((events & JOY_EVENT_PRESS) || (events & JOY_EVENT_LEFT))
        {
            UI_EnterScreen(UI_SCREEN_MENU);
        }
    }
    else if (s_screen == UI_SCREEN_THRESHOLD)
    {
        UI_ProcessThresholdEvents(events);
    }
    else if (s_screen == UI_SCREEN_TRAFFIC)
    {
        UI_ProcessTrafficEvents(events);
    }
    else
    {
        if ((events & JOY_EVENT_PRESS) || (events & JOY_EVENT_LEFT))
        {
            UI_EnterScreen(UI_SCREEN_MENU);
        }
    }
}

void App_UI_Init(void)
{
    s_last_status_time = Timing_GetTick();
    s_last_cursor_time = s_last_status_time;
    UI_EnterScreen(UI_SCREEN_MENU);
}

void App_UI_Task(void)
{
    uint32_t now;
    uint8_t events;

    now = Timing_GetTick();
    events = Joystick_GetEvents();
    UI_ProcessEvents(events);

    if (now - s_last_status_time >= UI_STATUS_REFRESH_MS)
    {
        s_last_status_time = now;
        s_status_dirty = 1;
    }

    if (now - s_last_cursor_time >= UI_CURSOR_BLINK_MS)
    {
        s_last_cursor_time = now;
        s_cursor_visible = !s_cursor_visible;
        s_cursor_dirty = 1;
    }

    if (s_screen_dirty)
    {
        s_screen_dirty = 0;
        s_status_dirty = 1;
        s_cursor_dirty = 0;

        if (s_screen == UI_SCREEN_MENU)
        {
            UI_RenderMenuScreen();
        }
        else if (s_screen == UI_SCREEN_STATUS)
        {
            UI_RenderStatusScreen();
        }
        else if (s_screen == UI_SCREEN_THRESHOLD)
        {
            UI_RenderThresholdScreen();
        }
        else
        {
            if (s_screen == UI_SCREEN_TRAFFIC)
            {
                UI_RenderTrafficScreen();
            }
            else
            {
                UI_RenderMpuScreen();
            }
        }
    }

    if (s_status_dirty)
    {
        s_status_dirty = 0;

        if (s_screen == UI_SCREEN_MENU)
        {
            UI_RenderStatusArea();
        }
        else if (s_screen == UI_SCREEN_STATUS)
        {
            UI_RenderStatusPageData();
        }
        else if (s_screen == UI_SCREEN_THRESHOLD)
        {
            UI_RenderThresholdData();
        }
        else if (s_screen == UI_SCREEN_MPU)
        {
            UI_RenderMpuPageData();
        }
    }

    if (s_cursor_dirty)
    {
        s_cursor_dirty = 0;

        if (s_screen == UI_SCREEN_MENU)
        {
            UI_RenderMenuItems();
        }
        else if (s_screen == UI_SCREEN_TRAFFIC)
        {
            UI_RenderTrafficItems();
        }
    }
}
