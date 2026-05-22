#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"

#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "conversation_logger.h"
#include "wxpusher_notifier.h"
#include "servo_controller.h"
#include "servo_debug_server.h"
#include "scheduler_server.h"
#include "medicine_config_server.h"
#include "pillbox_turntable.h"
#include "medicine_box_display.h"
#include "max30102.h"
#include "assets/lang_config.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <wifi_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

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

#define TAG "MedicineBoxPro"

struct PushTaskParams {
    std::string summary;
    std::string markdown;
    WxPusherNotifier* notifier;
};

static void PushConversationTask(void* arg) {
    auto* params = (PushTaskParams*)arg;
    params->notifier->SendMarkdown(params->summary.c_str(), params->markdown.c_str());
    delete params;
    vTaskDelete(NULL);
}

class MedicineBoxProBoard : public WifiBoard {
private:
    Button boot_button_;
    Button med_dismiss_button_;
    MedicineBoxDisplay* display_;
    WxPusherNotifier notifier_;
    MedicineDispenser* dispenser_ = nullptr;
    PillBoxTurntable* turntable_ = nullptr;
    ServoDebugServer* servo_debug_server_ = nullptr;
    SchedulerServer* scheduler_server_ = nullptr;
    MedicineConfigServer* medicine_config_server_ = nullptr;
    esp_timer_handle_t http_retry_timer_ = nullptr;
    esp_timer_handle_t med_check_timer_ = nullptr;
    esp_timer_handle_t ip_poll_timer_ = nullptr;
    esp_timer_handle_t display_refresh_timer_ = nullptr;
    esp_timer_handle_t voice_loop_timer_ = nullptr;
    int http_retry_count_ = 0;
    std::string last_push_status_;
    std::mutex status_mutex_;

    // ---- MAX30102 健康检测 ----
    MAX30102* max30102_ = nullptr;
    TaskHandle_t health_measure_task_ = nullptr;
    esp_timer_handle_t health_ui_timer_ = nullptr;
    esp_timer_handle_t health_dismiss_timer_ = nullptr;
    bool health_measuring_ = false;
    int health_elapsed_sec_ = 0;
    bool reminder_active_ = false;

    static constexpr int PPG_RING_SIZE = 600;
    struct PpgRingEntry {
        uint32_t red;
        uint32_t ir;
        bool valid;
    };
    PpgRingEntry ppg_ring_[PPG_RING_SIZE];
    int ppg_ring_head_ = 0;
    int ppg_ring_count_ = 0;
    std::vector<uint32_t> all_red_;
    std::vector<uint32_t> all_ir_;

    static void HttpServerRetryCallback(void* arg) {
        auto* self = (MedicineBoxProBoard*)arg;
        httpd_handle_t http_server = nullptr;
        httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
        http_cfg.max_uri_handlers = 20;
        http_cfg.uri_match_fn = httpd_uri_match_wildcard;

        if (httpd_start(&http_server, &http_cfg) == ESP_OK) {
            esp_timer_stop(self->http_retry_timer_);
            ESP_LOGI(TAG, "HTTP 服务器已启动 (重试 %d 次)", self->http_retry_count_);

            self->servo_debug_server_->RegisterHandlers(http_server);
            self->scheduler_server_->RegisterHttpHandlers(http_server);
            self->medicine_config_server_->RegisterHttpHandlers(http_server);

            // 在 LCD 屏幕上显示访问网址
            auto& wifi = WifiManager::GetInstance();
            std::string ip = wifi.GetIpAddress();
            if (!ip.empty() && ip != "0.0.0.0") {
                std::string msg = "智能药盒: http://" + ip + "/medicine-config\n"
                                  "定时调度: http://" + ip + "/scheduler";
                self->display_->ShowNotification(msg, 15000);
                ESP_LOGI(TAG, "智能药盒配置: http://%s/medicine-config", ip.c_str());
                ESP_LOGI(TAG, "定时调度器页面: http://%s/scheduler", ip.c_str());
            } else {
                // DHCP 尚未分配 IP，启动轮询等待
                esp_timer_create_args_t poll_args = {};
                poll_args.callback = IpPollCallback;
                poll_args.arg = self;
                poll_args.dispatch_method = ESP_TIMER_TASK;
                poll_args.name = "ip_poll";
                esp_timer_create(&poll_args, &self->ip_poll_timer_);
                esp_timer_start_periodic(self->ip_poll_timer_, 2000000);
                ESP_LOGI(TAG, "HTTP 服务器已就绪，等待 DHCP 分配 IP...");
            }
        } else {
            self->http_retry_count_++;
            if (self->http_retry_count_ >= 30) {
                esp_timer_stop(self->http_retry_timer_);
                ESP_LOGE(TAG, "HTTP 服务器启动失败 (已重试 %d 次)", self->http_retry_count_);
            }
        }
    }

