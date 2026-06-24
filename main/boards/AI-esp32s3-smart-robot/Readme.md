# AI瓦力机器人
---
## 介绍
趣创俱乐部暑假深圳营交付产品

## 硬件
| ESP32-S3 引脚 | 外设 | 信号 |
|:-------------:|-----|-----|
| GPIO4  | 麦克风 INMP441 | WS 数据选择 |
| GPIO5  | 麦克风 INMP441 | SCK 时钟 |
| GPIO6  | 麦克风 INMP441 | DIN 数据输入 |
| GND    | 麦克风 INMP441 | L/R 左右声道 |
| VCC    | 功放MAX98357A | SD关机频道 |
| GPIO7  | 功放 MAX98357A | DOUT 数据输出 |
| GPIO15 | 功放 MAX98357A | BCLK 位时钟 |
| GPIO16 | 功放 MAX98357A | LRCK 左右声道时钟 |
| GPIO47 | LCD ST7789 | MOSI 主机输出 |
| GPIO21 | LCD ST7789 | SCLK 时钟 |
| GPIO40 | LCD ST7789 | DC 数据/命令 |
| GPIO45 | LCD ST7789 | RST 复位 |
| GPIO41 | LCD ST7789 | CS 片选 |
| GPIO42 | LCD ST7789 | BL 背光 |
| GPIO48 | 舵机1 | 信号线 |
| GPIO13 | 舵机2 | 信号线 |
| GPIO0 | 按键 | BOOT 启动按钮 |
| GPIO1 | 电机驱动 DRV8833 | AIN1 |
| GPIO2 | 电机驱动 DRV8833 | AIN2 |
| GPIO10 | 电机驱动 DRV8833 | BIN1 |
| GPIO11 | 电机驱动 DRV8833 | BIN2 |
| — | DRV8833 → TT马达1 | AO1, AO2 |
| — | DRV8833 → TT马达2 | BO1, BO2 |
| — | DRV8833 STBY | 直连 5V 高电平 |
