#ifndef PILLBOX_TURNTABLE_H
#define PILLBOX_TURNTABLE_H

#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "servo_controller.h"

#define TAG_PBT "PillBoxTurntable"

class PillBoxTurntable {
public:
    PillBoxTurntable(MedicineDispenser* dispenser, gpio_num_t sw0_pin, gpio_num_t sw1_pin)
        : dispenser_(dispenser), sw0_pin_(sw0_pin), sw1_pin_(sw1_pin) {

        gpio_config_t io_cfg = {};
        io_cfg.pin_bit_mask = (1ULL << sw0_pin_) | (1ULL << sw1_pin_);
        io_cfg.mode = GPIO_MODE_INPUT;
        io_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        io_cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_cfg));

        esp_timer_create_args_t poll_args = {};
        poll_args.callback = PollCallback;
        poll_args.arg = this;
        poll_args.dispatch_method = ESP_TIMER_TASK;
        poll_args.name = "pbt_poll";
        esp_timer_create(&poll_args, &poll_timer_);

        ESP_LOGI(TAG_PBT, "转盘模块就绪 (SW0:GPIO%d, SW1:GPIO%d)", sw0_pin_, sw1_pin_);
    }

    ~PillBoxTurntable() {
        stopMotor();
        if (poll_timer_) {
            esp_timer_stop(poll_timer_);
            esp_timer_delete(poll_timer_);
            poll_timer_ = nullptr;
        }
    }

    void init() {
        if (is_homing_ || ready_) {
            ESP_LOGW(TAG_PBT, "已在寻零中或已就绪, 忽略重复init");
            return;
        }

        // 多次采样消抖确认 SW0 是否已触发 (避免单次 GPIO 噪声误判)
        int sw0_low = 0;
        for (int i = 0; i < 16; i++) {
            if (gpio_get_level(sw0_pin_) == 0) sw0_low++;
            vTaskDelay(pdMS_TO_TICKS(2));  // 共 32ms
        }
        if (sw0_low >= 14) {
            ESP_LOGI(TAG_PBT, "SW0 消抖确认触发, 已在归零点, 跳过寻零");
            current_slot_ = 0;
            sw1_counter_ = 0;
            ready_ = true;
            return;
        }

        ESP_LOGI(TAG_PBT, "SW0 未触发 (低电平 %d/16), 开始寻零流程...", sw0_low);
        performHoming();
    }

    void goToSlot(int n) {
        if (!ready_) {
            ESP_LOGW(TAG_PBT, "尚未完成寻零, 无法转槽");
            return;
        }
        if (n < 0 || n > 7) {
            ESP_LOGW(TAG_PBT, "无效槽号: %d", n);
            return;
        }
        if (is_moving_) {
            ESP_LOGW(TAG_PBT, "正在运动中, 忽略goToSlot(%d)", n);
            return;
        }
        if (n == current_slot_) {
            ESP_LOGI(TAG_PBT, "已在槽%d, 无需移动", n);
            return;
        }
        if (n == 0) {
            goHome();
            return;
        }

        ESP_LOGI(TAG_PBT, "转槽: %d → %d", current_slot_, n);
        esp_timer_stop(poll_timer_);
        target_slot_ = n;
        sw1_counter_ = current_slot_;
        // 舵机启动前初始化消抖 + 300ms 冷却抑制 EMI, poll_timer_ 先于舵机开始检测
        resetDebounce(sw1_db_, sw1_pin_, STARTUP_COOLDOWN);
        esp_timer_start_periodic(poll_timer_, POLL_INTERVAL_US);
        is_moving_ = true;
        movement_start_time_ = esp_timer_get_time();
        startMotor(NORMAL_DEV);
    }

    void goHome() {
        if (!ready_) {
            ESP_LOGW(TAG_PBT, "尚未完成寻零, 无法归零");
            return;
        }
        if (is_moving_) {
            ESP_LOGW(TAG_PBT, "正在运动中, 忽略goHome");
            return;
        }
        if (current_slot_ == 0) {
            ESP_LOGI(TAG_PBT, "已在槽0, 无需归零");
            return;
        }
        ESP_LOGI(TAG_PBT, "归零: %d → 0, 寻找 SW0...", current_slot_);
        esp_timer_stop(poll_timer_);
        resetDebounce(sw0_db_, sw0_pin_, STARTUP_COOLDOWN);
        target_slot_ = 0;
        sw1_counter_ = -1;
        is_moving_ = true;
        movement_start_time_ = esp_timer_get_time();
        esp_timer_start_periodic(poll_timer_, POLL_INTERVAL_US);
        startMotor(HOMING_DEV);
    }

    int  getCurrentSlot() const { return current_slot_; }
    bool isReady()        const { return ready_; }

