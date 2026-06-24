#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <driver/ledc.h>
#include <cmath>
#include <memory>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif

#define TAG "SmartRobotBoard"

// ============================================================
// DRV8833 电机驱动 — 单路 H 桥控制
//
//   DRV8833 真值表:
//     IN1=0   IN2=0   → Coast (滑行停止)
//     IN1=PWM IN2=0   → 正转
//     IN1=0   IN2=PWM → 反转
//     IN1=1   IN2=1   → Brake (刹车)
//
//   LEDC 10-bit PWM @ 5kHz 实现调速
// ============================================================
class MotorController {
private:
    gpio_num_t in1_pin_, in2_pin_;
    ledc_channel_t ch_in1_, ch_in2_;
    int current_duty_;      // 0~1023 (10-bit)
    int current_dir_;       // 1=正转, -1=反转, 0=停止

public:
    // 默认构造（不分配硬件）
    MotorController()
        : in1_pin_(GPIO_NUM_NC), in2_pin_(GPIO_NUM_NC),
          ch_in1_(LEDC_CHANNEL_MAX), ch_in2_(LEDC_CHANNEL_MAX),
          current_duty_(0), current_dir_(0) {}

    // 初始化硬件资源
    void Init(gpio_num_t in1, gpio_num_t in2, ledc_channel_t ch1, ledc_channel_t ch2, ledc_timer_t timer) {
        in1_pin_ = in1; in2_pin_ = in2;
        ch_in1_ = ch1; ch_in2_ = ch2;

        ledc_channel_config_t cfg1 = {};
        cfg1.gpio_num = in1_pin_;
        cfg1.speed_mode = LEDC_LOW_SPEED_MODE;
        cfg1.channel = ch_in1_;
        cfg1.timer_sel = timer;
        cfg1.duty = 0;
        cfg1.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&cfg1));

        ledc_channel_config_t cfg2 = {};
        cfg2.gpio_num = in2_pin_;
        cfg2.speed_mode = LEDC_LOW_SPEED_MODE;
        cfg2.channel = ch_in2_;
        cfg2.timer_sel = timer;
        cfg2.duty = 0;
        cfg2.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&cfg2));
    }

    /// speed: 0~100（百分比）, dir: 1=正转, -1=反转
    void Run(int speed, int dir) {
        if (speed < 0) speed = 0;
        if (speed > 100) speed = 100;
        int duty = (speed * 1023) / 100;
        current_duty_ = duty;
        current_dir_ = dir;

        if (dir == 1) {
            // 正转: IN1=PWM, IN2=0
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_in1_, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_in1_));
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_in2_, 0));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_in2_));
        } else if (dir == -1) {
            // 反转: IN1=0, IN2=PWM
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_in1_, 0));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_in1_));
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_in2_, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_in2_));
        } else {
            Stop();
        }
    }

    void Stop() {
        current_duty_ = 0;
        current_dir_ = 0;
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_in1_, 0));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_in1_));
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_in2_, 0));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_in2_));
    }

    int GetSpeed() const { return (current_duty_ * 100) / 1023; }
    int GetDir() const { return current_dir_; }
};

// ============================================================
// 舵机控制 — LEDC PWM, 50Hz, 0.5~2.5ms → 0°~180°
//   独立的 Timer + Channel，不与电机共享
// ============================================================
class ServoController {
private:
    gpio_num_t pin_;
    ledc_channel_t channel_;
    int current_angle_;

    static constexpr int MIN_PULSE_US = 500;
    static constexpr int MAX_PULSE_US = 2500;

    int AngleToDuty(int angle) {
        if (angle < 0) angle = 0;
        if (angle > 180) angle = 180;
        float pulse_us = MIN_PULSE_US + (float)(MAX_PULSE_US - MIN_PULSE_US) * angle / 180.0f;
        // 16-bit timer @ 50Hz → period=65536, 每μs=65536/20000≈3.2768
        // 14-bit → max_duty=16384, 每μs=16384/20000≈0.8192
        return (int)(pulse_us * 16384.0f / 20000.0f);
    }

public:
    ServoController() : pin_(GPIO_NUM_NC), channel_(LEDC_CHANNEL_MAX), current_angle_(90) {}