    static void IpPollCallback(void* arg) {
        auto* self = (MedicineBoxProBoard*)arg;
        auto& wifi = WifiManager::GetInstance();
        std::string ip = wifi.GetIpAddress();
        if (!ip.empty() && ip != "0.0.0.0") {
            esp_timer_stop(self->ip_poll_timer_);
            std::string msg = "智能药盒: http://" + ip + "/medicine-config\n"
                              "定时调度: http://" + ip + "/scheduler";
            self->display_->ShowNotification(msg, 15000);
            ESP_LOGI(TAG, "智能药盒配置: http://%s/medicine-config", ip.c_str());
            ESP_LOGI(TAG, "定时调度器页面: http://%s/scheduler", ip.c_str());
        }
    }

    static void MedCheckCallback(void* arg) {
        auto* self = (MedicineBoxProBoard*)arg;
        if (self->medicine_config_server_) {
            auto alert_msg = self->medicine_config_server_->CheckAndNotify();
            if (!alert_msg.empty() && self->display_) {
                // 屏幕同步显示用药提醒
                std::string full = "该吃药了! " + alert_msg;
                self->display_->ShowNotification(full, 10000);
            }
        }
    }

    static void VoiceLoopCallback(void* arg) {
        // 不能直接 PlaySound: PushPacketToDecodeQueue 会阻塞 ESP_TIMER_TASK
        // 导致同任务的按钮轮询定时器停止响应，GPIO10 按钮失效
        auto& app = Application::GetInstance();
        app.Schedule([]() {
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_MED_REMINDER);
        });
    }

    static void DisplayRefreshCallback(void* arg) {
        auto* self = (MedicineBoxProBoard*)arg;
        if (!self->display_) return;

        auto& wifi = WifiManager::GetInstance();
        std::string ip = wifi.GetIpAddress();
        if (ip.empty() || ip == "0.0.0.0") ip = "无网络";
        self->display_->SetNetworkInfo(ip);

        std::string status;
        {
            std::lock_guard<std::mutex> lock(self->status_mutex_);
            status = self->last_push_status_;
        }
        self->display_->SetPushStatus(status.empty() ? "--" : status);

        if (self->medicine_config_server_) {
            self->display_->SetMedPlan(
                self->medicine_config_server_->GetPlanSummary());
        }
    }

    // ======== MAX30102 健康检测方法 ========

    void StartHealthMeasurement() {
        if (health_measuring_ || !max30102_) return;
        ESP_LOGI(TAG, "启动健康检测...");

        if (!max30102_->StartSampling()) {
            ESP_LOGE(TAG, "MAX30102 启动采样失败");
            return;
        }

        health_measuring_ = true;
        health_elapsed_sec_ = 0;
        ppg_ring_head_ = 0;
        ppg_ring_count_ = 0;
        all_red_.clear();
        all_ir_.clear();
        all_red_.reserve(6000);
        all_ir_.reserve(6000);

        if (display_) {
            if (!display_->ShowHealthOverlay()) {
                ESP_LOGE(TAG, "健康检测 UI 创建失败");
                health_measuring_ = false;
                max30102_->StopSampling();
                return;
            }
            display_->ResetPpgBaseline();
        }

        xTaskCreatePinnedToCore(
            HealthMeasureTask, "health_meas", 4096,
            this, 5, &health_measure_task_, 0);

        esp_timer_create_args_t ui_args = {};
        ui_args.callback = HealthUITimerCallback;
        ui_args.arg = this;
        ui_args.dispatch_method = ESP_TIMER_TASK;
        ui_args.name = "health_ui";
        esp_timer_create(&ui_args, &health_ui_timer_);
        esp_timer_start_periodic(health_ui_timer_, 16000);  // 16ms ≈ 60Hz
    }

    static void HealthMeasureTask(void* arg) {
        auto* self = (MedicineBoxProBoard*)arg;
        const int duration_sec = 60;
        const int64_t start_ms = esp_timer_get_time() / 1000;

        ESP_LOGI(TAG, "健康检测任务启动 (%ds)", duration_sec);

        while (true) {
            int64_t elapsed = (esp_timer_get_time() / 1000) - start_ms;
            if (elapsed >= duration_sec * 1000) break;

            self->health_elapsed_sec_ = (int)(elapsed / 1000);

            // 排空 FIFO: 每次循环读完所有可用样本, 防止 FIFO 积压
            int drained = 0;
            MAX30102::PpgSample sample;
            while (self->max30102_->ReadSample(sample) && drained < 32) {
                auto& entry = self->ppg_ring_[self->ppg_ring_head_];
                entry.red = sample.red;
                entry.ir = sample.ir;
                entry.valid = true;
                self->ppg_ring_head_ = (self->ppg_ring_head_ + 1) % PPG_RING_SIZE;
                if (self->ppg_ring_count_ < PPG_RING_SIZE) self->ppg_ring_count_++;

                self->all_red_.push_back(sample.red);
                self->all_ir_.push_back(sample.ir);
                drained++;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        self->max30102_->StopSampling();
        ESP_LOGI(TAG, "健康检测采样结束, 共 %d 个样本", (int)self->all_red_.size());

        // 计算最终结果
        HealthResult result;
        if (self->all_red_.size() >= 50) {
            result = self->max30102_->ComputeResults(
                self->all_red_.data(), self->all_ir_.data(),
                (int)self->all_red_.size());
        }

        self->FinalizeHealthMeasurement(result);
        self->health_measure_task_ = nullptr;
        vTaskDelete(NULL);
    }

    static void HealthUITimerCallback(void* arg) {
        auto* self = (MedicineBoxProBoard*)arg;
        if (!self->health_measuring_ || !self->display_) return;

        DisplayLockGuard lock(self->display_);

        // 1. 画波形 (双通道: 左画布 IR 绿色心率, 右画布 RED 红色血氧)
        int samples_to_read = (self->ppg_ring_count_ < 2) ? self->ppg_ring_count_ : 2;
        if (samples_to_read > 0) {
            uint32_t waveform_ir[5], waveform_red[5];
            int read_pos = (self->ppg_ring_head_ - samples_to_read + PPG_RING_SIZE) % PPG_RING_SIZE;
            for (int i = 0; i < samples_to_read; i++) {
                waveform_ir[i]  = self->ppg_ring_[read_pos].ir;
                waveform_red[i] = self->ppg_ring_[read_pos].red;
                read_pos = (read_pos + 1) % PPG_RING_SIZE;
            }
            self->display_->UpdatePpgWaveform(waveform_ir, waveform_red, samples_to_read);
        }

        // 2. 每 2 秒更新滚动估值
        static int64_t last_est_ms = 0;
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_est_ms > 2000) {
            last_est_ms = now_ms;
            self->max30102_->UpdateRollingEstimate();
            int hr = self->max30102_->GetRollingHR();
            int spo2 = self->max30102_->GetRollingSpO2();
            self->display_->UpdateHealthReadings(hr, spo2);
        }

        // 3. 倒计时
        char status[32];
        int remaining = 60 - self->health_elapsed_sec_;
        if (remaining < 0) remaining = 0;
        snprintf(status, sizeof(status), "检测中... %d秒", remaining);
        self->display_->UpdateHealthStatus(status);
    }

    void FinalizeHealthMeasurement(const HealthResult& result) {
        health_measuring_ = false;

        if (health_ui_timer_) {
            esp_timer_stop(health_ui_timer_);
            esp_timer_delete(health_ui_timer_);
            health_ui_timer_ = nullptr;
        }

        if (display_) {
            if (result.finger_detected && result.heart_rate > 0 && result.spo2 > 0) {
                display_->UpdateHealthReadings(result.heart_rate, result.spo2);
                display_->UpdateHealthStatus("检测完成");

                // 语音播报
                auto& app = Application::GetInstance();
                char msg[64];
                snprintf(msg, sizeof(msg), "最近一次测得血氧浓度为%d%%，心率为%d次/分",
                         result.spo2, result.heart_rate);
                app.Schedule([msg_str = std::string(msg)]() {
                    Application::GetInstance().Alert("健康检测", msg_str.c_str(), "");
                });
            } else if (!result.finger_detected) {
                display_->UpdateHealthStatus("未检测到手指，请重试");
            } else {
                display_->UpdateHealthStatus("信号质量不足，请重试");
            }
        }

        // 8 秒后自动消失
        esp_timer_create_args_t dis_args = {};
        dis_args.callback = [](void* arg) {
            auto* self = (MedicineBoxProBoard*)arg;
            if (self->display_) {
                self->display_->HideHealthOverlay();
            }
        };
        dis_args.arg = this;
        dis_args.dispatch_method = ESP_TIMER_TASK;
        dis_args.name = "health_dis";
        esp_timer_create(&dis_args, &health_dismiss_timer_);
        esp_timer_start_once(health_dismiss_timer_, HEALTH_RESULT_DISPLAY_MS * 1000);
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
#ifdef LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new MedicineBoxDisplay(panel_io, panel,
                                          DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                          DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                          DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                          DISPLAY_SWAP_XY);
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

        // GPIO10 静音按钮：停止用药提醒语音播报并清除屏幕提醒
        med_dismiss_button_.OnClick([this]() {
            // 停止语音循环定时器
            if (voice_loop_timer_) {
                esp_timer_stop(voice_loop_timer_);
            }
            auto& app = Application::GetInstance();
            // 停止当前正在播放的 OGG 语音
            app.GetAudioService().ResetDecoder();
            // 清除屏幕 Alert 状态
            app.DismissAlert();
            // 清除 Display 通知文字
            if (display_) {
                display_->DismissNotification();
            }
            // 舵机归零：仅在用药提醒激活时归零，避免双击检测时误触
            if (turntable_ && turntable_->isReady() && reminder_active_) {
                turntable_->goHome();
                reminder_active_ = false;
            }
            ESP_LOGI(TAG, "用药提醒已确认 (GPIO10 按钮)");
        });

        // GPIO10 双击：启动心率血氧健康检测
        med_dismiss_button_.OnDoubleClick([this]() {
            if (health_measuring_) {
                ESP_LOGW(TAG, "健康检测已在进行中");
                return;
            }
            StartHealthMeasurement();
        });
    }

