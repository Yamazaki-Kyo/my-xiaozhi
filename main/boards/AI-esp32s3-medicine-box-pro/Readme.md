# AI 智能药盒 Pro

**基于开源小智 + K230D 视觉识别的智能药盒方案**

---

## 前言

### 项目背景

中国 65 岁以上老年人口已超过 2 亿，其中约 60% 需要长期服用处方药物。漏服、错服、重复服药是居家慢病管理中最常见的三大问题。市面上的智能药盒普遍存在以下痛点：

- **功能割裂**：语音提醒、药品识别、健康监测、远程通知各自独立，需要多个设备配合
- **交互门槛高**：触屏操作对高龄用户不友好，APP 配置流程繁琐
- **隐私风险**：健康数据和用药记录全部上传云端，缺乏本地化处理能力

本方案基于开源生态，将**语音交互**（小智）与**本地人脸识别**（K230D NPU）整合，通过免费的第三方推送服务（PushPlus/Server酱）实现微信通知，打造一个既能说话、又能认人、子女能远程知情的智能药盒。

### 应用场景

| 场景                 | 描述                                                         |
|----------------------|--------------------------------------------------------------|
| **独居老人日常用药** | 到点语音提醒 + 自动旋转出药 → 子女微信收到"已服药"通知        |
| **多药品复杂方案**   | 早晚不同药物组合，K230D 人脸识别确认取药人身份，防止他人误取  |
| **家庭健康管家**     | 心率血氧检测 + 用药记录，本地隐私存储，异常主动微信告警       |
| **远程关怀**         | 子女通过微信实时接收服药确认、异常告警，必要时可远程联系      |

---

## 方案总览

本文档提供**两种架构方案**，用户可根据需求选择：

| 维度       | 方案一（推荐）                            | 方案二                                          |
|------------|:----------------------------------------:|:-----------------------------------------------:|
| 芯片数量   | **2 芯片**                               | **3 芯片**                                      |
| 硬件       | 小智 ESP32-S3 + K230D                    | 小智 ESP32-S3 + K230D + ESP-Claw ESP32-S3       |
| 微信通知   | 小智直连 PushPlus/Server酱               | 小智 → UART → ESP-Claw → 微信                   |
| 服药调度   | 小智本地 MedicineReminder                | ESP-Claw Lua 脚本                               |
| IM 多通道  | 微信（通过 PushPlus）                    | Telegram + 微信 + 飞书                          |
| 硬件成本   | **~480 元**                              | ~680 元                                         |
| 复杂度     | ⭐⭐⭐ 中                                 | ⭐⭐⭐⭐ 高                                      |
| 成熟度     | 全部基于已验证功能                        | ESP-Claw 生态早期，存在不确定性                  |

- **方案一**（推荐）：务实路线，全部业务逻辑跑在小智上，微信通知通过小智直接 HTTP 调用 PushPlus 实现，K230D 只做人脸识别。ESP-Claw 暂不参与。
- **方案二**：在方案一基础上引入 ESP-Claw 作为独立 Agent 层，扩展 IM 多通道能力（Telegram/飞书等）。适合 ESP-Claw 生态成熟后升级。

---

# 方案一：小智 + K230D 双芯片架构（推荐）

## 系统架构

