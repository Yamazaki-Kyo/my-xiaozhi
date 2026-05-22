# 方案二：小智 + K230D + ESP-Claw 三芯片架构

> 此方案在方案一基础上引入 ESP-Claw 作为独立 AI Agent 层。ESP-Claw 是乐鑫 2025-2026 年推出的开源 AI 智能体框架，生态尚在早期，有一定不确定性。**建议先按方案一实现，等 ESP-Claw 生态成熟后再升级至此方案。**

## 系统架构

```
┌──────────────────────────────────────────────────────────────┐
│              XiaoZhi ESP32-S3 (Body / 感知执行层)              │
│                                                              │
│   INMP441 麦克风 ──→ I2S ──→ Opus 编码 ──→ WiFi ──→ 云端 LLM  │
│   云端 TTS 音频 ←── I2S ←── Opus 解码 ←── WiFi ←── 云端 LLM   │
│   MAX98357A 功放 ──→ 喇叭                                     │
│                                                              │
│   SPI LCD 240x240 ──→ LVGL 显示 (状态/对话/用药提醒)           │
│   MAX30102 ──→ I2C ──→ 心率血氧 PPG 算法                      │
│   360° 舵机 ──→ LEDC PWM ──→ 8 槽药品转盘 (1 logo + 7 药)     │
│   SW0/SW1 微动开关 ──→ 转盘定位 (双开关闭环计数)                │
│   GPIO48 LED / GPIO10 静音键 / GPIO0 按键 / GPIO3 调度 LED      │
│                                                              │
│   ┌─────────────────────────────────────────┐                │
│   │ EspClawBridge (UART1 GPIO8/9 @115200)   │                │
│   │  JSON 行协议 | 心跳 10s | 超时告警        │                │
│   └─────────────────────────────────────────┘                │
│                                                              │
│   ┌─────────────────────────────────────────┐                │
│   │ K230D Vision UART (UART2 GPIO13/14)      │                │
│   │  接收人脸识别结果                         │                │
│   └─────────────────────────────────────────┘                │
└───────────────┬────────────────────────┬─────────────────────┘
                │ UART1 (JSON 双向)       │ UART2 (视觉结果)
                ▼                        ▼
┌───────────────────────────┐  ┌──────────────────────┐
│  ESP-Claw ESP32-S3 (Brain)│  │  K230D AI 视觉模组    │
│                           │  │                      │
│  - AI Agent 决策引擎       │  │  ★ 人脸识别 + 特征提取 │
│  - Lua 自动化脚本           │  │  - 药品识别 (规划中)   │
│  - 服药计划调度             │  │  - 条形码扫描 (规划中)  │
│  - Telegram/微信/飞书 Bot  │  │  - 取药动作检测 (规划中) │
│  - Web 配置页面            │  │                      │
│  - 本地私有记忆 (NVS)      │  │  GC2093 摄像头 @1080P │
│  - 云端 LLM 连接           │  └──────────────────────┘
└───────────────────────────┘
```

## 方案二新增物料

在方案一物料清单基础上，额外增加：

| 序号 | 物料               | 型号/规格                            | 数量   | 用途               |
|------|--------------------|--------------------------------------|--------|--------------------|
| 13   | ESP32-S3 开发板    | DevKitC-1 (16MB Flash + 8MB PSRAM)   | 1      | 运行 ESP-Claw      |

> **注意**：方案二中 UART1 被 ESP-Claw 占用。若仍需串口屏，需将串口屏用杜邦线接至空闲 GPIO（如 GPIO10/11），通过 ESP32-S3 GPIO 矩阵映射为软件串口，但性能不如硬件 UART。建议方案二用户优先使用 Web 配置，或等 ESP-Claw 支持直接驱动串口屏后再接入。

## 方案二新增接线

#### XiaoZhi ↔ ESP-Claw (UART1)

| XiaoZhi 引脚   | ESP-Claw 引脚 | 说明                       |
|:--------------:|:-------------:|----------------------------|
| GPIO8 (TXD)    | GPIO9 (RXD)   | XiaoZhi → ESP-Claw         |
| GPIO9 (RXD)    | GPIO8 (TXD)   | ESP-Claw → XiaoZhi         |
| GND            | GND           | 共地                       |

## 通信协议 (XiaoZhi ↔ ESP-Claw)

**物理层**：115200 baud / 8N1 / 无流控，每条 JSON 以 `\n` 结尾

**心跳保活**：
```
XiaoZhi → Claw:  {"src":"xiaozhi","type":"hb"}
Claw → XiaoZhi:  {"src":"claw","type":"hb_ack"}
```
间隔 10 秒。连续 3 次未收到 ACK（共 30 秒），XiaoZhi 判定 ESP-Claw 断线并上报告警。

