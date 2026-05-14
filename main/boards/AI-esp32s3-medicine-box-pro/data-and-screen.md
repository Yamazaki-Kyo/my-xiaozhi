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
