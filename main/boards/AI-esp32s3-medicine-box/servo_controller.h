#ifndef _SERVO_CONTROLLER_H_
#define _SERVO_CONTROLLER_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define SERVO_TAG "ServoController"

// MG90S 舵机控制类
// 180° 行程均分为 8 个位置，相邻间隔 22.5°
// 位置 0 (闭口): 0°, 位置 1: 22.5°, ..., 位置 7: 157.5°
class ServoController {
private:
    gpio_num_t pwm_pin_;
    int current_slot_;  // 0-7
    bool is_moving_;

    static constexpr uint32_t SERVO_FREQ_HZ = 50;          // 50Hz 标准舵机频率
    static constexpr ledc_timer_t LEDC_TIMER = LEDC_TIMER_0;
    static constexpr ledc_channel_t LEDC_CHANNEL = LEDC_CHANNEL_0;
    static constexpr uint32_t DUTY_MAX = 8191;             // 13-bit resolution
    static constexpr uint32_t PULSE_MIN_US = 500;          // 0° pulse width
    static constexpr uint32_t PULSE_MAX_US = 2500;         // 180° pulse width
    static constexpr uint32_t PERIOD_US = 20000;           // 20ms = 50Hz period
    static constexpr uint32_t SLOT_ANGLE_STEP = 225;       // 22.5° per slot (in tenths of degree)
    static constexpr uint32_t MOVE_DELAY_MS = 500;         // 转动稳定延时

    // 角度转 duty cycle
    uint32_t AngleToDuty(uint32_t angle_tenth_deg) {
        // angle in 0.1 degrees, range 0 to 1800 (0° to 180°)
        uint32_t pulse_us = PULSE_MIN_US + (angle_tenth_deg * (PULSE_MAX_US - PULSE_MIN_US)) / 1800;
        return (pulse_us * DUTY_MAX) / PERIOD_US;
    }

public:
    ServoController(gpio_num_t pin)
        : pwm_pin_(pin)
        , current_slot_(0)
        , is_moving_(false)
    {
        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_cfg.timer_num = LEDC_TIMER;
        timer_cfg.duty_resolution = LEDC_TIMER_13_BIT;
        timer_cfg.freq_hz = SERVO_FREQ_HZ;
        timer_cfg.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

        ledc_channel_config_t channel_cfg = {};
        channel_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        channel_cfg.channel = LEDC_CHANNEL;
        channel_cfg.timer_sel = LEDC_TIMER;
        channel_cfg.intr_type = LEDC_INTR_DISABLE;
        channel_cfg.gpio_num = pwm_pin_;
        channel_cfg.duty = 0;
        channel_cfg.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));

        // 初始化位置为闭口(位置0)
        MoveToSlot(0);
        ESP_LOGI(SERVO_TAG, "舵机初始化完成，当前闭口位置");
    }

    ~ServoController() {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 0);
    }

    bool IsMoving() const { return is_moving_; }
    int GetCurrentSlot() const { return current_slot_; }

    // 移动到指定药槽 (1-7)，0 表示闭口
    void MoveToSlot(int slot) {
        if (slot < 0 || slot > 7) {
            ESP_LOGE(SERVO_TAG, "无效的槽号: %d (有效范围 0-7)", slot);
            return;
        }

        is_moving_ = true;
        uint32_t angle = slot * SLOT_ANGLE_STEP;  // 以 0.1° 为单位: 0, 225, 450, ...
        uint32_t duty = AngleToDuty(angle);

        ESP_LOGI(SERVO_TAG, "转向槽 %d (角度 %lu.%lu°)", slot,
                 angle / 10, angle % 10);

        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL));

        vTaskDelay(pdMS_TO_TICKS(MOVE_DELAY_MS));

        current_slot_ = slot;
        is_moving_ = false;
        ESP_LOGI(SERVO_TAG, "已到达槽 %d", slot);
    }

    // 关闭药盒 (移动到闭口位置)
    void Close() {
        MoveToSlot(0);
    }
};

#endif // _SERVO_CONTROLLER_H_
