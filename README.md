# STM32 联网天气时钟终端

基于 STM32F103C8T6 的联网天气时钟终端。系统通过 ESP32 AT 固件接入 Wi‑Fi，使用 SNTP 校准片内 RTC，并从天气 API 获取实时天气，在 ST7735 彩色屏幕上显示日期、时间、天气图标、网络温度和本地传感器温度。

## 主要功能

- ESP32 AT 指令初始化、Wi‑Fi 连接和 HTTP GET 请求。
- SNTP 网络授时，每小时重新同步一次 RTC。
- 每 10 分钟更新一次实时天气。
- ST7735 SPI 彩屏显示日期、时间、温度和天气图标。
- 支持晴、多云、阴、雨、雪和大风图标。
- MPU6050 温度读取，用于显示终端附近的环境温度参考值。
- STM32F103 标准外设库、CMSIS 和 Keil µVision 工程完整包含在仓库中。

## 硬件与软件

- MCU：STM32F103C8T6（Cortex‑M3）
- 网络模块：运行 AT 固件的 ESP32
- 显示：ST7735 SPI TFT
- 传感器：MPU6050
- 时钟：STM32 RTC + 32.768 kHz LSE
- IDE：Keil µVision 5
- 编译器：ARM Compiler 5
- 器件包：Keil STM32F1xx DFP 2.4.1

## 目录结构

- `app/`：主程序、天气解析、字体和天气图标资源。
- `driver/`：ESP AT、ST7735、RTC、定时器、LED 和 MPU6050 驱动。
- `firmware/`：STM32F10x 标准外设库与 CMSIS。
- `mdk/stm32f103.uvprojx`：Keil µVision 工程入口。

## 配置联网参数

为避免把 Wi‑Fi 密码和天气 API 密钥提交到 GitHub，联网参数使用本地头文件：

1. 将 `app/network_config.h.example` 复制为 `app/network_config.h`。
2. 填写 `WIFI_SSID` 和 `WIFI_PASSWORD`。
3. 在 `WEATHER_API_URI` 中填写自己的天气 API 密钥和城市。

`app/network_config.h` 已加入 `.gitignore`，不会被提交。

## 编译

1. 安装 Keil µVision 5 和 STM32F1xx 器件支持包。
2. 完成本地联网参数配置。
3. 打开 `mdk/stm32f103.uvprojx`。
4. 选择 `project` 目标并执行 Build/Rebuild。

原工程最近一次完整构建结果为 0 错误、0 警告，程序占用记录为：Code 13,324 B、RO-data 32,376 B、RW-data 120 B、ZI-data 9,216 B。本次 GitHub 整理未重新连接硬件烧录或进行实机联网验证。

## 更新周期

- 显示日期和时间：100 ms
- 读取本地温度：1 s
- 更新网络天气：10 min
- SNTP 时间同步：1 h

