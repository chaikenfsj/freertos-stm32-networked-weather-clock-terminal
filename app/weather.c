#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "weather.h"


// 示例输入：
// +HTTPCGET:...,{"current":{"time":"...","interval":900,"temperature_2m":25.4,"weather_code":3}}
//
// OK


bool weather_parse(const char *data, weather_t *weather)
{
    if (data == NULL || weather == NULL)
    {
        return false;
    }
    weather_t parsed = {0};

    // 从current对象开始查找，避免命中前面current_units中的同名字段
    const char *current = strstr(data, "\"current\":");
    if (current == NULL)
    {
        return false;
    }

    const char *p = strstr(current, "\"temperature_2m\":");
    if (p == NULL)
    {
        return false;
    }
    p += strlen("\"temperature_2m\":");
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }

    // 屏幕的大字温度区域只适合显示整数，保留符号和整数部分
    const char *q = p;
    if (*q == '-')
    {
        q++;
    }
    const char *digits = q;
    while (*q >= '0' && *q <= '9')
    {
        q++;
    }
    if (q == digits)
    {
        return false;
    }
    size_t len = (size_t)(q - p);
    if (len >= sizeof(parsed.temperature))
    {
        return false;
    }
    memcpy(parsed.temperature, p, len);
    parsed.temperature[len] = '\0';

    p = strstr(current, "\"weather_code\":");
    if (p == NULL)
    {
        return false;
    }
    p += strlen("\"weather_code\":");
    char *end = NULL;
    long code = strtol(p, &end, 10);
    if (end == p)
    {
        return false;
    }

    if (code == 0)
    {
        strcpy(parsed.weather, "Clear");
    }
    else if (code == 1 || code == 2)
    {
        strcpy(parsed.weather, "Cloudy");
    }
    else if (code == 3)
    {
        strcpy(parsed.weather, "Overcast");
    }
    else if (code == 45 || code == 48)
    {
        strcpy(parsed.weather, "Fog");
    }
    else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || (code >= 95 && code <= 99))
    {
        strcpy(parsed.weather, "Rain");
    }
    else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86))
    {
        strcpy(parsed.weather, "Snow");
    }
    else
    {
        strcpy(parsed.weather, "Unknown");
    }

    *weather = parsed;
    return true;
}