    void Init(gpio_num_t pin, ledc_channel_t ch, ledc_timer_t timer) {
        pin_ = pin;
        channel_ = ch;
        if (pin_ == GPIO_NUM_NC) return;

        ledc_channel_config_t cfg = {};
        cfg.gpio_num = pin_;
        cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        cfg.channel = channel_;
        cfg.timer_sel = timer;
        cfg.duty = AngleToDuty(90);
        cfg.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&cfg));
        current_angle_ = 90;
    }

    void SetAngle(int angle) {
        if (pin_ == GPIO_NUM_NC) return;
        int duty = AngleToDuty(angle);
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_));
        current_angle_ = angle;
        ESP_LOGI(TAG, "舵机 GPIO%d → %d°", pin_, angle);
    }

    int GetAngle() const { return current_angle_; }
};

class SmartRobotBoard : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;

    // 电机（用指针延迟构造，避免无效初始化）
    std::unique_ptr<MotorController> motor_a_;
    std::unique_ptr<MotorController> motor_b_;

    // 舵机（最多2路）
    ServoController servo_[2];
    int servo_count_;

    // ---- 舵机初始化（独立 Timer2 + CH4~5） ----
    void InitializeServos() {
        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_cfg.duty_resolution = LEDC_TIMER_14_BIT;  // ESP32-S3 最大 14-bit
        timer_cfg.timer_num = LEDC_TIMER_2;
        timer_cfg.freq_hz = 50;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

        gpio_num_t servo_pins[] = {SERVO1_PWM_PIN, SERVO2_PWM_PIN};
        ledc_channel_t ch_map[] = {LEDC_CHANNEL_4, LEDC_CHANNEL_5};  // 避免与电机 CH0~3 冲突
        servo_count_ = 0;
        for (int i = 0; i < 2; i++) {
            if (servo_pins[i] == GPIO_NUM_NC) continue;
            servo_[i].Init(servo_pins[i], ch_map[i], LEDC_TIMER_2);
            servo_count_++;
        }
        ESP_LOGI(TAG, "舵机初始化完成 (%d路 / 50Hz)", servo_count_);
    }

    // ---- 电机 PWM 初始化（Timer3 + CH0~3，避免与背光 Timer0 冲突） ----
    void InitializeMotors() {
        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
        timer_cfg.timer_num = LEDC_TIMER_3;
        timer_cfg.freq_hz = 5000;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

        // Motor A: IO1(AIN1)=CH0, IO2(AIN2)=CH1 → TT马达1
        motor_a_ = std::make_unique<MotorController>();
        motor_a_->Init(MOTOR_A_IN1_PIN, MOTOR_A_IN2_PIN,
                       LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_TIMER_3);

        // Motor B: IO10(BIN1)=CH2, IO11(BIN2)=CH3 → TT马达2
        motor_b_ = std::make_unique<MotorController>();
        motor_b_->Init(MOTOR_B_IN1_PIN, MOTOR_B_IN2_PIN,
                       LEDC_CHANNEL_2, LEDC_CHANNEL_3, LEDC_TIMER_3);

        ESP_LOGI(TAG, "电机 PWM 初始化完成 (DRV8833 / 5kHz / Timer3 CH0-3)");
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

    // ---- MCP 远程控制工具 ----
    // 工具描述面向 AI 大模型，需用中文注明触发场景，
    // 让模型看到描述后能正确映射 "前进"→forward、"后退"→backward 等。
    void RegisterMcpTools() {
        auto& mcp = McpServer::GetInstance();

        // ========== 电机：移动 ==========
        mcp.AddTool(
            "self.motor.forward",
            "小车向前行驶（双轮同向正转）。适用指令：前进、往前走、向前移动、直行、冲。"
            "speed: 速度百分比 0-100，默认 50",
            PropertyList({Property("speed", kPropertyTypeInteger, 50, 0, 100)}),
            [this](const PropertyList& props) -> ReturnValue {
                int speed = props["speed"].value<int>();
                motor_a_->Run(speed, 1);
                motor_b_->Run(speed, 1);
                ESP_LOGI(TAG, "MCP forward speed=%d", speed);
                return true;
            });

        mcp.AddTool(
            "self.motor.backward",
            "小车向后行驶（双轮同向反转）。适用指令：后退、往后退、倒车、向后移动。"
            "speed: 速度百分比 0-100，默认 50",
            PropertyList({Property("speed", kPropertyTypeInteger, 50, 0, 100)}),
            [this](const PropertyList& props) -> ReturnValue {
                int speed = props["speed"].value<int>();
                motor_a_->Run(speed, -1);
                motor_b_->Run(speed, -1);
                ESP_LOGI(TAG, "MCP backward speed=%d", speed);
                return true;
            });

        mcp.AddTool(
            "self.motor.turn_left",
            "原地左转（左轮反转，右轮正转）。适用指令：左转、向左转、往左拐、转左边。"
            "speed: 速度百分比 0-100，默认 50",
            PropertyList({Property("speed", kPropertyTypeInteger, 50, 0, 100)}),
            [this](const PropertyList& props) -> ReturnValue {
                int speed = props["speed"].value<int>();
                motor_a_->Run(speed, -1);
                motor_b_->Run(speed, 1);
                ESP_LOGI(TAG, "MCP turn_left speed=%d", speed);
                return true;
            });

        mcp.AddTool(
            "self.motor.turn_right",
            "原地右转（左轮正转，右轮反转）。适用指令：右转、向右转、往右拐、转右边。"
            "speed: 速度百分比 0-100，默认 50",
            PropertyList({Property("speed", kPropertyTypeInteger, 50, 0, 100)}),
            [this](const PropertyList& props) -> ReturnValue {
                int speed = props["speed"].value<int>();
                motor_a_->Run(speed, 1);
                motor_b_->Run(speed, -1);
                ESP_LOGI(TAG, "MCP turn_right speed=%d", speed);
                return true;
            });

        mcp.AddTool(
            "self.motor.stop",
            "立即停止所有电机运动。适用指令：停下、停止、刹车、别动了、站住。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                motor_a_->Stop();
                motor_b_->Stop();
                ESP_LOGI(TAG, "MCP stop");
                return true;
            });

        // ========== 舵机 ==========
        mcp.AddTool(
            "self.servo.set_angle",
            "控制机器人头部/手臂舵机转到指定角度。适用指令：抬头、低头、向左看、向右看、举手。"
            "servo_id: 1=舵机1(GPIO48), 2=舵机2(GPIO13)。angle: 0-180度",
            PropertyList({
                Property("servo_id", kPropertyTypeInteger, 1, 1, 2),
                Property("angle", kPropertyTypeInteger, 90, 0, 180)
            }),
            [this](const PropertyList& props) -> ReturnValue {
                int id = props["servo_id"].value<int>() - 1;
                int angle = props["angle"].value<int>();
                if (id < 0 || id >= servo_count_) {
                    char err[64];
                    snprintf(err, sizeof(err), "舵机ID无效，可用范围: 1~%d", servo_count_);
                    return std::string(err);
                }
                servo_[id].SetAngle(angle);
                return true;
            });

        mcp.AddTool(
            "self.servo.get_angle",
            "查询某个舵机当前角度。servo_id: 1=舵机1, 2=舵机2",
            PropertyList({Property("servo_id", kPropertyTypeInteger, 1, 1, 2)}),
            [this](const PropertyList& props) -> ReturnValue {
                int id = props["servo_id"].value<int>() - 1;
                if (id < 0 || id >= servo_count_) return std::string("舵机ID无效");
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"servo_id\":%d,\"angle\":%d}",
                         id + 1, servo_[id].GetAngle());
                return std::string(buf);
            });

        // ========== 状态 ==========
        mcp.AddTool(
            "self.status",
            "获取机器人完整运行状态：包括左右电机的转速/方向，以及两个舵机的当前角度。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "{\"motor_a\":{\"speed\":%d,\"dir\":%d},"
                    "\"motor_b\":{\"speed\":%d,\"dir\":%d},"
                    "\"servo1\":%d,\"servo2\":%d}",
                    motor_a_->GetSpeed(), motor_a_->GetDir(),
                    motor_b_->GetSpeed(), motor_b_->GetDir(),
                    servo_[0].GetAngle(), servo_[1].GetAngle());
                return std::string(buf);
            });

        ESP_LOGI(TAG, "MCP 工具注册完成 (电机×5 + 舵机×2 + 状态×1)");
    }

public:
    SmartRobotBoard()
        : boot_button_(BOOT_BUTTON_GPIO),
          servo_count_(0)
    {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeServos();
        InitializeMotors();
        InitializeTools();
        RegisterMcpTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        ESP_LOGI(TAG, "AI瓦力机器人初始化完成");
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(SmartRobotBoard);
