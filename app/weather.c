#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "weather.h"


// 示例输入：
// +HTTPCGET:261,{"results":[{"location":{"id":"WTEMH46Z5N09","name":"Hefei","country":"CN","path":"Hefei,Hefei,Anhui,China","timezone":"Asia/Shanghai","timezone_offset":"+08:00"},"now":{"text":"Cloudy","code":"4","temperature":"36"},"last_update":"2024-08-26T16:20:13+08:00"}]}
//
// OK


bool weather_parse(const char *data, weather_t *weather)
{
    if (data == NULL || weather == NULL)
    {
        return false;
    }
    weather_t parsed = {0};
    const char *p = strstr(data, "\"text\":\"");
    if (p == NULL)
    {
        return false;
    }
    p += strlen("\"text\":\"");
    const char *q = strchr(p, '\"');
    if (q == NULL)
    {
        return false;
    }
    size_t len = (size_t)(q - p);
    if (len >= sizeof(parsed.weather))
    {
        len = sizeof(parsed.weather) - 1;
    }
    memcpy(parsed.weather, p, len);
    parsed.weather[len] = '\0';

    p = strstr(data, "\"temperature\":\"");
    if (p == NULL)
    {
        return false;
    }
    p += strlen("\"temperature\":\"");
    q = strchr(p, '\"');
    if (q == NULL)
    {
        return false;
    }
    len = (size_t)(q - p);
    if (len >= sizeof(parsed.temperature))
    {
        len = sizeof(parsed.temperature) - 1;
    }
    memcpy(parsed.temperature, p, len);
    parsed.temperature[len] = '\0';

    *weather = parsed;
    return true;
}
