#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h"
#include "main.h"
#include "led.h"
#include "rtc.h"
#include "esp_at.h"
#include "mpu6050.h"
#include "st7735.h"
#include "stfonts.h"
#include "stimage.h"
#include "ui_glyphs.h"
#include "weather.h"
#include "network_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


static const char *wifi_ssid = WIFI_SSID;
static const char *wifi_password = WIFI_PASSWORD;
static const char *weather_uri = WEATHER_API_URI;


typedef enum
{
    UI_EVENT_INDOOR_TEMPERATURE,
    UI_EVENT_TIME_SYNCED,
    UI_EVENT_WEATHER,
    UI_EVENT_NETWORK_ERROR
} ui_event_type_t;

typedef enum
{
    NETWORK_ERROR_ESP,
    NETWORK_ERROR_WIFI,
    NETWORK_ERROR_SNTP,
    NETWORK_ERROR_HTTP,
    NETWORK_ERROR_JSON
} network_error_t;

typedef struct
{
    ui_event_type_t type;
    union
    {
        float indoor_temperature;
        weather_t weather;
        network_error_t network_error;
    } data;
} ui_event_t;

static QueueHandle_t ui_event_queue;


#define UI_DARK_BLUE  ST7735_COLOR565(5, 24, 48)
#define UI_MUTED_BLUE ST7735_COLOR565(24, 54, 82)
#define UI_PURPLE     ST7735_COLOR565(174, 86, 255)
#define UI_ORANGE     ST7735_COLOR565(255, 158, 36)
#define UI_HEADER     ST7735_COLOR565(13, 22, 47)


static void ui_draw_chinese_glyph(uint16_t x, uint16_t y, const uint8_t *glyph,
                                  uint16_t color, uint16_t bgcolor)
{
    uint16_t row;
    uint16_t col;

    for (row = 0; row < 16; row++)
    {
        for (col = 0; col < 16; col++)
        {
            uint8_t mask = (uint8_t)(0x80U >> (col & 7U));
            uint8_t bits = glyph[row * 2U + col / 8U];
            st7735_draw_pixel(x + col, y + row, (bits & mask) ? color : bgcolor);
        }
    }
}

static void ui_draw_weather_text(const char *weather)
{
    const uint8_t *first = ui_cn_wei;
    const uint8_t *second = ui_cn_zhi;
    uint16_t x = 5;

    if (strcmp(weather, "Clear") == 0)
    {
        first = ui_cn_qing;
        second = ui_cn_tian;
    }
    else if (strcmp(weather, "Cloudy") == 0)
    {
        first = ui_cn_duo;
        second = ui_cn_yun;
    }
    else if (strcmp(weather, "Overcast") == 0)
    {
        first = ui_cn_yin;
        second = ui_cn_tian;
    }
    else if (strcmp(weather, "Fog") == 0)
    {
        first = ui_cn_wu;
        second = NULL;
        x = 13;
    }
    else if (strcmp(weather, "Rain") == 0)
    {
        first = ui_cn_xia;
        second = ui_cn_yu;
    }
    else if (strcmp(weather, "Snow") == 0)
    {
        first = ui_cn_xia;
        second = ui_cn_xue;
    }

    st7735_fill_rect(2, 112, 38, 16, ST7735_BLACK);
    ui_draw_chinese_glyph(x, 112, first, ST7735_CYAN, ST7735_BLACK);
    if (second != NULL)
    {
        ui_draw_chinese_glyph(x + 16, 112, second, ST7735_CYAN, ST7735_BLACK);
    }
}

static void ui_draw_degree_symbol(uint16_t x, uint16_t y, uint16_t color)
{
    // 5x5空心圆环，比直接使用字母o更接近温度单位的角标。
    st7735_fill_rect(x, y, 5, 5, ST7735_BLACK);
    st7735_fill_rect(x + 1, y, 3, 1, color);
    st7735_fill_rect(x + 1, y + 4, 3, 1, color);
    st7735_fill_rect(x, y + 1, 1, 3, color);
    st7735_fill_rect(x + 4, y + 1, 1, 3, color);
}

