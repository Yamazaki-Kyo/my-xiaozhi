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
| 8    | 180° 舵机          | MG90S / MG996R                       | 1      | 药品转盘驱动     |
| 9    | LED                | 5mm 红色 LED                         | 1      | 状态指示         |
| 10   | 轻触开关           | 6x6mm                                | 1      | 物理按键         |
| 11   | 3D 打印药盒外壳    | PLA / 8 槽转盘结构 (1 logo + 7 药)   | 1 套   | 机械结构         |
| 12   | 淘晶驰串口屏       | TJC X2系列 7寸 电容触摸 (UART)       | 1      | 本地配置后台     |
| 13   | 杜邦线 + 面包板    | —                                    | 若干   | 原型验证         |

### 引脚接线

#### XiaoZhi ESP32-S3 ↔ 外设

| ESP32-S3 引脚 | 外设                         | 信号                        |
|:-------------:|------------------------------|-----------------------------|
| GPIO1         | INMP441                      | WS (数据选择)               |
| GPIO2         | INMP441                      | SCK (时钟)                  |
| GPIO42        | INMP441                      | SD (数据)                   |
| GPIO39        | MAX98357A                    | DIN (数字信号)              |
| GPIO40        | MAX98357A                    | BCLK (位时钟)               |
| GPIO41        | MAX98357A                    | LRC (左/右时钟)             |
| GPIO20        | LCD                          | MOSI (数据)                 |
| GPIO19        | LCD                          | SCLK (时钟)                 |
| GPIO47        | LCD                          | DC (数据选择)               |
| GPIO21        | LCD                          | RST (复位)                  |
| GPIO45        | LCD                          | CS (片选)                   |
| GPIO38        | LCD                          | BLK (背光)                  |
| GPIO17        | MAX30102                     | SDA (I2C 数据)              |
| GPIO18        | MAX30102                     | SCL (I2C 时钟)              |
| GPIO12        | 180° 舵机                    | PWM (信号线)                |
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

---

### 180° 舵机控制方案

> 180° 舵机通过 PWM 脉宽直接指定绝对角度（500-2500μs → 0-180°），**停止后自带保持力矩**，无需归位传感器，药盒关闭时转盘无法被外力旋转出槽。

**与 360° 舵机的关键区别**：

| 特性           | 180° 舵机（本方案）              | 360° 舵机（已舍弃）              |
|----------------|--------------------------------|---------------------------------|
| 控制方式       | PWM → 绝对角度                  | PWM → 转速和方向 → 时间控制     |
| 位置保持       | 自带保持力矩，外力推不动         | 停止后无保持力矩，可能被转出     |
| 归位传感器     | 不需要                          | 必须（微动开关等）              |
| 代码复杂度     | 低，`SetAngle()` 一句话到位      | 高，需 ISR + 超时检测 + 校准    |
| 可靠性         | 高                              | 中（依赖传感器和软件追踪）      |

**8 槽转盘布局**：

```
槽位 0 (logo) = 0°       ← 关闭/待机状态，正面展示 logo
槽位 1 =  25.7°          ← 药品 1
槽位 2 =  51.4°          ← 药品 2
槽位 3 =  77.1°          ← 药品 3
槽位 4 = 102.9°          ← 药品 4
槽位 5 = 128.6°          ← 药品 5
槽位 6 = 154.3°          ← 药品 6
槽位 7 = 180.0°          ← 药品 7

槽位间隔 = 180° / 7 ≈ 25.7°
```

```
           转盘俯视图（8 槽）
     ┌─────────────────────────┐
     │   \  7   0(logo)  1  /  │
     │    \                /   │
     │     6    ○转轴○    2    │  ← 取药口固定在上方
     │    /                \   │
     │   /   5         3    \  │
     │          4              │
     └─────────────────────────┘
```

**软件实现**（`servo_controller.h`）：

