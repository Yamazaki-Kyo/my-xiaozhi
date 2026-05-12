#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <driver/ledc.h>
#include <esp_log.h>

class MedicineDispenser {
public:
    MedicineDispenser(gpio_num_t pwm_pin) : current_slot_(0) {
        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_cfg.duty_resolution = LEDC_TIMER_13_BIT;
        timer_cfg.timer_num = LEDC_TIMER_2;
        timer_cfg.freq_hz = 50;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

        ledc_channel_config_t ch_cfg = {};
        ch_cfg.gpio_num = pwm_pin;
        ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        ch_cfg.channel = LEDC_CHANNEL_2;
        ch_cfg.timer_sel = LEDC_TIMER_2;
        ch_cfg.duty = AngleToDuty(0);
        ch_cfg.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    }

    /// @brief 设置绝对角度 (0-180°)，PWM 500-2500μs
    void SetAngle(int angle) {
        if (angle < 0) angle = 0;
        if (angle > 180) angle = 180;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2,
                      AngleToDuty(angle));
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
        current_angle_ = angle;
    }

    int GetAngle() const { return current_angle_; }

    /// @brief 旋转到指定槽位 (0=logo, 1-7=药品)
    void GoToSlot(int slot) {
        if (slot < 0) slot = 0;
        if (slot > 7) slot = 7;
        int angle = slot * 180 / 7;
        SetAngle(angle);
        current_slot_ = slot;
    }

    int CurrentSlot() const { return current_slot_; }

    /// @brief 归零：设置舵机到 0°（logo 展示位），安装时使用
    void Zero() { GoToSlot(0); }

    /// @brief 关闭药盒（回到 logo 展示位）
    void Close() { GoToSlot(0); }

private:
    static constexpr uint32_t MIN_US = 500;
    static constexpr uint32_t MAX_US = 2500;
    static constexpr uint32_t PERIOD_US = 20000;

    int current_slot_;
    int current_angle_ = 0;

    /// @brief 角度 → 13-bit duty: pulse_us = 500 + angle/180*2000, duty = pulse*8191/20000
    static uint32_t AngleToDuty(int angle) {
        uint32_t pulse_us = MIN_US + (uint32_t)(angle * 2000ULL / 180);
        return pulse_us * 8191 / PERIOD_US;
    }
};

#endif