static void ui_draw_outdoor_temperature(const char *temperature)
{
    uint16_t number_width = (uint16_t)strlen(temperature) * font_temper_16x32.width;
    uint16_t total_width = number_width + 5U + font_temper_16x32.width;
    uint16_t x = (total_width < 54U) ? (uint16_t)(99U - total_width) : 45U;

    st7735_fill_rect(44, 70, 55, 32, ST7735_BLACK);
    st7735_write_string(x, 70, temperature, &font_temper_16x32,
                        UI_ORANGE, ST7735_BLACK);
    ui_draw_degree_symbol(x + number_width, 73, UI_ORANGE);
    st7735_write_char(x + number_width + 5U, 70, 'C', &font_temper_16x32,
                      UI_ORANGE, ST7735_BLACK);
}

static void ui_draw_indoor_temperature(float temperature)
{
    char value[16];
    uint16_t text_width;

    snprintf(value, sizeof(value), "IN %2.0f", temperature);
    text_width = (uint16_t)strlen(value) * font_ascii_8x16.width;

    st7735_fill_rect(44, 108, 55, 16, ST7735_BLACK);
    st7735_write_string(44, 108, value, &font_ascii_8x16,
                        ST7735_GREEN, ST7735_BLACK);
    ui_draw_degree_symbol(44U + text_width, 109, ST7735_GREEN);
    st7735_write_char(49U + text_width, 108, 'C', &font_ascii_8x16,
                      ST7735_GREEN, ST7735_BLACK);
}

static void ui_draw_guangzhou_tower(void)
{
    uint16_t y;

    // 右侧用一座纤细的广州塔平衡画面，不压过温度和天气角色
    st7735_fill_rect(116, 68, 2, 6, UI_ORANGE);
    st7735_draw_pixel(116, 67, ST7735_YELLOW);
    st7735_fill_rect(112, 74, 10, 2, UI_ORANGE);

    for (y = 76; y <= 124; y++)
    {
        uint16_t distance = (y > 99) ? (y - 99) : (99 - y);
        uint16_t half_width = 2 + distance / 7;
        uint16_t left = 117 - half_width;
        uint16_t right = 117 + half_width;
        uint16_t color;

        if (y < 92)
        {
            color = UI_ORANGE;
        }
        else if (y < 109)
        {
            color = UI_PURPLE;
        }
        else
        {
            color = ST7735_CYAN;
        }

        st7735_draw_pixel(left, y, color);
        st7735_draw_pixel(right, y, color);
        if ((y - 76) % 6 == 0)
        {
            st7735_fill_rect(left, y, right - left + 1, 1, UI_MUTED_BLUE);
            st7735_draw_pixel(left, y, color);
            st7735_draw_pixel(right, y, color);
        }
    }
    st7735_fill_rect(109, 125, 18, 2, ST7735_CYAN);
}

static void ui_draw_dashboard_decorations(void)
{
    // 顶栏使用直观的英文城市名，避免 CANTON 造成理解成本
    st7735_fill_rect(0, 0, ST7735_WIDTH, 16, UI_HEADER);
    st7735_write_string(2, 0, "GUANGZHOU", &font_ascii_8x16, ST7735_CYAN, UI_HEADER);

    // 时间与天气区之间用一条有节奏的广州夜景配色分割线
    st7735_fill_rect(0, 64, 48, 2, ST7735_CYAN);
    st7735_fill_rect(48, 64, 48, 2, UI_PURPLE);
    st7735_fill_rect(96, 64, 32, 2, UI_ORANGE);
    st7735_fill_rect(42, 69, 1, 57, UI_MUTED_BLUE);
    st7735_fill_rect(102, 69, 1, 57, UI_MUTED_BLUE);

    st7735_draw_image(2, 68, icon_weather_mascot.width,
                      icon_weather_mascot.height, icon_weather_mascot.data);
    ui_draw_guangzhou_tower();
}

