#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "main.h"
#include "esp_usart.h"
#include "esp_at.h"


#define RX_BUFFER_SIZE  4096

#define RX_RESULT_OK    0
#define RX_RESULT_ERROR 1
#define RX_RESULT_FAIL  2


static uint8_t rxdata[RX_BUFFER_SIZE];
static volatile uint32_t rxlen;
static volatile bool rxready;
static volatile uint8_t rxresult;


static void on_usart_received(uint8_t data)
{
    // 没有数据请求，不接受数据
    if (!rxready)
    {
        return;
    }

    // 接收数据，防止缓冲区溢出
    if (rxlen < RX_BUFFER_SIZE - 1)
    {
        rxdata[rxlen++] = data;
        rxdata[rxlen] = '\0';
    }
    else
    {
        rxresult = RX_RESULT_FAIL;
        rxready = false;
        return;
    }

    // 数据接收完毕判断
    if (data == '\n')
    {
        // 判断接收换行符是否为\r\n
        if (rxlen >= 2 && rxdata[rxlen - 2] == '\r')
        {
            // 收到OK
            if (rxlen >= 4 &&
                (rxlen == 4 || rxdata[rxlen - 5] == '\n') &&
                rxdata[rxlen - 4] == 'O' && rxdata[rxlen - 3] == 'K')
            {
                rxresult = RX_RESULT_OK;
                rxready = false;
            }
            else if (rxlen >= 7 &&
                     (rxlen == 7 || rxdata[rxlen - 8] == '\n') &&
                     rxdata[rxlen - 7] == 'E' && rxdata[rxlen - 6] == 'R' &&
                     rxdata[rxlen - 5] == 'R' && rxdata[rxlen - 4] == 'O' &&
                     rxdata[rxlen - 3] == 'R')
            {
                rxresult = RX_RESULT_ERROR;
                rxready = false;
            }
            else if (rxlen >= 6 &&
                     (rxlen == 6 || rxdata[rxlen - 7] == '\n') &&
                     memcmp(&rxdata[rxlen - 6], "FAIL", 4) == 0)
            {
                rxresult = RX_RESULT_FAIL;
                rxready = false;
            }
        }
    }
}

bool esp_at_init(void)
{
    rxready = false;

    esp_usart_init();
    esp_usart_receive_register(on_usart_received);

    return esp_at_reset();
}

bool esp_at_send_command(const char *cmd, const char **rsp, uint32_t *length, uint32_t timeout)
{
    if (rsp)
    {
        *rsp = NULL;
    }
    if (length)
    {
        *length = 0;
    }
    if (cmd == NULL)
    {
        return false;
    }
    rxready = false;
    rxlen = 0;
    rxdata[0] = '\0';
    rxresult = RX_RESULT_FAIL;
    rxready = true;

    esp_usart_write_string(cmd);
    esp_usart_write_string("\r\n");

    while (rxready && timeout--)
    {
        delay_ms(1);
    }
    rxready = false;

    if (rsp)
    {
        *rsp = (const char *)rxdata;
    }
    if (length)
    {
        *length = rxlen;
    }

    return rxresult == RX_RESULT_OK;
}

bool esp_at_send_data(const uint8_t *data, uint32_t length)
{
    esp_usart_write_data((uint8_t *)data, length);

    return true;
}

bool esp_at_reset(void)
{
    // 复位esp32
    if (!esp_at_send_command("AT+RESTORE", NULL, NULL, 1000))
    {
        return false;
    }
    delay_ms(2000);
    // 关闭回显
    if (!esp_at_send_command("ATE0", NULL, NULL, 1000))
    {
        return false;
    }
    // 关闭存储
    if (!esp_at_send_command("AT+SYSSTORE=0", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}

bool esp_at_wifi_init(void)
{
    // 设置为station模式
    if (!esp_at_send_command("AT+CWMODE=1", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}
bool esp_at_wifi_connect(const char *ssid, const char *pwd)
{
    char cmd[128];

    if (ssid == NULL || pwd == NULL)
    {
        return false;
    }

    // 连接wifi
    int size = snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    if (size < 0 || (size_t)size >= sizeof(cmd))
    {
        return false;
    }
    if (!esp_at_send_command(cmd, NULL, NULL, 10000))
    {
        return false;
    }

    return true;
}

bool esp_at_get_http(const char *url, const char **rsp, uint32_t *length, uint32_t timeout)
{
    char cmd[256];

    if (rsp)
    {
        *rsp = NULL;
    }
    if (length)
    {
        *length = 0;
    }
    if (url == NULL)
    {
        return false;
    }

    int size = snprintf(cmd, sizeof(cmd), "AT+HTTPCGET=\"%s\"", url);
    if (size < 0 || (size_t)size >= sizeof(cmd))
    {
        return false;
    }
    if (!esp_at_send_command(cmd, rsp, length, timeout))
    {
        return false;
    }

    return true;
}

bool esp_at_sntp_init(void)
{
    // 设置为SNTP模式
    if (!esp_at_send_command("AT+CIPSNTPCFG=1,8,\"cn.ntp.org.cn\",\"ntp.sjtu.edu.cn\"", NULL, NULL, 1000))
    {
        return false;
    }

    // 查询sntp时间
    if (!esp_at_send_command("AT+CIPSNTPTIME?", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}

bool esp_at_get_time(uint32_t *timestamp)
{
    const char *rsp;
    uint32_t length;

    if (timestamp == NULL)
    {
        return false;
    }
    if (!esp_at_send_command("AT+SYSTIMESTAMP?", &rsp, &length, 1000))
    {
        return false;
    }

    const char *sts = strstr(rsp, "+SYSTIMESTAMP:");
    if (sts == NULL)
    {
        return false;
    }
    sts += strlen("+SYSTIMESTAMP:");
    uint32_t value = 0;
    if (*sts < '0' || *sts > '9')
    {
        return false;
    }
    while (*sts >= '0' && *sts <= '9')
    {
        uint32_t digit = (uint32_t)(*sts++ - '0');
        if (value > (UINT32_MAX - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
    }
    if (value == 0 || *sts != '\r' || sts[1] != '\n')
    {
        return false;
    }
    *timestamp = value;

    return true;
}

static bool copy_quoted_value(const char *rsp, const char *prefix, char *output, size_t capacity)
{
    const char *start = strstr(rsp, prefix);
    if (start == NULL)
    {
        return false;
    }
    start += strlen(prefix);
    if (*start++ != '"')
    {
        return false;
    }
    const char *end = strchr(start, '"');
    if (end == NULL || end == start || (size_t)(end - start) >= capacity)
    {
        return false;
    }
    size_t size = (size_t)(end - start);
    memcpy(output, start, size);
    output[size] = '\0';
    return true;
}

bool esp_at_wifi_get_ip(char ip[16])
{
    const char *rsp;
    if (ip == NULL)
    {
        return false;
    }
    if (!esp_at_send_command("AT+CIPSTA?", &rsp, NULL, 1000))
    {
        return false;
    }

    // 解析ip地址
    return copy_quoted_value(rsp, "+CIPSTA:ip:", ip, 16);
}

bool esp_at_wifi_get_mac(char mac[18])
{
    const char *rsp;
    if (mac == NULL)
    {
        return false;
    }
    if (!esp_at_send_command("AT+CIPSTAMAC?", &rsp, NULL, 1000))
    {
        return false;
    }

    // 解析mac地址
    return copy_quoted_value(rsp, "+CIPSTAMAC:", mac, 18);
}