```
┌──────────────────────────────────────────────────────────────┐
│                   XiaoZhi ESP32-S3 (全业务)                    │
│                                                              │
│   INMP441 麦克风 ──→ I2S ──→ Opus 编码 ──→ WiFi ──→ 云端 LLM  │
│   云端 TTS 音频 ←── I2S ←── Opus 解码 ←── WiFi ←── 云端 LLM   │
│   MAX98357A 功放 ──→ 喇叭                                     │
│                                                              │
│   SPI LCD 240x320 ──→ LVGL 显示 (状态/对话/健康数据)           │
│   MAX30102 ──→ I2C ──→ 心率血氧 PPG 算法                      │
│   MG90S/MG996 180° 舵机 ──→ LEDC PWM ──→ 7 槽药品转盘         │
│   GPIO48 LED / GPIO0 按键                                     │
│                                                              │
│   ┌──────────────────────────────────────────┐               │
│   │ MedicineReminder (NVS 数据权威)            │               │
│   │ HTTP Server (Web 配置页)                   │               │
│   │ PushPlus 微信推送 (HTTP POST)              │               │
│   └──────────────────────────────────────────┘               │
│                                                              │
│   ┌──────────────────────────────────────────┐               │
│   │ 淘晶驰串口屏 UART (UART1 GPIO8/9)          │               │
│   │  本地配置后台，与 Web 数据联动              │               │
│   └──────────────────────────────────────────┘               │
│                                                              │
│   ┌──────────────────────────────────────────┐               │
│   │ K230D Vision UART (UART2 GPIO13/14)       │               │
│   │  接收人脸识别结果                          │               │
│   └──────────────────────────────────────────┘               │
└──────────────────────┬───────────────────────────────────────┘
                       │ UART2
                       ▼
┌──────────────────────────────────────────────────────────────┐
│                      K230D AI 视觉模组                         │
│                                                              │
│   ★ 人脸识别 + 特征提取 (当前实现)                              │
│   - 药品识别 (规划中)                                          │
│   - 条形码扫描 (规划中)                                        │
│   GC2093 摄像头 @1080P                                        │
└──────────────────────────────────────────────────────────────┘
```

**设计原则**：
- **单一主控**：小智承担所有业务逻辑（语音、显示、舵机控制、健康检测、服药提醒、Web 配置、微信推送），K230D 仅做人脸识别
- **微信直达**：小智直接 HTTP POST 到 PushPlus API，无需中间芯片中转

## 硬件方案

### 物料清单

| 序号 | 物料               | 型号/规格                            | 数量   | 用途             |
|------|--------------------|--------------------------------------|--------|------------------|
| 1    | ESP32-S3 开发板    | DevKitC-1 (16MB Flash + 8MB PSRAM)   | 1      | 运行小智         |
| 2    | K230D AI 视觉模组  | CanMV K230D + GC2093 摄像头          | 1      | 本地人脸识别     |
| 3    | I2S 数字麦克风     | INMP441                              | 1      | 语音输入         |
| 4    | I2S 数字功放       | MAX98357A                            | 1      | 语音输出         |
| 5    | 喇叭               | 3W / 4Ω                              | 1      | 语音播报         |
| 6    | LCD 显示屏         | ST7789 240x320 SPI (8Pin)            | 1      | 信息展示         |
| 7    | 心率血氧传感器     | MAX30102                             | 1      | 健康检测         |
| 8    | 180° 舵机          | MG90S / MG996                        | 1      | 药品转盘驱动     |
| 9    | LED                | 5mm 红色 LED                         | 1      | 状态指示         |
| 10   | 轻触开关           | 6x6mm                                | 1      | 物理按键         |
| 11   | 3D 打印药盒外壳    | PLA / 7 槽转盘结构                   | 1 套   | 机械结构         |
| 12   | 淘晶驰串口屏       | TJC X2系列 7寸 电容触摸 (UART)       | 1      | 本地配置后台     |
| 13   | 杜邦线 + 面包板    | —                                    | 若干   | 原型验证         |

### 引脚接线

#### XiaoZhi ESP32-S3 ↔ 外设

| ESP32-S3 引脚 | 外设                         | 信号                        |
|:-------------:|------------------------------|-----------------------------|
| GPIO4         | INMP441                      | WS (数据选择)               |
| GPIO5         | INMP441                      | SCK (时钟)                  |
| GPIO6         | INMP441                      | SD (数据)                   |
| GPIO7         | MAX98357A                    | DIN (数字信号)              |
| GPIO15        | MAX98357A                    | BCLK (位时钟)               |
| GPIO16        | MAX98357A                    | LRC (左/右时钟)             |
| GPIO47        | LCD                          | MOSI (数据)                 |
| GPIO21        | LCD                          | SCLK (时钟)                 |
| GPIO40        | LCD                          | DC (数据选择)               |
| GPIO45        | LCD                          | RST (复位)                  |
| GPIO41        | LCD                          | CS (片选)                   |
| GPIO42        | LCD                          | BLK (背光)                  |
| GPIO17        | MAX30102                     | SDA (I2C 数据)              |
| GPIO18        | MAX30102                     | SCL (I2C 时钟)              |
| GPIO12        | 180° 舵机 (MG90S/MG996)      | PWM (信号线)                |
| GPIO48        | LED                          | 阳极 (通过 220Ω 限流)      |
| GPIO0         | 按键                         | 一端接 GND，内置上拉        |

