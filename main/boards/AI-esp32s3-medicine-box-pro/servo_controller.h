#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG_SERVO "Servo"

class MedicineDispenser {
public:
    MedicineDispenser(gpio_num_t pwm_pin, gpio_num_t home_switch_pin)
        : pwm_pin_(pwm_pin), home_switch_pin_(home_switch_pin) {

        ledc_timer_config_t timer_cfg = {};
        timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_cfg.duty_resolution = LEDC_TIMER_13_BIT;
        timer_cfg.timer_num = LEDC_TIMER_2;
        timer_cfg.freq_hz = 50;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

        ledc_channel_config_t ch_cfg = {};
        ch_cfg.gpio_num = pwm_pin_;
        ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        ch_cfg.channel = LEDC_CHANNEL_2;
        ch_cfg.timer_sel = LEDC_TIMER_2;
        ch_cfg.duty = PulseToDuty(STOP_US);
        ch_cfg.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

        gpio_config_t home_cfg = {};
        if (home_switch_pin_ != GPIO_NUM_NC) {
            home_cfg.pin_bit_mask = (1ULL << home_switch_pin_);
            home_cfg.mode = GPIO_MODE_INPUT;
            home_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            ESP_ERROR_CHECK(gpio_config(&home_cfg));
        }

        ESP_LOGI(TAG_SERVO, "360°舵机调试模式 (PWM: GPIO%d, 开关: GPIO%d)",
                 pwm_pin_, home_switch_pin_);
    }

    ~MedicineDispenser() { SendStopPulse(); }

    // === 兼容旧 API ===
    bool Calibrate(int = 0) { ESP_LOGI(TAG_SERVO, "调试模式，跳过校准"); return true; }
    int    CurrentSlot()  const { return 0; }
    bool   IsCalibrated() const { return true; }
    void   GoToSlot(int)  {}
    void   Close()        { Stop(); }
    void   SetCurrentSlot(int) {}
    uint32_t GetSlotPulse(int) const { return current_pulse_; }
    void     SetSlotPulse(int, uint32_t) {}

    // === 360° 舵机核心控制 ===

    /// @brief 设置转速偏差 (相对由 stop_trim 调整后的中心)
    void Rotate(int dev) {
        if (dev < -MAX_DEV) dev = -MAX_DEV;
        if (dev >  MAX_DEV) dev =  MAX_DEV;

        // 在死区范围内 → 视为停止
        if (abs(dev) <= dead_band_) {
            SendStopPulse();
            moving_ = false;
            prev_dev_ = 0;
            return;
        }

        // 方向切换: 先短暂停止让舵机刹车，再反向 (保护齿轮)
        if (moving_ && (dev > 0) != (prev_dev_ > 0)) {
            ESP_LOGI(TAG_SERVO, "方向切换 %+d→%+d，暂停 200ms...", prev_dev_, dev);
            SendStopPulse();
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        current_pulse_ = EffectiveCenter() + dev;
        SetPwm(current_pulse_);
        prev_dev_ = dev;
        moving_ = true;
        ESP_LOGI(TAG_SERVO, "旋转: %+dμs (%.0f°/s, PWM=%luμs)",
                 dev, GetSpeedEstimate(), current_pulse_);
    }

    void Stop() {
        SendStopPulse();
        moving_ = false;
        prev_dev_ = 0;
        ESP_LOGI(TAG_SERVO, "停止 (PWM=%luμs)", current_pulse_);
    }

    void RotatePulse(int delta) {
        int pulse = (int)current_pulse_ + delta;
        int lo = (int)STOP_US - MAX_DEV;
        int hi = (int)STOP_US + MAX_DEV;
        if (pulse < lo) pulse = lo;
        if (pulse > hi) pulse = hi;
        current_pulse_ = (uint32_t)pulse;
        SetPwm(current_pulse_);
        moving_ = (current_pulse_ != EffectiveCenter());
        prev_dev_ = GetDeviation();
    }

    // === 死区与中心微调 ===

    int      GetDeadBand()  const { return dead_band_; }
    void     SetDeadBand(int us) {
        if (us < 0) us = 0;
        if (us > 30) us = 30;
        dead_band_ = us;
        ESP_LOGI(TAG_SERVO, "死区: ±%dμs", dead_band_);
    }

    int      GetStopTrim()  const { return stop_trim_; }
    void     SetStopTrim(int us) {
        if (us < -50) us = -50;
        if (us > 50) us = 50;
        stop_trim_ = us;
        ESP_LOGI(TAG_SERVO, "停止中心: %luμs (偏移 %+d)",
                 EffectiveCenter(), stop_trim_);
    }

    // === 速度因子 (μs → °/s 换算) ===

    float    GetSpeedFactor() const { return speed_factor_; }
    void     SetSpeedFactor(float f) {
        if (f < 0.1f) f = 0.1f;
        if (f > 1.0f) f = 1.0f;
        speed_factor_ = f;
        ESP_LOGI(TAG_SERVO, "速度因子: %.2f°/s/μs (最大 %.0f°/s)",
                 speed_factor_, MAX_DEV * speed_factor_);
    }
    float    GetSpeedEstimate() const {
        return abs(GetDeviation()) * speed_factor_;
    }

    uint32_t EffectiveCenter() const { return STOP_US + stop_trim_; }
    uint32_t GetPulse()        const { return current_pulse_; }
    int      GetDeviation()    const { return (int)current_pulse_ - (int)EffectiveCenter(); }
    bool     IsMoving()        const { return moving_; }
    bool     IsHomeTriggered() const { return home_switch_pin_ != GPIO_NUM_NC && gpio_get_level(home_switch_pin_) == 0; }

    static constexpr uint32_t STOP_US = 1500;
    static constexpr int      MAX_DEV = 300;

private:
    static constexpr uint32_t PERIOD_US = 20000;

    gpio_num_t pwm_pin_;
    gpio_num_t home_switch_pin_;
    uint32_t   current_pulse_ = STOP_US;
    bool       moving_ = false;
    int        prev_dev_  = 0;    // 上一次偏差, 用于检测方向切换
    int        dead_band_ = 0;     // ±μs 死区, |dev|<=此值视为停止
    int        stop_trim_ = 0;     // 停止中心偏移 (-50~+50)
    float      speed_factor_ = 0.3f; // μs→°/s 换算系数

    void SendStopPulse() {
        current_pulse_ = EffectiveCenter();
        SetPwm(current_pulse_);
    }

    static uint32_t PulseToDuty(uint32_t pulse_us) {
        return pulse_us * 8191 / PERIOD_US;
    }

    void SetPwm(uint32_t pulse_us) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, PulseToDuty(pulse_us));
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
    }
};

#endif