public:
    MedicineBoxProBoard()
        : boot_button_(BOOT_BUTTON_GPIO),
          med_dismiss_button_(MED_DISMISS_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();

        // 360° 舵机调试模式 (无开机校准)
        dispenser_ = new MedicineDispenser(SERVO_PWM_PIN, GPIO_NUM_NC);
        ESP_LOGI(TAG, "舵机调试模式就绪 (PWM: GPIO12)");

        // 药盘转盘控制 (双微动开关定位, 上电后自动寻零)
        turntable_ = new PillBoxTurntable(dispenser_,
                                          TURNTABLE_SW0_PIN,
                                          TURNTABLE_SW1_PIN);
        turntable_->init();  // 启动寻零 (非阻塞, 后台完成)

        // 初始化 MAX30102 心率血氧传感器 (轻量预检 PART_ID)
        max30102_ = new MAX30102();
        if (!max30102_->Initialize()) {
            ESP_LOGW(TAG, "MAX30102 未检测到, 健康检测功能禁用");
            delete max30102_;
            max30102_ = nullptr;
        }

        // 舵机调试服务器对象 (稍后网络就绪时注册路由)
        servo_debug_server_ = new ServoDebugServer(dispenser_);

        // 定时调度器 (核心逻辑独立于 HTTP，稍后网络就绪时注册路由)
        scheduler_server_ = new SchedulerServer(SCHEDULER_LED_GPIO);

        // 设置事件触发回调：定时任务执行时播报
        scheduler_server_->SetEventCallback([](const char* msg) {
            auto& app = Application::GetInstance();
            std::string text(msg);
            app.Schedule([text]() {
                Application::GetInstance().Alert("定时任务", text.c_str(), "");
            });
        });

        // 智能药盒用药配置 (稍后网络就绪时注册路由)
        medicine_config_server_ = new MedicineConfigServer();

        // 用药提醒回调：到点时屏幕通知 + 大字剂量 + OGG 语音循环播报
        medicine_config_server_->SetEventCallback([this](int slot, int dose, const char* msg) {
            reminder_active_ = true;
            // 舵机转至目标药槽
            if (turntable_ && turntable_->isReady()) {
                turntable_->goToSlot(slot);
                ESP_LOGI(TAG, "用药提醒: 舵机转至槽%d", slot);
            }
            // 屏幕左侧大字显示服药数量
            if (display_) {
                display_->ShowDoseOverlay(slot, dose);
            }
            auto& app = Application::GetInstance();
            std::string text(msg);
            app.Schedule([text]() {
                Application::GetInstance().Alert("用药提醒", text.c_str(), "");
            });
            // 循环播放 OGG 语音 (~5.3秒)，直到按下 GPIO10 按钮停止
            if (!voice_loop_timer_) {
                esp_timer_create_args_t voice_args = {};
                voice_args.callback = VoiceLoopCallback;
                voice_args.arg = nullptr;
                voice_args.dispatch_method = ESP_TIMER_TASK;
                voice_args.name = "voice_loop";
                esp_timer_create(&voice_args, &voice_loop_timer_);
            }
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_MED_REMINDER);
            esp_timer_start_periodic(voice_loop_timer_, 5500000); // 5.5秒循环
        });

        // 用药提醒检查定时器 (每 30 秒)
        {
            esp_timer_create_args_t med_args = {};
            med_args.callback = MedCheckCallback;
            med_args.arg = this;
            med_args.dispatch_method = ESP_TIMER_TASK;
            med_args.name = "med_check";
            esp_timer_create(&med_args, &med_check_timer_);
            esp_timer_start_periodic(med_check_timer_, 30000000); // 30秒
        }

        // === MCP 工具注册 ===
        // WxPusher 微信推送
        McpServer::GetInstance().AddTool(
            "push_conversation",
            "将当前对话记录推送到家人微信",
            PropertyList(),
            [this](const PropertyList& props) -> ReturnValue {
                auto& logger = ConversationLogger::GetInstance();
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    last_push_status_ = "发送中...";
                }
                auto* params = new PushTaskParams{
                    logger.ToSummary(),
                    logger.ToMarkdown(),
                    &notifier_,
                };
                logger.Clear();
                xTaskCreate(PushConversationTask, "wxpush", 8192,
                            params, 5, NULL);
                return std::string("对话记录已推送到微信");
            });

        // 语音添加定时任务
        McpServer::GetInstance().AddTool(
            "add_schedule",
            "添加定时任务，在指定日期和时间自动控制LED开关。当用户通过语音说\"设置定时\"、\"定时开灯\"、\"几点关灯\"等时调用此工具。"
            "如果用户要求每天重复执行(如\"每天\"、\"每天早上\"、\"每天都\")，请设置repeat_type=1。"
            "如果用户要求每天重复但指定了截止日期(如\"每天执行到5月20日\"、\"每天直到月底\")，请设置repeat_type=2并填写end_month和end_day。"
            "默认repeat_type=0(单次执行)。"
            "如果用户指定了具体日期(如\"5月11日\"、\"每月15日\"、\"3月每天\")，请设置month(1-12,0=每月)和day(1-31,0=每天)参数。",
            PropertyList({
                Property("hour", kPropertyTypeInteger, 0, 23),
                Property("minute", kPropertyTypeInteger, 0, 59),
                Property("action", kPropertyTypeString),
                Property("repeat_type", kPropertyTypeInteger, 0),
                Property("month", kPropertyTypeInteger, 0),
                Property("day", kPropertyTypeInteger, 0),
                Property("end_month", kPropertyTypeInteger, 0),
                Property("end_day", kPropertyTypeInteger, 0),
            }),
            [this](const PropertyList& props) -> ReturnValue {
                int hour = props["hour"].value<int>();
                int minute = props["minute"].value<int>();
                std::string action_str = props["action"].value<std::string>();
                int repeat_type = props["repeat_type"].value<int>();
                int month = props["month"].value<int>();
                int day = props["day"].value<int>();
                int end_month = props["end_month"].value<int>();
                int end_day = props["end_day"].value<int>();

                int action_val;
                if (action_str == "led_on") {
                    action_val = 1;
                } else if (action_str == "led_off") {
                    action_val = 0;
                } else {
                    return std::string("action 参数无效，必须为 led_on 或 led_off");
                }

                std::string error;
                bool ok = scheduler_server_->AddEvent(hour, minute, action_val, repeat_type,
                                                      month, day, end_month, end_day, &error);
                if (ok) {
                    const char* rpt_info = repeat_type == 1 ? "(每天)" :
                                           repeat_type == 2 ? "(每天,有截止)" : "";
                    char date_info[32] = "";
                    if (month > 0 && day > 0)
                        snprintf(date_info, sizeof(date_info), "%d月%d日 ", month, day);
                    else if (month > 0)
                        snprintf(date_info, sizeof(date_info), "%d月每天 ", month);
                    else if (day > 0)
                        snprintf(date_info, sizeof(date_info), "每月%d日 ", day);
                    char buf[96];
                    snprintf(buf, sizeof(buf), "已添加定时任务: %s%02d:%02d %s%s",
                             date_info, hour, minute, action_val ? "LED亮" : "LED灭", rpt_info);
                    return std::string(buf);
                }
                return std::string("添加失败: " + error);
            });

        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }

        // 屏幕信息刷新定时器 (每 3 秒更新侧边栏)
        {
            esp_timer_create_args_t refresh_args = {};
            refresh_args.callback = DisplayRefreshCallback;
            refresh_args.arg = this;
            refresh_args.dispatch_method = ESP_TIMER_TASK;
            refresh_args.name = "display_refresh";
            esp_timer_create(&refresh_args, &display_refresh_timer_);
            esp_timer_start_periodic(display_refresh_timer_, 5000000);
        }

        // HTTP 服务器延迟启动：网络协议栈在 StartNetwork() 中初始化，
        // 而构造函数在 StartNetwork() 之前执行。用定时器重试启动。
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = HttpServerRetryCallback;
        timer_args.arg = this;
        timer_args.dispatch_method = ESP_TIMER_TASK;
        timer_args.name = "http_retry";
        esp_timer_create(&timer_args, &http_retry_timer_);
        esp_timer_start_periodic(http_retry_timer_, 2000000); // 每 2 秒重试
        ESP_LOGI(TAG, "HTTP 服务器等待网络就绪...");
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
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

DECLARE_BOARD(MedicineBoxProBoard);