#### XiaoZhi ESP32-S3 ↔ K230D 视觉模组 (UART2)

| XiaoZhi 引脚   | K230D 引脚    | 说明                       |
|:--------------:|:-------------:|----------------------------|
| GPIO13 (RXD)   | TXD           | K230D → XiaoZhi            |
| GPIO14 (TXD)   | RXD           | XiaoZhi → K230D            |
| GND            | GND           | 共地                       |
| 3V3            | VCC (可选)    | 若 K230D 不从 USB 取电     |

> K230D 独立供电（USB-C），两块板仅通过 UART 和 GND 互联。

#### XiaoZhi ESP32-S3 ↔ 淘晶驰串口屏 (UART1)

| XiaoZhi 引脚   | 串口屏引脚    | 说明                                    |
|:--------------:|:------------:|-----------------------------------------|
| GPIO8 (TXD)    | RX           | XiaoZhi → 串口屏                        |
| GPIO9 (RXD)    | TX           | 串口屏 → XiaoZhi                        |
| 5V             | VCC          | 串口屏供电（需独立供电，不可从 ESP32 取电） |
| GND            | GND          | 共地                                    |

> 串口屏推荐独立供电（5V/2A），7 寸屏功耗较大，不可从 ESP32 取电。

## 微信通知实现

本方案不需要 ESP-Claw，小智直接通过 HTTP 调用第三方推送服务，将消息推送到用户微信。

### PushPlus 方案（推荐）

**原理**：用户微信关注「PushPlus」公众号 → 获取一个专属 Token → 小智往 `pushplus.plus` API 发 HTTP POST → 微信秒收推送。支持一对多推送（家庭成员各自关注同一公众号即可）。

**在小智中的实现**（在 `medicine_box_pro_board.cc` 中添加）：

```cpp
#include "esp_http_client.h"

class WechatNotifier {
public:
    WechatNotifier(const char* token) : token_(token) {}

    void Send(const char* title, const char* content) {
        char url[256];
        snprintf(url, sizeof(url), "http://www.pushplus.plus/send");

        char post_data[1024];
        snprintf(post_data, sizeof(post_data),
            "token=%s&title=%s&content=%s&template=html",
            token_, title, content);

        esp_http_client_config_t config = {};
        config.url = url;
        config.method = HTTP_METHOD_POST;
        config.timeout_ms = 5000;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type",
            "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, post_data, strlen(post_data));

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "微信通知发送成功");
        } else {
            ESP_LOGW(TAG, "微信通知发送失败: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);
    }

private:
    const char* token_;
};

// 使用示例
WechatNotifier notifier("YOUR_PUSHPLUS_TOKEN");

// 服药确认通知
notifier.Send("服药提醒", "张三 已服用 08:00 降压药 ✅");

// 异常告警
notifier.Send("⚠️ 药盒异常", "检测到非注册人员取药！");

// 健康异常告警
notifier.Send("🫀 健康提醒", "心率异常：105 次/分钟，超出正常范围。");
```

**PushPlus 对接步骤**：

1. 微信搜索并关注「PushPlus」公众号
2. 进入公众号 →「个人中心」→ 获取 Token
3. 将 Token 填入小智固件配置（通过 Web 配置页或 Kconfig）
4. 家庭成员各自关注 PushPlus 公众号，在「一对多」中添加接收人

### 备选方案

| 服务                   | 接入方式                                                     | 特点                           |
|------------------------|--------------------------------------------------------------|--------------------------------|
| **Server酱**           | GET `https://sctapi.ftqq.com/{SENDKEY}.send?title=...&desp=...` | 更简单，但免费版有限额         |
| **企业微信群机器人**   | POST JSON 到 Webhook URL                                     | 适合已有企业微信的家庭         |
| **微信公众号测试号**   | POST JSON 到模板消息 API                                     | 需开发者认证，但体验最好       |

