## 硬件方案

### 物料清单

| 序号 | 物料               | 型号/规格                            | 数量   | 用途             |
|------|--------------------|--------------------------------------|--------|------------------|
| 1    | ESP32-S3 开发板    | DevKitC-1 (16MB Flash + 8MB PSRAM)   | 1      | 运行小智         |
| 2    | K230D AI 视觉模组  | CanMV K230D + GC2093 摄像头          | 1      | 本地人脸识别     |
| 3    | I2S 数字麦克风     | INMP441                              | 1      | 语音输入         |
| 4    | I2S 数字功放       | MAX98357A                            | 1      | 语音输出         |
| 5    | 喇叭               | 3W / 4Ω                              | 1      | 语音播报         |
| 6    | LCD 显示屏         | ST7789 240x240 SPI 1.54寸 (8Pin)      | 1      | 信息展示         |
| 7    | 心率血氧传感器     | MAX30102                             | 1      | 健康检测         |
| 8    | 360° 连续旋转舵机   | DS04-NFC / 360° 改装舵机              | 1      | 药品转盘驱动     |
| 9    | LED                | 5mm 红色 LED ×2                       | 2      | 状态指示 (GPIO48) + 调度指示 (GPIO3) |
| 10   | 轻触开关           | 6x6mm                                | 3      | 物理按键 (BOOT + 静音 + 2× 微动开关) |
| 11   | 微动开关           | KW12-3                               | 2      | 转盘定位: SW0(槽0) GPIO1, SW1(槽1~7) GPIO2 |
| 12   | 3D 打印药盒外壳    | PLA / 8 槽转盘结构 (1 logo + 7 药)   | 1 套   | 机械结构         |
| 13   | 淘晶驰串口屏       | TJC X2系列 7寸 电容触摸 (UART)       | 1      | 本地配置后台     |
| 14   | 杜邦线 + 面包板    | —                                    | 若干   | 原型验证         |

### 引脚接线

#### XiaoZhi ESP32-S3 ↔ 外设

| ESP32-S3 引脚 | 外设                         | 信号                        |
|:-------------:|------------------------------|-----------------------------|
| GPIO4         | INMP441                      | WS (数据选择)               |
| GPIO5         | INMP441                      | SCK (时钟)                  |
| GPIO6        | INMP441                      | SD (数据)                   |
| GPIO7        | MAX98357A                    | DIN (数字信号)              |
| GPIO15        | MAX98357A                    | BCLK (位时钟)               |
| GPIO16        | MAX98357A                    | LRC (左/右时钟)             |
| GPIO20        | LCD                          | MOSI (数据)                 |
| GPIO19        | LCD                          | SCLK (时钟)                 |
| GPIO47        | LCD                          | DC (数据选择)               |
| GPIO21        | LCD                          | RST (复位)                  |
| GPIO45        | LCD                          | CS (片选)                   |
| GPIO38        | LCD                          | BLK (背光)                  |
| GPIO17        | MAX30102                     | SDA (I2C 数据)              |
| GPIO18        | MAX30102                     | SCL (I2C 时钟)              |
| GPIO12        | 360° 舵机                    | PWM (信号线)                |
| GPIO1         | 微动开关 SW0                 | 槽0 归零检测 (内部上拉)     |
| GPIO2         | 微动开关 SW1                 | 槽1~7 计数 (内部上拉)       |
| GPIO10        | 静音/确认按键                | 停止提醒 + 转盘归零 (内部上拉) |
| GPIO3         | 调度器 LED                   | 阳极 (通过 220Ω 限流)       |
| GPIO48        | LED                          | 阳极 (通过 220Ω 限流)       |
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

### 360° 舵机 + 双微动开关定位方案

> 本方案使用 **360° 连续旋转舵机**（只能顺时针），通过两个微动开关实现 8 槽精确定位。SW0 专用于检测槽位 0（归零位），SW1 用于槽位 1~7 的经过计数。每次上电自动执行寻零流程。

**360° 舵机特性**：

| 特性           | 360° 连续旋转舵机                     |
|----------------|--------------------------------------|
| 控制方式       | PWM → 转速 (脉宽偏离 1500μs 越远转速越快) |
| 停止方式       | PWM = 1500μs 停止，无保持力矩          |
| 位置检测       | 必须通过微动开关（SW0/SW1）            |
| 方向限制       | 只能顺时针旋转                         |
| 代码复杂度     | 高（消抖 + 定时器轮询 + 超时 + 跨圈）   |
| 可靠性         | 高（硬件传感器闭环，无累积误差）         |

