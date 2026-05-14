## 工作流程示例

### 场景：早 8 点服药提醒全流程

```
08:00:00  XiaoZhi RTC 定时器触发服药提醒
08:00:01  XiaoZhi TTS 语音播报: "主人，该吃药啦。请服用1号槽的降压药，每次2颗。"
08:00:01  180° 舵机 PWM 指定角度，直接旋转至槽位1（保持力矩锁定）
08:00:02  到位即锁定，LCD 显示 "降压药 2颗 | 槽位 1"
08:00:05  用户听到语音，走近药盒
08:00:06  K230D 检测到人脸 → 确认身份为"张三"
08:00:10  用户从取药口取药，按下确认键
08:00:11  舵机归位至槽位0（logo 展示位）
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
| 微信通知     | 无                             | WxPusher 直连微信                                   |
| 人脸识别     | 无                             | K230D 本地推理 + 身份验证                            |
| 配置方式     | Web 页面                       | Web 页面 + 淘晶驰串口屏（双通道联动）                |
| 硬件成本     | ~150 元                        | ~480 元（方案一）/ ~680 元（方案二）                  |