### 微信通知触发节点

| 事件                           | 通知内容                                               | 接收人             |
|--------------------------------|--------------------------------------------------------|--------------------|
| 服药完成                       | "张三 已服用 08:00 降压药 ✅"                           | 所有家庭成员       |
| 提醒超时（5分钟未取药）        | "⚠️ 08:00 降压药 提醒超时，尚未取药"                    | 所有家庭成员       |
| 连续3次未取药                  | "🚨 今日降压药 连续3次未服用，请关注！"                  | 紧急联系人         |
| 非注册人员取药                 | "⚠️ 药盒检测到非注册人员（陌生人）取药"                  | 所有家庭成员       |
| 心率/血氧异常                  | "🫀 心率：105次/分，偏离正常范围"                        | 紧急联系人         |
| 药品库存不足                   | "📦 1号槽 降压药 预计剩余 3 天用量"                      | 所有家庭成员       |

## 数据存储与多端联动

### 数据权威：NVS + MedicineReminder

网页、串口屏、小智本地逻辑三方共享同一份服药计划数据。数据架构如下：

```
┌───────────────────────────────────────┐
│           NVS (Flash 非易失存储)        │
│  namespace: "med_plan"                │
│  ├── "plan_json"      服药计划 JSON    │
│  ├── "master_enabled" 提醒总开关       │
│  └── "last_day"       上次检查日期      │
└────────────────┬──────────────────────┘
                 │ LoadPlan() / SavePlan()
                 ▼
┌───────────────────────────────────────┐
│       MedicineReminder (单例)          │  ← 唯一数据权威
│  - SetPlanFromJson(json)             │
│  - GetPlanJson() → cJSON*            │
│  - NotifyListeners() 变更广播         │
└──┬──────────────┬──────────────┬──────┘
   │              │              │
   ▼              ▼              ▼
┌─────────┐ ┌──────────┐ ┌──────────┐
│ Web 页面 │ │ 串口屏    │ │ 小智内部  │
│ HTTP    │ │ UART     │ │ 直接调用   │
│ 手机/PC  │ │ TJC 触控  │ │ RTC 提醒  │
└─────────┘ └──────────┘ └──────────┘
```

**设计要点**：

- `MedicineReminder` 是唯一的数据读写入口，三方不直接操作 NVS
- 任何一方修改数据 → `MedicineReminder` 更新内存 → `SavePlan()` 写入 NVS → `NotifyListeners()` 广播变更
- `NotifyListeners` 是一个回调列表，Web 和串口屏各自注册，确保数据始终同步

### 联动流程：Web 修改 → 串口屏自动刷新

```
1. 子女用手机打开 Web 配置页，将 1 号槽药品从"降压药"改为"降压药缓释片"
2. 网页 POST /api/medicine/plan → XiaoZhi HTTP Server 收到 JSON
3. HttpSetPlanHandler 调用 MedicineReminder::SetPlanFromJson(json)
4. SetPlanFromJson 内部:
   a. cJSON_Parse(json) → 更新内存 MedicinePlan 结构体
   b. SavePlan() → Settings.SetString("med_plan", "plan_json", json) → 写入 NVS
   c. NotifyListeners() → 遍历回调
5. NotifyListeners 触发两个回调:
   ├── Web 回调: 返回 200 OK → 浏览器显示"保存成功"
   └── 串口屏回调: uart_write("t1.txt=\"降压药缓释片\"") → 串口屏即时更新
```

### 联动流程：串口屏触控 → Web 下次打开自动同步

```
1. 老人触控串口屏，把 08:00 提醒改成 07:30
2. 串口屏通过 UART 发: {"type":"update_reminder","slot":1,"old_time":"08:00","new_time":"07:30"}
3. XiaoZhi UART 任务收到 → 解析 JSON
4. 调用 MedicineReminder::UpdateReminderTime(1, "08:00", "07:30")
5. 更新内存数据 → SavePlan() → NVS
6. NotifyListeners → 串口屏显示确认弹窗; Web 端(下次刷新即见新时间)
```

