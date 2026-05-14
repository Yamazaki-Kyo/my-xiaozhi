## 微信通知实现

本方案不需要 ESP-Claw，小智直接通过 HTTP 调用 WxPusher API，将消息推送到用户微信。

### WxPusher 方案（推荐）

**原理**：用户在 WxPusher 平台扫码关注应用 → 小智通过 HTTP GET 调用 WxPusher API → 消息直达微信。可选在 WxPusher App 内绑定 ClawBot (iLink) 通道，消息将直接进入微信聊天界面，体验更佳。

**架构概览**：

```
老人语音 "推送聊天记录"
       │
       ▼
┌─────────────────────────────┐
│  XiaoZhi ESP32-S3           │
│                             │
│  ConversationLogger (环形缓冲)│
│  → ToSummary() 摘要         │
│  → ToMarkdown() 聊天记录    │
│                             │
│  MCP Tool: push_conversation│
│  → FreeRTOS Task (8KB 栈)   │
│  → WxPusherNotifier::Send   │
└──────────┬──────────────────┘
           │ HTTP GET
           ▼
┌─────────────────────────────┐
│  wxpusher.zjiecode.com      │
│  /api/send/message          │
│  ?appToken=AT_xxx           │
│  &uid=UID_xxx               │
│  &contentType=3 (Markdown)  │
│  &summary=...&content=...   │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  微信 / WxPusher 公众号      │
│  (可选 ClawBot iLink 通道)   │
└─────────────────────────────┘
```

**实际代码实现**：

`wxpusher_notifier.h` — WxPusher 推送模块：

```cpp
#define WXPUSHER_APP_TOKEN "AT_L5k2LTux7tEFbz62YFcy38ebqPWpyJVO"
#define WXPUSHER_UID "UID_6n0C3cRBd8qdzPVyFMROFiPurm2L"

class WxPusherNotifier {
public:
    // 发送纯文本消息 (contentType=1)
    void SendText(const char* summary, const char* text,
                  const char* uid = WXPUSHER_UID);

    // 发送 Markdown 消息 (contentType=3)
    void SendMarkdown(const char* summary, const char* markdown,
                      const char* uid = WXPUSHER_UID);
};
```

`conversation_logger.h` — 对话记录环形缓冲（最多200条），自动捕获 STT/TTS 消息，过滤 MCP 工具调用：

```cpp
class ConversationLogger {
public:
    void Append(const std::string& role, const std::string& content);
    std::string ToMarkdown();   // Markdown 格式，用于微信推送
    std::string ToSummary();    // 摘要 ≤100 字符，用于推送标题
    void Clear();               // 推送后清空
};
```

`medicine_box_pro_board.cc` — MCP 工具注册：

```cpp
McpServer::GetInstance().AddTool(
    "push_conversation",
    "将当前对话记录推送到家人微信",
    PropertyList(),
    [this](const PropertyList& props) -> ReturnValue {
        auto& logger = ConversationLogger::GetInstance();
        auto* params = new PushTaskParams{
            logger.ToSummary(),
            logger.ToMarkdown(),
            &notifier_
        };
        logger.Clear();
        xTaskCreate(PushConversationTask, "wxpush", 8192,
                    params, 5, NULL);
        return std::string("对话记录已推送到微信");
    });
```

**WxPusher 对接步骤**：

1. 访问 [wxpusher.zjiecode.com](https://wxpusher.zjiecode.com) 注册账号并创建应用，获取 AppToken
2. 微信扫码关注该应用（无需填写 UID 也可接收）
3. 将 AppToken 和 UID 填入 `wxpusher_notifier.h`
4. （可选）在 WxPusher App 内绑定 ClawBot → 消息直接进微信聊天界面
5. 老人说「推送聊天记录」→ MCP 触发 → 子女微信即时收到对话摘要

### 备选方案

| 服务                   | 接入方式                                                     | 特点                           |
|------------------------|--------------------------------------------------------------|--------------------------------|
| **PushPlus**           | POST `http://www.pushplus.plus/send` (form-urlencoded)       | 需关注公众号获取 Token，支持一对多推送 |
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