**8 槽转盘布局**：

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

SW0 → 仅槽位0触发 (归零位 / logo展示位)
SW1 → 槽位1~7 每经过一个触发一次计数
```

**舵机驱动**（`servo_controller.h`）：

```cpp
class MedicineDispenser {
public:
    MedicineDispenser(gpio_num_t pwm_pin, gpio_num_t feedback_pin);

    // 360° 舵机: dev 偏离 1500μs 的幅度决定转速
    // dev=0 → 停止, dev>0 → 顺时针, dev<0 → 逆时针
    void Rotate(int dev);   // PWM = 1500 + dev
    void Stop();            // PWM = 1500 (停止)
};

// 速度档位:
// HOMING_DEV = 100  (慢速寻零)
// NORMAL_DEV = 250  (正常转槽)
```

**转盘控制**（`pillbox_turntable.h`）：

```cpp
class PillBoxTurntable {
public:
    PillBoxTurntable(MedicineDispenser* dispenser,
                     gpio_num_t sw0_pin, gpio_num_t sw1_pin);

    void init();           // 上电寻零 (非阻塞, 后台完成)
    void goToSlot(int n);  // 转到指定药槽 0~7 (跨圈自动处理)
    void goHome();         // 寻 SW0 回到槽位 0
    int  getCurrentSlot(); // 获取当前槽位
    bool isReady();        // 寻零是否完成

private:
    // 5ms 定时器轮询 SW0/SW1, 20ms 消抖, 150ms 冷却
    // 10s 超时保护
    // SW0 触发自动清零 sw1_counter
};
```

**使用示例**：

```cpp
auto* dispenser = new MedicineDispenser(SERVO_PWM_PIN, GPIO_NUM_NC);
auto* turntable = new PillBoxTurntable(dispenser,
                                       TURNTABLE_SW0_PIN,  // GPIO1
                                       TURNTABLE_SW1_PIN); // GPIO2
turntable->init();       // 启动寻零 (后台运行)

// ... 寻零完成后 ...
turntable->goToSlot(3);  // 顺时针转至槽位3 (自动跨圈)
turntable->goHome();     // 回到槽位0

// 用药提醒流程:
// 1. turntable->goToSlot(slot) → 转盘转至目标槽
// 2. 用户取药, 按下 GPIO10 按钮
// 3. turntable->goHome() → 转盘归零
```

**为什么选择 360° 舵机 + 微动开关**：

1. **无累积误差**：每次经过 SW0 自动清零计数器，永不累积偏移
2. **闭环定位**：SW1 硬件计数，不依赖时间估算，不受电压/负载波动影响
3. **跨圈安全**：只能顺时针 + SW0 复位机制，多圈旋转也不会丢失位置
4. **上电寻零**：每次开机自动寻 SW0 归零，无需记忆断电位置

---

### 舵机安装调试网页

在安装舵机前，必须将转盘对准槽 0（logo 展示位）装入。板上电后舵机自动寻零，可通过 Wi-Fi 网页调试。

**访问地址**：`http://<设备IP>/servo-debug`

**页面功能**：

| 功能       | 说明                                                    |
|------------|--------------------------------------------------------|
| 实时状态   | 显示舵机转速偏差 (dev μs) 和当前槽位                     |
| 滑块控制   | -300 ~ +300 μs 连续调节转速，拖拽实时响应               |
| 步进微调   | ±1 / ±5 / ±10 / ±50 / ±100 μs 按钮                     |
| 停转按钮   | 一键发送 1500μs 停止舵机                               |
| 速度档位   | 预设寻零(100) / 正常(250) / 快速(400) μs               |
| 归零       | 启动寻零流程，自动寻找 SW0                              |

**API 接口**（`servo_debug_server.h`）：

| 方法   | 路径              | 说明               | 请求体                |
|--------|-------------------|--------------------|-----------------------|
| GET    | `/servo-debug`    | 调试网页           | —                     |
| GET    | `/api/servo/angle`| 获取当前偏差和槽位 | —                     |
| POST   | `/api/servo/angle`| 设置转速偏差       | `{"dev": 250}`        |
| POST   | `/api/servo/zero` | 启动寻零           | —                     |
| POST   | `/api/servo/slot` | 转到指定槽位       | `{"slot": 3}`         |