---

## 串口屏配置方案

### 选型：淘晶驰 TJC X2 系列

淘晶驰（TJC）是国内最成熟的串口屏方案，自带 MCU + Flash + GUI 设计工具，通过 UART 收发简单指令即可完成界面更新和触控事件回传。X2 系列相较基础款拥有更快的处理器和更大的存储空间，适合复杂界面。

| 型号                           | 系列  | 尺寸    | 分辨率    | 触摸类型    | 参考价    |
|--------------------------------|-------|---------|-----------|------------|-----------|
| **TJC8048X270_011C** (推荐)    | X2    | 7.0寸   | 800×480   | 电容触摸   | ~180元    |

> 选用 7 寸电容屏：大字体清晰适合老人操作，电容触摸响应灵敏，800×480 分辨率足够展示完整的药品配置界面。

### 淘晶驰开发流程

1. 下载 **USART HMI**（淘晶驰官方 GUI 设计软件）
2. 在软件中拖控件设计界面（按钮、文本框、时间选择器、开关等）
3. 每个控件绑定事件：触控时自动通过 UART 返回数据
4. 编译生成 `.tft` 文件，通过 TF 卡烧录到串口屏
5. 烧录后屏幕独立运行，小智只需通过 UART 发字符串指令更新数据

### XiaoZhi ↔ 串口屏 通信协议

淘晶驰串口屏的指令格式非常简单，本质是"控件名.属性=值"，以 `0xFF 0xFF 0xFF` 结尾。为便于解析和扩展，XiaoZhi 端包装为 JSON → 淘晶驰指令的转换层。

**XiaoZhi → 串口屏（更新显示）**：

| 场景               | JSON（XiaoZhi 内部）                                                     | 淘晶驰指令（发往屏幕）                     |
|--------------------|-------------------------------------------------------------------------|--------------------------------------------|
| 同步 1 号槽药品名   | `{"type":"plan_sync","slot":1,"name":"降压药","dose":2}`                 | `med1.txt="降压药"` + `dose1.txt="每次2颗"` |
| 同步完整计划        | `{"type":"plan_full_sync","slots":[...],"master_enabled":true}`          | 批量更新所有控件                            |
| 正在出药            | `{"type":"status","event":"dispensing","slot":1}`                        | `status.txt="正在出药: 1号槽"`              |
| 健康检测中          | `{"type":"status","event":"health_measuring","sec":15}`                  | `status.txt="检测中...剩余15秒"`            |

**串口屏 → XiaoZhi（用户操作）**：

淘晶驰在用户触控时自动通过 UART 返回控件事件。例如用户点击"保存"按钮，屏幕自动发 `0xFF 0xFF 0xFF btn_save 1 0xFF 0xFF 0xFF`。XiaoZhi 端解析后映射为 JSON：

| 淘晶驰返回事件                         | XiaoZhi 解析后的 JSON                                                                                         |
|----------------------------------------|---------------------------------------------------------------------------------------------------------------|
| 用户修改槽位 1 药品名并保存            | `{"source":"serial_screen","type":"update_slot","slot":1,"name":"降压药","dose":2,"times":["08:00","19:00"]}` |
| 用户切换总开关为关闭                   | `{"source":"serial_screen","type":"set_master_switch","enabled":false}`                                       |
| 用户点击"立即出药"按钮                 | `{"source":"serial_screen","type":"dispense_now","slot":3}`                                                   |
| 用户点击"健康检测"按钮                 | `{"source":"serial_screen","type":"start_health_check"}`                                                      |

### XiaoZhi 端实现：TjcSerialScreen 类