**事件上报 (XiaoZhi → ESP-Claw)**：

| 事件               | JSON                                                                                              | 触发条件                       |
|--------------------|---------------------------------------------------------------------------------------------------|--------------------------------|
| 唤醒词检测         | `{"src":"xiaozhi","type":"event","event":"wake_word"}`                                           | "你好小智" 唤醒                |
| 按键事件           | `{"src":"xiaozhi","type":"event","event":"button","btn":"boot","action":"click"}`                | GPIO0 按下                     |
| 健康检测完成       | `{"src":"xiaozhi","type":"event","event":"health_result","hr":78,"spo2":98,"quality":0.95}`      | MAX30102 测量结束              |
| 舵机到位           | `{"src":"xiaozhi","type":"event","event":"servo_done","slot":3}`                                | 转盘旋转到位 (SW1 计数到达目标) |
| 取药确认/提醒关闭  | `{"src":"xiaozhi","type":"event","event":"pill_taken","slot":3}`                                | 用户按 GPIO10 确认取药, 转盘归零 |
| 人脸检测（已注册） | `{"src":"xiaozhi","type":"event","event":"vision","vision_type":"face","person":"张三","confidence":0.92}`  | K230D 识别到已注册人脸 |
| 人脸检测（陌生人） | `{"src":"xiaozhi","type":"event","event":"vision","vision_type":"face","person":"unknown","confidence":0.85}` | K230D 检测到未注册人脸 |

**命令下发 (ESP-Claw → XiaoZhi)**：

| 命令       | JSON                                                                                    | 动作                       |
|------------|-----------------------------------------------------------------------------------------|----------------------------|
| 语音播报   | `{"src":"claw","type":"cmd","cmd":"speak","text":"该吃药了..."}`                        | 触发本地 TTS 播报          |
| 屏幕显示   | `{"src":"claw","type":"cmd","cmd":"display","message":"降压药 2颗\n槽位: 1"}`           | LCD 显示消息               |
| 出药       | `{"src":"claw","type":"cmd","cmd":"dispense","slot":3}`                                 | 360° 舵机转盘转至槽位 3 (SW1 计数定位) |
| 闭口       | `{"src":"claw","type":"cmd","cmd":"close"}`                                             | 转盘 goHome() 归零至槽位 0 (寻 SW0) |
| 健康检测   | `{"src":"claw","type":"cmd","cmd":"health_check"}`                                      | 启动心率血氧检测           |
| LED 控制   | `{"src":"claw","type":"cmd","cmd":"led","state":"blink","duration_ms":5000}`            | LED 闪烁 5 秒              |

## ESP-Claw 端配置指南

### 1. 烧录固件

访问 [ESP Launchpad](https://espressif.github.io/esp-launchpad/)，选择 ESP32-S3 设备，烧录最新 ESP-Claw 固件（8MB Flash + 8MB PSRAM）。

### 2. WiFi 与 LLM 配置

烧录后连接热点 `esp-claw-xxxx`，访问 `http://esp-claw.local/`：
- 配置家庭 WiFi
- 填入 LLM API Key（推荐 DeepSeek / Qwen）
- 配置 Telegram Bot Token / 微信 ClawBot

### 3. 药盒 Lua 脚本示例

```lua
-- medicine_scheduler.lua

local schedule = {
    {time="08:00", slot=1, drug="降压药", dose=2},
    {time="12:30", slot=2, drug="降糖药", dose=1},
    {time="19:00", slot=1, drug="降压药", dose=2},
}

local pill_taken_today = {}

function on_timer(minute_of_day)
    for _, entry in ipairs(schedule) do
        local h, m = entry.time:match("(%d+):(%d+)")
        local target_min = tonumber(h) * 60 + tonumber(m)
        if minute_of_day == target_min and not pill_taken_today[entry.time] then
            uart_send_json({
                src = "claw", type = "cmd", cmd = "speak",
                text = string.format("主人，该吃药啦。请服用%d号槽的%s，每次%d颗。",
                                     entry.slot, entry.drug, entry.dose)
            })
            uart_send_json({src="claw", type="cmd", cmd="dispense", slot=entry.slot})
        end
    end
end

function on_event(event)
    if event.event == "pill_taken" then
        pill_taken_today[os.date("%H:%M")] = true
        telegram_send("父母已服用 " .. os.date("%H:%M") .. " 的药物")
    elseif event.event == "vision" and event.vision_type == "face"
           and event.person == "unknown" then
        telegram_send("⚠️ 药盒检测到非注册人员取药！")
    end
end
```

---