static void ui_draw_clock(const rtc_date_t *date)
{
    char value[3];

    snprintf(value, sizeof(value), "%02d", date->hour);
    st7735_write_string(4, 16, value, &font_time_24x48, ST7735_WHITE, ST7735_BLACK);

    // 橙色冒号每秒闪烁，比整行变空格更干净
    st7735_write_char(52, 16, ':', &font_time_24x48,
                      date->second % 2 ? ST7735_BLACK : UI_ORANGE,
                      ST7735_BLACK);

    snprintf(value, sizeof(value), "%02d", date->minute);
    st7735_write_string(76, 16, value, &font_time_24x48, ST7735_WHITE, ST7735_BLACK);
}

static void ui_draw_clock_waiting(void)
{
    st7735_fill_rect(0, 16, ST7735_WIDTH, 48, ST7735_BLACK);
    st7735_write_string(36, 32, "SYNCING", &font_ascii_8x16,
                        UI_ORANGE, ST7735_BLACK);
}


static void ui_post_network_error(network_error_t error)
{
    ui_event_t event;
    event.type = UI_EVENT_NETWORK_ERROR;
    event.data.network_error = error;
    (void)xQueueSend(ui_event_queue, &event, pdMS_TO_TICKS(100));
}

static bool network_initialize(network_error_t *error)
{
    if (!esp_at_init())
    {
        *error = NETWORK_ERROR_ESP;
        return false;
    }
    if (!esp_at_wifi_init() || !esp_at_wifi_connect(wifi_ssid, wifi_password))
    {
        *error = NETWORK_ERROR_WIFI;
        return false;
    }
    if (!esp_at_sntp_init())
    {
        *error = NETWORK_ERROR_SNTP;
        return false;
    }
    return true;
}

static bool network_sync_time(void)
{
    uint32_t timestamp;
    if (!esp_at_get_time(&timestamp))
    {
        return false;
    }
    rtc_set_timestamp(timestamp + 8UL * 60UL * 60UL);
    return true;
}

static void ui_task(void *argument)
{
    bool time_ready = false;
    TickType_t last_clock_update = 0;
    ui_event_t event;
    char value[16];

    (void)argument;
    st7735_fill_screen(ST7735_BLACK);
    ui_draw_dashboard_decorations();
    st7735_write_string(86, 0, "--/--", &font_ascii_8x16,
                        UI_MUTED_BLUE, UI_HEADER);
    ui_draw_clock_waiting();
    st7735_write_string(5, 112, "WAIT", &font_ascii_8x16,
                        ST7735_YELLOW, ST7735_BLACK);

    for (;;)
    {
        if (xQueueReceive(ui_event_queue, &event, pdMS_TO_TICKS(100)) == pdPASS)
        {
            switch (event.type)
            {
                case UI_EVENT_INDOOR_TEMPERATURE:
                    ui_draw_indoor_temperature(event.data.indoor_temperature);
                    break;

                case UI_EVENT_TIME_SYNCED:
                    time_ready = true;
                    last_clock_update = 0;
                    break;

                case UI_EVENT_WEATHER:
                    ui_draw_weather_text(event.data.weather.weather);
                    ui_draw_outdoor_temperature(event.data.weather.temperature);
                    break;

                case UI_EVENT_NETWORK_ERROR:
                {
                    const char *error_text = "NET";
                    if (event.data.network_error == NETWORK_ERROR_ESP)
                    {
                        error_text = "ESP";
                    }
                    else if (event.data.network_error == NETWORK_ERROR_WIFI)
                    {
                        error_text = "WIFI";
                    }
                    else if (event.data.network_error == NETWORK_ERROR_SNTP)
                    {
                        error_text = "NTP";
                    }
                    else if (event.data.network_error == NETWORK_ERROR_HTTP)
                    {
                        error_text = "HTTP";
                    }
                    else if (event.data.network_error == NETWORK_ERROR_JSON)
                    {
                        error_text = "JSON";
                    }
                    st7735_fill_rect(2, 112, 38, 16, ST7735_BLACK);
                    st7735_write_string(5, 112, error_text, &font_ascii_8x16,
                                        ST7735_YELLOW, ST7735_BLACK);
                    break;
                }

                default:
                    break;
            }
        }

        if (time_ready &&
            (TickType_t)(xTaskGetTickCount() - last_clock_update) >= pdMS_TO_TICKS(100))
        {
            rtc_date_t date;
            last_clock_update = xTaskGetTickCount();
            rtc_get_date(&date);
            snprintf(value, sizeof(value), "%02d/%02d", date.month, date.day);
            st7735_write_string(86, 0, value, &font_ascii_8x16,
                                ST7735_WHITE, UI_HEADER);
            ui_draw_clock(&date);
        }
    }
}