```cpp
class TjcSerialScreen {
public:
    TjcSerialScreen(uart_port_t port, int tx_pin, int rx_pin);

    // 注册 MedicineReminder 变更回调
    void AttachToReminder(MedicineReminder& reminder);

    // 发送 JSON 消息，内部转换为淘晶驰指令
    void SendUpdate(const cJSON* json);

    // 同步完整计划到屏幕
    void SyncFullPlan(const MedicinePlan& plan);

private:
    uart_port_t port_;

    // 淘晶驰指令发送（自动追加帧尾）
    void SendCommand(const char* cmd);

    // UART 接收任务：解析淘晶驰事件 → 转换为 JSON → 调用 MedicineReminder
    static void UartTask(void* arg);
    void ProcessTouchEvent(const char* raw_event);

    // 将 JSON 消息转为淘晶驰指令
    void JsonToTjcCommand(const cJSON* json);
};

// 指令转换示例
void TjcSerialScreen::SendCommand(const char* cmd) {
    uart_write_bytes(port_, cmd, strlen(cmd));
    // 淘晶驰指令帧尾（3个 0xFF）
    uint8_t tail[] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(port_, tail, 3);
}

void TjcSerialScreen::SyncFullPlan(const MedicinePlan& plan) {
    char buf[128];
    for (int i = 1; i <= 7; i++) {
        if (plan.slot_configured[i]) {
            snprintf(buf, sizeof(buf), "med%d.txt=\"%s\"", i, plan.slots[i].drug_name);
            SendCommand(buf);
            snprintf(buf, sizeof(buf), "dose%d.txt=\"每次%d颗\"", i, plan.slots[i].dose_count);
            SendCommand(buf);
        } else {
            snprintf(buf, sizeof(buf), "med%d.txt=\"未配置\"", i);
            SendCommand(buf);
        }
    }
    // 同步提醒开关状态
    SendCommand(plan.master_enabled ? "sw_master.val=1" : "sw_master.val=0");
}
```

### UI 界面设计建议

```
┌──────────────────────────┐
│  🏠 AI 智能药盒 - 配置    │
│                          │
│  [总开关]  ● 已开启       │
│                          │
│  槽位1: [降压药       ]   │
│  剂量:  [2] 颗/次         │
│  时间:  [08:00] [19:00]  │
│  ─────────────────────   │
│  槽位2: [降糖药       ]   │
│  剂量:  [1] 颗/次         │
│  时间:  [12:30]          │
│  ─────────────────────   │
│  ...                     │
│                          │
│  [立即出药 ▼] [健康检测]  │
│  [保存配置] [退出]        │
└──────────────────────────┘
```

---

## 功能设计

### 一、语音交互（XiaoZhi 原生）

- **唤醒词**："你好小智"
- **对话能力**：通过云端 LLM 实现自然语言交互，可询问用药时间、药品说明、健康建议
- **语音指令**（MCP 触发）：
  - "检测心率" / "测血氧" → 启动 MAX30102 健康检测
  - "打开 3 号药槽" → 舵机旋转至指定槽位
  - "关闭药盒" → 舵机回位
  - "今天还有什么药没吃" → 查询今日未完成提醒

### 二、人脸识别（K230D 本地 NPU）

| 功能               | 状态          | 实现方式                                              | 应用                                 |
|--------------------|:-------------:|-------------------------------------------------------|--------------------------------------|
| **人脸识别**       | ✅ 当前实现   | MobileFaceNet 轻量模型，本地注册 2-5 位家庭成员       | 非注册人员取药微信告警；个性化提醒   |
| **药品识别**       | 🔲 规划中     | 自训练 YOLO 小模型，.kmodel 部署                      | 取药时视觉确认是否正确               |
| **条码扫描**       | 🔲 规划中     | K230D 内置二维码/AprilTag 库                          | 扫描药品包装条码自动录入             |
| **取药动作检测**   | 🔲 规划中     | 帧差法监控取药口区域                                  | 自动确认取药完成                     |

### 三、服药调度（XiaoZhi 本地 MedicineReminder）

复用现有 medicine-box 的完整服药提醒系统：

- **计划管理**：Web 页面配置每日 1-5 次、7 槽独立药品
- **数据持久化**：服药计划存储在 NVS，断电不丢失
- **RTC 定时触发**：30 秒周期检查，匹配当前时间并触发提醒
- **语音播报**：到点自动 TTS 语音播报药品及剂量
- **自动出药**：舵机旋转至对应药槽
- **二次提醒**：首次提醒后 5 分钟未取药，降低音量重播
- **取药确认**：用户按下确认键，标记已完成，微信通知子女

