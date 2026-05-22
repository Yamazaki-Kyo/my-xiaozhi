## 工作流程示例

### 场景：早 8 点服药提醒全流程

```
08:00:00  XiaoZhi MedCheckCallback 30 秒定时器触发用药提醒
08:00:01  360° 舵机顺时针旋转, PillBoxTurntable 自动转至目标药槽 (双微动开关闭环计数)
08:00:02  屏幕左侧大字显示 "请取出" + "2" (30px) + "颗药 · 槽1"
08:00:03  OGG 语音循环播报 "该吃药了" (每 5.5 秒循环)
08:00:05  用户听到语音，走近药盒
08:00:06  K230D 检测到人脸 → 确认身份为"张三"
08:00:10  用户从取药口取药，按下 GPIO10 静音键
08:00:11  停止 OGG 循环 + 清除屏幕提醒 + 舵机 goHome() 归零至槽0
08:00:12  XiaoZhi 通过 WxPusher 推送微信: "张三 已服用 08:00 降压药 ✅"
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
| LCD Type               | GC9A01 240x240 / ST7789 240x240    |
| Wake Word              | AFE Wake Word (ESP32-S3 推荐)      |
| Audio Noise Reduction  | On                                 |
| WiFi Configuration     | Hotspot                            |

---

## 与现有 medicine-box 的差异

| 维度         | medicine-box (基础版)          | medicine-box-pro                                     |
|--------------|--------------------------------|------------------------------------------------------|
| 主控         | 1× ESP32-S3                    | 1× ESP32-S3 (+ 可选 ESP-Claw)                        |
| 视觉         | 外部模组 UART 唤醒             | K230D NPU 本地人脸识别                               |
| 舵机         | 180° 角度控制                    | 360° 连续旋转 + 双微动开关闭环定位                   |
| 转盘定位     | PWM 角度 (开环)                 | SW0/SW1 硬件计数 (闭环, 无累积误差)                  |
| 微信通知     | 无                             | WxPusher 直连微信                                   |
| 人脸识别     | 无                             | K230D 本地推理 + 身份验证                            |
| 定时调度     | 无                             | 独立 LED 定时调度器 (Web + MCP 语音)                 |
| 用药提醒     | TTS 单次播报                    | OGG 循环播报 + GPIO10 单键确认 + 转盘自动归零        |
| 屏幕显示     | 基础状态栏                      | 侧边栏实时状态 + 大字服药剂量覆盖层                   |
| 配置方式     | Web 页面                       | 3 个 Web 页面 (用药/调度/舵机) + 淘晶驰串口屏         |
| 硬件成本     | ~150 元                        | ~480 元（方案一）/ ~680 元（方案二）                  |