static void sensor_task(void *argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    ui_event_t event;

    (void)argument;
    event.type = UI_EVENT_INDOOR_TEMPERATURE;
    for (;;)
    {
        event.data.indoor_temperature = mpu6050_read_temper();
        (void)xQueueSend(ui_event_queue, &event, 0);
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(1000));
    }
}

static void network_task(void *argument)
{
    ui_event_t event;
    network_error_t init_error;

    (void)argument;
    for (;;)
    {
        bool time_ok = false;
        bool weather_ok = false;
        bool time_attempted = false;
        bool weather_attempted = false;
        uint8_t retry;
        uint8_t consecutive_failures = 0;
        TickType_t last_time_attempt = 0;
        TickType_t last_weather_attempt = 0;

        if (!network_initialize(&init_error))
        {
            ui_post_network_error(init_error);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        for (retry = 0; retry < 10; retry++)
        {
            time_attempted = true;
            last_time_attempt = xTaskGetTickCount();
            if (network_sync_time())
            {
                event.type = UI_EVENT_TIME_SYNCED;
                (void)xQueueSend(ui_event_queue, &event, pdMS_TO_TICKS(100));
                time_ok = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        for (;;)
        {
            TickType_t now = xTaskGetTickCount();
            TickType_t time_interval = time_ok ? pdMS_TO_TICKS(60UL * 60UL * 1000UL)
                                               : pdMS_TO_TICKS(2000);
            TickType_t weather_interval = weather_ok ? pdMS_TO_TICKS(10UL * 60UL * 1000UL)
                                                     : pdMS_TO_TICKS(10000);

            if (!time_attempted ||
                (TickType_t)(now - last_time_attempt) >= time_interval)
            {
                time_attempted = true;
                last_time_attempt = now;
                if (network_sync_time())
                {
                    event.type = UI_EVENT_TIME_SYNCED;
                    (void)xQueueSend(ui_event_queue, &event, pdMS_TO_TICKS(100));
                    time_ok = true;
                }
            }

            if (!weather_attempted ||
                (TickType_t)(now - last_weather_attempt) >= weather_interval)
            {
                const char *response = NULL;
                bool http_ok;

                weather_attempted = true;
                last_weather_attempt = now;
                http_ok = esp_at_get_http(weather_uri, &response, NULL, 10000);
                event.type = UI_EVENT_WEATHER;
                weather_ok = http_ok && weather_parse(response, &event.data.weather);
                if (weather_ok)
                {
                    consecutive_failures = 0;
                    (void)xQueueSend(ui_event_queue, &event, pdMS_TO_TICKS(100));
                }
                else
                {
                    ui_post_network_error(http_ok ? NETWORK_ERROR_JSON : NETWORK_ERROR_HTTP);
                    consecutive_failures++;
                    if (consecutive_failures >= 3)
                    {
                        break;
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    BaseType_t result;

    board_lowlevel_init();
    led_init();
    rtc_init();
    (void)mpu6050_init();
    st7735_init();
    st7735_fill_screen(ST7735_BLACK);

    ui_event_queue = xQueueCreate(6, sizeof(ui_event_t));
    configASSERT(ui_event_queue != NULL);
    vQueueAddToRegistry(ui_event_queue, "uiEvent");

    result = xTaskCreate(ui_task, "UI", 384, NULL, 3, NULL);
    configASSERT(result == pdPASS);
    result = xTaskCreate(network_task, "Network", 384, NULL, 2, NULL);
    configASSERT(result == pdPASS);
    result = xTaskCreate(sensor_task, "Sensor", 128, NULL, 1, NULL);
    configASSERT(result == pdPASS);

    vTaskStartScheduler();
    for (;;)
    {
    }
}