### 四、健康监测（XiaoZhi + MAX30102）

复用现有 medicine-box 的完整流程：

1. 手指在位检测（信号强度阈值，10 秒超时）
2. 15-20 秒稳定数据采集
3. PPG 波形滤波 → 峰值识别 → 心率/血氧计算
4. 语音播报 + 屏幕大字显示结果
5. 异常数值温和提醒（心率 < 50 或 > 120，血氧 < 95%）
6. 异常数值微信告警

### 五、双通道配置（Web 页面 + 串口屏）

药盒提供两种配置方式，数据通过 MedicineReminder 实时同步：

| 通道               | 适用场景                       | 硬件               | 交互方式           |
|--------------------|--------------------------------|--------------------|--------------------|
| **Web 配置页**     | 子女远程配置、初次设置         | 手机/PC 浏览器     | 触摸 + 键盘        |
| **淘晶驰串口屏**   | 老人本地操作、日常调整         | TJC 串口屏         | 触摸 + 大字体      |

- **Web 配置页** 复用现有 medicine-box 的响应式页面：药品与药槽绑定、服药时间设置、提醒开关、微信推送 Token、人脸库管理
- **串口屏** 提供简化的本地界面，面向老人：药品名称编辑、提醒时间调整、总开关、一键出药、健康检测入口
- 任一方修改后，另一方立即同步（详见上方"数据存储与多端联动"）

---

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
│   SPI LCD 240x320 ──→ LVGL 显示 (状态/对话/健康数据)           │
│   MAX30102 ──→ I2C ──→ 心率血氧 PPG 算法                      │
│   MG90S/MG996 180° 舵机 ──→ LEDC PWM ──→ 7 槽药品转盘         │
│   GPIO48 LED / GPIO0 按键                                     │
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
| 舵机到位           | `{"src":"xiaozhi","type":"event","event":"servo_done","slot":3}`                                | 转盘旋转完成                   |
| 取药确认           | `{"src":"xiaozhi","type":"event","event":"pill_taken","slot":3}`                                | 用户按键确认取药               |
| 人脸检测（已注册） | `{"src":"xiaozhi","type":"event","event":"vision","vision_type":"face","person":"张三","confidence":0.92}`  | K230D 识别到已注册人脸 |
| 人脸检测（陌生人） | `{"src":"xiaozhi","type":"event","event":"vision","vision_type":"face","person":"unknown","confidence":0.85}` | K230D 检测到未注册人脸 |

**命令下发 (ESP-Claw → XiaoZhi)**：

| 命令       | JSON                                                                                    | 动作                       |
|------------|-----------------------------------------------------------------------------------------|----------------------------|
| 语音播报   | `{"src":"claw","type":"cmd","cmd":"speak","text":"该吃药了..."}`                        | 触发本地 TTS 播报          |
| 屏幕显示   | `{"src":"claw","type":"cmd","cmd":"display","message":"降压药 2颗\n槽位: 1"}`           | LCD 显示消息               |
| 出药       | `{"src":"claw","type":"cmd","cmd":"dispense","slot":3}`                                 | 舵机旋转至槽位 3           |
| 闭口       | `{"src":"claw","type":"cmd","cmd":"close"}`                                             | 舵机回位置 0               |
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

# 公共部分（两个方案通用）

以下内容适用于两个方案。

## XiaoZhi ↔ K230D 视觉数据 (UART2)

K230D 端（CanMV MicroPython）通过 UART 发送 JSON 行，协议沿用现有 medicine-box 的 `VISION_UART_*` 框架。当前仅实现人脸识别：

```json
{"type":"face","person":"张三","confidence":0.92}
{"type":"face","person":"unknown","confidence":0.85}
```

> 药品识别、条码扫描、取药动作检测等功能将在后续版本中逐步加入，届时扩展 JSON 字段。

小智接收后：
1. 若检测到人脸 → 自动唤醒小智（触发对话）
2. 方案一中直接处理身份逻辑；方案二中通过 EspClawBridge 转发至 ESP-Claw

## K230D 视觉识别方案

### 开发环境