```cpp
class MedicineDispenser {
public:
    MedicineDispenser(gpio_num_t pwm_pin)
        : current_slot_(0) {
        // LEDC 定时器 (50Hz PWM, 用于舵机信号)
        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_cfg.duty_resolution = LEDC_TIMER_13_BIT;
        timer_cfg.timer_num = LEDC_TIMER_0;
        timer_cfg.freq_hz = 50;
        ledc_timer_config(&timer_cfg);

        ledc_channel_config_t ch_cfg = {};
        ch_cfg.gpio_num = pwm_pin;
        ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        ch_cfg.channel = LEDC_CHANNEL_0;
        ch_cfg.timer_sel = LEDC_TIMER_0;
        ch_cfg.duty = AngleToDuty(0);  // 初始: 槽位0 (logo)
        ledc_channel_config(&ch_cfg);

        GoToSlot(0);  // 上电归位
    }

    /**
     * 旋转到指定槽位 (0-7)
     * 槽位0 = logo展示/关闭状态, 槽位1-7 = 药品
     * 角度自动保持，无需传感器
     */
    void GoToSlot(int slot) {
        slot = std::clamp(slot, 0, 7);
        int angle = slot * 180 / 7;  // 0→0°, 1→25.7°, ..., 7→180°
        SetAngle(angle);
        current_slot_ = slot;
    }

    /** 返回 logo 展示位（关闭药盒） */
    void Close() { GoToSlot(0); }

    /** 打开指定药品槽 (1-7) */
    void Dispense(int slot) {
        if (slot < 1 || slot > 7) return;
        GoToSlot(slot);
    }

    int CurrentSlot() const { return current_slot_; }

private:
    static constexpr uint32_t MIN_PULSE_US = 500;   // 0° 对应脉宽
    static constexpr uint32_t MAX_PULSE_US = 2500;  // 180° 对应脉宽
    static constexpr uint32_t PWM_PERIOD_US = 20000; // 50Hz

    int current_slot_;

    // 角度 → LEDC duty (13-bit: 0-8191)
    static uint32_t AngleToDuty(int angle) {
        // pulse_us = 500 + (angle / 180.0) * 2000
        uint32_t pulse_us = MIN_PULSE_US +
            (uint32_t)(angle * 2000ULL / 180);
        // duty = pulse_us * 8191 / 20000
        return pulse_us * 8191 / PWM_PERIOD_US;
    }

    void SetAngle(int angle) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                      AngleToDuty(angle));
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
};

// 使用示例
MedicineDispenser dispenser(GPIO_NUM_12);
dispenser.GoToSlot(3);  // 旋转到 3 号药品槽
// ... 用户取药 ...
dispenser.Close();      // 回到 logo 展示位
```

**为什么选择 180° 舵机**：

1. **保持力矩**：PWM 信号持续输出，舵机自带位置保持，药盒关闭时即使外力旋转转盘也无法转动
2. **无传感器**：不需要归位开关、霍尔元件等额外硬件，减少故障点
3. **控制简单**：`SetAngle(angle)` 一行代码到位，无超时检测、无 ISR、无校准参数
4. **上电定位**：上电发送目标 PWM，舵机自动锁定到指定角度

---

### 舵机安装调试网页

在安装舵机前，必须将转盘对准 0°（logo 展示位）装入。板上电后舵机自动归零，可通过 Wi-Fi 网页二次校准。

**访问地址**：`http://<设备IP>/servo-debug`

**页面功能**：

| 功能       | 说明                                                    |
|------------|--------------------------------------------------------|
| 实时角度   | 显示当前角度和对应槽位号，每 2 秒自动刷新               |
| 滑块控制   | 0-180° 连续调节，拖拽实时响应                           |
| 步进微调   | ±1° / ±5° / ±10° 按钮，用于精确校准                    |
| 归零按钮   | 一键回到 0°（logo 展示位），安装前使用                  |
| 槽位直达   | 8 个槽位按钮（槽0 LOGO + 槽1-7 药品），点击直达目标槽位 |

**安装步骤**：

1. 设备上电，WiFi 配网后获取 IP
2. 浏览器访问 `http://<IP>/servo-debug`
3. 点击「归零 (安装位)」确认舵机在 0°
4. 将转盘槽位 0（logo 面）对准取药口装入
5. 用槽位按钮逐槽验证对准情况

**API 接口**（`servo_debug_server.h`）：

| 方法   | 路径              | 说明               | 请求体                |
|--------|-------------------|--------------------|-----------------------|
| GET    | `/servo-debug`    | 调试网页           | —                     |
| GET    | `/api/servo/angle`| 获取当前角度和槽位 | —                     |
| POST   | `/api/servo/angle`| 设置绝对角度       | `{"angle": 90}`       |
| POST   | `/api/servo/zero` | 归零到槽位 0       | —                     |
| POST   | `/api/servo/slot` | 转到指定槽位       | `{"slot": 3}`         |