private:
    MedicineDispenser* dispenser_;
    gpio_num_t sw0_pin_, sw1_pin_;

    int current_slot_ = -1;
    int target_slot_ = -1;
    int sw1_counter_ = 0;
    bool is_homing_ = false;
    bool is_moving_ = false;
    bool ready_ = false;

    esp_timer_handle_t poll_timer_ = nullptr;
    int64_t movement_start_time_ = 0;

    struct DebounceState {
        bool last_raw = false;   // false=释放(HIGH), true=按下(LOW)
        bool stable = false;
        int  stable_count = 0;
        int  cooldown = 0;
    };
    DebounceState sw0_db_, sw1_db_;

    static constexpr int DEBOUNCE_SAMPLES = 4;      // 4 × 5ms = 20ms
    static constexpr int COOLDOWN_SAMPLES = 30;     // 30 × 5ms = 150ms
    static constexpr int STARTUP_COOLDOWN = 60;     // 60 × 5ms = 300ms 舵机启动 EMI 抑制
    static constexpr int POLL_INTERVAL_US = 5000;   // 5ms
    static constexpr int HOMING_DEV = 300;
    static constexpr int NORMAL_DEV = 300;
    static constexpr int64_t TIMEOUT_US = 30000000; // 30s

    void startMotor(int dev) {
        dispenser_->Rotate(dev);
    }

    void stopMotor() {
        dispenser_->Stop();
    }

    void resetDebounce(DebounceState& db, gpio_num_t pin, int initial_cooldown = 0) {
        bool raw = (gpio_get_level(pin) == 0);
        db.last_raw = raw;
        db.stable = raw;
        db.stable_count = DEBOUNCE_SAMPLES;
        db.cooldown = initial_cooldown;
    }

    void performHoming() {
        esp_timer_stop(poll_timer_);
        resetDebounce(sw0_db_, sw0_pin_, STARTUP_COOLDOWN);
        resetDebounce(sw1_db_, sw1_pin_);
        is_homing_ = true;
        is_moving_ = true;
        target_slot_ = 0;
        sw1_counter_ = 0;
        movement_start_time_ = esp_timer_get_time();
        esp_timer_start_periodic(poll_timer_, POLL_INTERVAL_US);
        startMotor(HOMING_DEV);
    }

    // 返回 true 表示检测到一次有效按下 (HIGH→LOW 边沿)
    bool debounceSwitch(bool raw, DebounceState& db) {
        // 消抖状态机 (冷却期内也追踪信号, 但抑制触发)
        if (raw != db.last_raw) {
            db.last_raw = raw;
            db.stable_count = 1;
        } else {
            if (db.stable_count < DEBOUNCE_SAMPLES) {
                db.stable_count++;
            }
            if (db.stable_count >= DEBOUNCE_SAMPLES && raw != db.stable) {
                db.stable = raw;
                if (raw && db.cooldown == 0) {
                    // LOW(按下) 边沿, 且不在冷却期 → 有效按下
                    db.cooldown = COOLDOWN_SAMPLES;
                    return true;
                }
            }
        }
        if (db.cooldown > 0) db.cooldown--;
        return false;
    }

    static void PollCallback(void* arg) {
        auto* self = (PillBoxTurntable*)arg;

        if (!self->is_moving_ && !self->is_homing_) {
            // 空闲状态, 但定时器仍在运行 (只在寻零关闭期间需要)
            return;
        }

        // 超时检测
        int64_t elapsed = esp_timer_get_time() - self->movement_start_time_;
        if (elapsed > TIMEOUT_US) {
            ESP_LOGE(TAG_PBT, "%s超时 (%.1fs), 强制停止",
                     self->is_homing_ ? "寻零" : "转槽",
                     elapsed / 1000000.0f);
            self->stopMotor();
            self->is_moving_ = false;
            self->is_homing_ = false;
            esp_timer_stop(self->poll_timer_);
            return;
        }

        bool sw0_raw = (gpio_get_level(self->sw0_pin_) == 0);
        bool sw1_raw = (gpio_get_level(self->sw1_pin_) == 0);

        bool sw0_trig = self->debounceSwitch(sw0_raw, self->sw0_db_);
        bool sw1_trig = self->debounceSwitch(sw1_raw, self->sw1_db_);

        if (self->is_homing_) {
            // 寻零: 等待 SW0
            if (sw0_trig) {
                self->stopMotor();
                self->current_slot_ = 0;
                self->sw1_counter_ = 0;
                self->is_homing_ = false;
                self->is_moving_ = false;
                self->ready_ = true;
                esp_timer_stop(self->poll_timer_);
                ESP_LOGI(TAG_PBT, "寻零完成, 当前位置: 槽0");
            }
            return;
        }

        if (!self->is_moving_) return;

        // 转槽或归零中

        if (self->target_slot_ == 0) {
            // goHome: 等待 SW0
            if (sw0_trig) {
                self->stopMotor();
                self->current_slot_ = 0;
                self->sw1_counter_ = 0;
                self->is_moving_ = false;
                esp_timer_stop(self->poll_timer_);
                ESP_LOGI(TAG_PBT, "归零完成, 当前位置: 槽0");
            }
            return;
        }

        // goToSlot(N): 仅计数 SW1, 不因经过槽0而清零
        if (sw1_trig) {
            self->sw1_counter_++;
            ESP_LOGI(TAG_PBT, "SW1触发, 计数器=%d (目标=%d)",
                     self->sw1_counter_, self->target_slot_);
            if (self->sw1_counter_ == self->target_slot_) {
                self->stopMotor();
                self->current_slot_ = self->target_slot_;
                self->is_moving_ = false;
                esp_timer_stop(self->poll_timer_);
                ESP_LOGI(TAG_PBT, "到达槽%d", self->current_slot_);
            }
        }
    }
};

#endif
