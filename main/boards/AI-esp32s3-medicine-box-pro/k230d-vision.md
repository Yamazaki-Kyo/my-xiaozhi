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