- **IDE**：CanMV IDE（图形化 MicroPython 开发环境）
- **模型转换**：TensorFlow/PyTorch 训练 → nncase 工具 → .kmodel 格式
- **固件**：CanMV 官方固件（含 KPU 驱动和 Sensor API）

### 人脸识别流程

```
1. GC2093 摄像头持续采集 @ 30fps
2. 轻量人脸检测模型 → 裁剪人脸区域
3. MobileFaceNet 提取 128 维特征向量
4. 与本地注册库比对 (余弦相似度 > 0.7 视为匹配)
5. 结果通过 UART 发送 JSON 至 XiaoZhi
```

### CanMV 主脚本示例（当前阶段：人脸识别）

```python
# main.py - K230D 药盒人脸识别主程序
from machine import UART
import sensor, image, time, json

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(30)

uart = UART(1, baudrate=115200, tx_pin=4, rx_pin=5)
face_detector = kpu.load("/sd/face_detect.kmodel")

while True:
    img = sensor.snapshot()
    faces = kpu.forward(face_detector, img)
    for face in faces:
        result = {"type": "face", "person": "unknown", "confidence": float(face.confidence)}
        uart.write(json.dumps(result) + "\n")
    time.sleep_ms(100)

# TODO: 后续版本加入药品识别模型 pill_cls.kmodel
# TODO: 后续版本加入条码扫描、取药动作检测
```

### 后续扩展（规划中）

```
药品识别:  MobileNetV2-SSD / YOLO-Fastest → nncase 转 .kmodel → KPU 推理
条码扫描:  K230D 内置二维码/AprilTag 库
动作检测:  帧差法监控取药口区域
```

---

## 工作流程示例

### 场景：早 8 点服药提醒全流程

```
08:00:00  XiaoZhi RTC 定时器触发服药提醒
08:00:01  XiaoZhi TTS 语音播报: "主人，该吃药啦。请服用1号槽的降压药，每次2颗。"
08:00:01  舵机从位置 0 旋转至位置 1（1号槽对准取药口）
08:00:02  LCD 显示 "降压药 2颗 | 槽位 1"
08:00:05  用户听到语音，走近药盒
08:00:06  K230D 检测到人脸 → 确认身份为"张三"
08:00:10  用户从取药口取药，按下确认键
08:00:11  舵机回位置 0（闭口）
08:00:12  XiaoZhi 通过 PushPlus 推送微信: "张三 已服用 08:00 降压药 ✅"
08:00:13  子女微信收到通知
```

---

## 编译与烧录

### 环境准备

```bash
# 激活 ESP-IDF (示例路径)
cmd /k C:\esp\v5.5.4\esp-idf\export.bat

# 进入项目目录
cd xiaozhi-esp32
```

### XiaoZhi 固件编译

```bash
idf.py menuconfig
# → Application Configuration → Board Type → AI-esp32s3-medicine-box-pro

idf.py build
idf.py -p COMx flash monitor
```

### Kconfig 关键配置项

| 配置项                 | 推荐值                             |
|------------------------|------------------------------------|
| Board Type             | AI-esp32s3-medicine-box-pro        |
| LCD Type               | ST7789 240x320                     |
| Wake Word              | AFE Wake Word (ESP32-S3 推荐)      |
| Audio Noise Reduction  | On                                 |
| WiFi Configuration     | Hotspot                            |

---

## 与现有 medicine-box 的差异

| 维度         | medicine-box (基础版)          | medicine-box-pro                                     |
|--------------|--------------------------------|------------------------------------------------------|
| 主控         | 1× ESP32-S3                    | 1× ESP32-S3 (+ 可选 ESP-Claw)                        |
| 视觉         | 外部模组 UART 唤醒             | K230D NPU 本地人脸识别                               |
| 微信通知     | 无                             | PushPlus/Server酱 直连微信                           |
| 人脸识别     | 无                             | K230D 本地推理 + 身份验证                            |
| 配置方式     | Web 页面                       | Web 页面 + 淘晶驰串口屏（双通道联动）                |
| 硬件成本     | ~150 元                        | ~480 元（方案一）/ ~680 元（方案二）                  |
