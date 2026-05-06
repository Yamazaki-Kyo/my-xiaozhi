#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"

#include "max30102.h"
#include "servo_controller.h"
#include "medicine_reminder.h"

#include <esp_log.h>
#include <driver/spi_common.h>
#include <driver/uart.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <esp_http_server.h>

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

#define TAG "MedicineBoxBoard"

class MedicineBoxBoard : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
    MAX30102 max30102_;
    ServoController servo_;
    MedicineReminder reminder_;
    esp_timer_handle_t reminder_timer_;
    TaskHandle_t uart_task_handle_;
    httpd_handle_t http_server_;

    // 健康检测状态
    bool health_detection_active_;
    HealthResult last_health_result_;
    int finger_retry_count_;

    // 初始化SPI
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

    // 初始化LCD
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
        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                     DISPLAY_SWAP_XY);
    }

    // 初始化按键
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

    // 初始化UART (视觉识别模组)
    void InitializeUart() {
        uart_config_t uart_config = {};
        uart_config.baud_rate = VISION_UART_BAUD_RATE;
        uart_config.data_bits = UART_DATA_8_BITS;
        uart_config.parity = UART_PARITY_DISABLE;
        uart_config.stop_bits = UART_STOP_BITS_1;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config.source_clk = UART_SCLK_DEFAULT;

        ESP_ERROR_CHECK(uart_driver_install(VISION_UART_PORT_NUM,
                                            VISION_UART_BUF_SIZE * 2,
                                            0, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(VISION_UART_PORT_NUM, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(VISION_UART_PORT_NUM,
                                     VISION_UART_TXD, VISION_UART_RXD,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

        ESP_LOGI(TAG, "UART 视觉模组初始化完成 (端口=%d, RX=GPIO%d, TX=GPIO%d)",
                 VISION_UART_PORT_NUM, VISION_UART_RXD, VISION_UART_TXD);
    }

    // UART 接收任务 (后台运行)
    static void UartTask(void* arg) {
        MedicineBoxBoard* board = static_cast<MedicineBoxBoard*>(arg);
        uint8_t buf[VISION_UART_BUF_SIZE];

        while (true) {
            int len = uart_read_bytes(VISION_UART_PORT_NUM, buf,
                                      VISION_UART_BUF_SIZE - 1,
                                      pdMS_TO_TICKS(500));
            if (len > 0) {
                buf[len] = 0;
                ESP_LOGI(TAG, "视觉模组数据: %s", buf);

                // 检测到有效的视觉识别反馈后唤醒小智
                if (strstr((char*)buf, "DETECT") != nullptr ||
                    strstr((char*)buf, "FACE") != nullptr ||
                    strstr((char*)buf, "PERSON") != nullptr) {

                    auto& app = Application::GetInstance();
                    app.Schedule([&app]() {
                        if (app.GetDeviceState() == kDeviceStateIdle) {
                            app.ToggleChatState();
                        }
                    });
                    ESP_LOGI(TAG, "视觉识别触发唤醒");
                }
            }
        }
    }

    // 用药提醒定时器回调
    static void ReminderTimerCallback(void* arg) {
        MedicineBoxBoard* board = static_cast<MedicineBoxBoard*>(arg);
        auto reminders = board->reminder_.CheckAndTriggerReminders();

        for (auto& entry : reminders) {
            ESP_LOGI(TAG, "用药提醒触发: 槽%d %s %d颗 %02d:%02d",
                     entry.slot, entry.drug_name.c_str(),
                     entry.dose_count, entry.hour, entry.minute);

            auto& app = Application::GetInstance();
            app.Schedule([&app, entry]() {
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    // 唤醒小智进行语音播报提醒
                    app.ToggleChatState();
                }
            });
        }
    }

    // 健康检测流程
    void StartHealthDetection() {
        if (health_detection_active_) {
            ESP_LOGW(TAG, "健康检测已在运行中");
            return;
        }

        health_detection_active_ = true;
        finger_retry_count_ = 0;

        auto& app = Application::GetInstance();
        display_->SetChatMessage("system", "请把手指轻轻放在传感器上，保持不动，即将开始检测。");

        // 手指在位检测 (10秒超时, 最多3次重试)
        while (finger_retry_count_ < 3) {
            bool finger_found = false;

            for (int i = 0; i < 20; i++) {  // 10s = 20 * 500ms
                if (max30102_.IsFingerDetected()) {
                    finger_found = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            if (finger_found) break;

            finger_retry_count_++;
            if (finger_retry_count_ < 3) {
                display_->SetChatMessage("system", "未检测到手指，请重新放置。");
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }

        if (finger_retry_count_ >= 3) {
            display_->SetChatMessage("system", "检测已取消，如需检测请重新唤醒。");
            health_detection_active_ = false;
            return;
        }

        // 数据采集 (15秒)
        display_->SetChatMessage("system", "采集中… 15s\n请保持手指稳定不动");
        last_health_result_ = max30102_.PerformMeasurement(15);

        // 信号质量检查
        if (last_health_result_.signal_quality < 0.3f) {
            display_->SetChatMessage("system", "手指似乎有移动，请保持静止，重新检测。");

            // 延长采集，仅一次重试
            vTaskDelay(pdMS_TO_TICKS(1000));
            last_health_result_ = max30102_.PerformMeasurement(20);
        }

        // 显示结果
        char result_msg[256];
        if (last_health_result_.heart_rate > 0 && last_health_result_.spo2 > 0) {
            snprintf(result_msg, sizeof(result_msg),
                     "检测完成！\n心率：%d bpm\n血氧：%d%%",
                     last_health_result_.heart_rate,
                     last_health_result_.spo2);

            // 异常值提醒
            if (last_health_result_.heart_rate < 50 || last_health_result_.heart_rate > 120 ||
                last_health_result_.spo2 < 95) {
                strcat(result_msg, "\n\n数值偏离常规范围，如有不适请关注身体状况。");
            }
        } else {
            snprintf(result_msg, sizeof(result_msg),
                     "检测未能获得有效结果，请重新检测。");
        }

        display_->SetChatMessage("system", result_msg);
        health_detection_active_ = false;
    }

    // 执行服药出药动作
    void DispenseMedicine(int slot) {
        if (slot < 1 || slot > 7) {
            ESP_LOGE(TAG, "无效的槽号: %d", slot);
            return;
        }

        const SlotConfig* cfg = reminder_.GetSlotConfig(slot);
        if (cfg == nullptr) {
            ESP_LOGW(TAG, "槽%d未配置药品", slot);
            return;
        }

        ESP_LOGI(TAG, "出药: 槽%d %s %d颗", slot, cfg->drug_name.c_str(), cfg->dose_count);

        // 转动舵机到目标药槽
        servo_.MoveToSlot(slot);

        // 等待取药确认 (通过MCP工具或按键)
        // 实际应用中可结合按键/盒盖检测
    }

    // 注册 MCP 工具
    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        // ---- 健康检测 ----
        mcp_server.AddTool(
            "self.health.start_detection",
            "启动心率血氧检测。检测流程：手指在位检测→数据采集→结果播报。检测期间独占总线。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                if (max30102_.IsDeviceOnline()) {
                    auto& app = Application::GetInstance();
                    app.Schedule([this]() {
                        StartHealthDetection();
                    });
                    return true;
                }
                return "健康检测功能暂时不可用。";
            });

        mcp_server.AddTool(
            "self.health.get_result",
            "获取最近一次健康检测结果，返回心率(bpm)、血氧(%)、信号质量",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                HealthResult r = max30102_.GetLastResult();
                if (r.heart_rate < 0 && r.spo2 < 0) {
                    return "暂无检测结果";
                }
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "{\"heart_rate\":%d,\"spo2\":%d,\"signal_quality\":%.2f,\"finger_detected\":%s}",
                         r.heart_rate, r.spo2, (double)r.signal_quality,
                         r.finger_detected ? "true" : "false");
                return std::string(buf);
            });

        mcp_server.AddTool(
            "self.health.get_status",
            "获取 MAX30102 传感器状态",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                bool online = max30102_.IsDeviceOnline();
                char buf[64];
                snprintf(buf, sizeof(buf),
                         "{\"sensor_online\":%s,\"detection_active\":%s}",
                         online ? "true" : "false",
                         health_detection_active_ ? "true" : "false");
                return std::string(buf);
            });

        // ---- 药品计划管理 ----
        mcp_server.AddTool(
            "self.medicine.set_plan",
            "设置用药计划。plan_json: 药品计划JSON字符串，包含总开关(master_enabled)和7个药槽配置(slots数组)。"
            "每个药槽包含: slot(槽号1-7), drug_name(药品名), shape_label(形状标签), dose_count(每次颗数1-9), "
            "daily_frequency(每日次数1-5), times(时间数组: [{hour, minute}, ...])",
            PropertyList({Property("plan_json", kPropertyTypeString, "{}")}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string json = properties["plan_json"].value<std::string>();
                bool ok = reminder_.SetPlanFromJson(json.c_str());
                if (!ok) return std::string("计划设置失败，请检查JSON格式");
                return true;
            });

        mcp_server.AddTool(
            "self.medicine.get_plan",
            "获取当前用药计划JSON",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                return reminder_.GetPlanJson();
            });

        mcp_server.AddTool(
            "self.medicine.enable",
            "启用或禁用用药提醒总开关",
            PropertyList({Property("enabled", kPropertyTypeBoolean, true)}),
            [this](const PropertyList& properties) -> ReturnValue {
                bool enabled = properties["enabled"].value<bool>();
                reminder_.SetMasterEnabled(enabled);
                return true;
            });

        mcp_server.AddTool(
            "self.medicine.get_daily_reminders",
            "获取今日提醒列表",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto reminders = reminder_.GenerateDailyReminders();
                cJSON* arr = cJSON_CreateArray();
                for (auto& r : reminders) {
                    cJSON* obj = cJSON_CreateObject();
                    cJSON_AddNumberToObject(obj, "slot", r.slot);
                    cJSON_AddStringToObject(obj, "drug_name", r.drug_name.c_str());
                    cJSON_AddNumberToObject(obj, "dose_count", r.dose_count);
                    cJSON_AddNumberToObject(obj, "hour", r.hour);
                    cJSON_AddNumberToObject(obj, "minute", r.minute);
                    cJSON_AddBoolToObject(obj, "enabled", r.enabled);
                    cJSON_AddItemToArray(arr, obj);
                }
                char* str = cJSON_PrintUnformatted(arr);
                std::string result(str);
                cJSON_free(str);
                cJSON_Delete(arr);
                return result;
            });

        // ---- 药盒转盘控制 ----
        mcp_server.AddTool(
            "self.carousel.move_to_slot",
            "控制转盘转到指定药槽(1-7)或闭口位置(0)。转动期间不响应其他出药指令。",
            PropertyList({Property("slot", kPropertyTypeInteger, 0, 0, 7)}),
            [this](const PropertyList& properties) -> ReturnValue {
                if (servo_.IsMoving()) {
                    return "转盘正在转动中，请稍后再试。";
                }
                int slot = properties["slot"].value<int>();
                servo_.MoveToSlot(slot);
                return true;
            });

        mcp_server.AddTool(
            "self.carousel.close",
            "关闭药盒 (转盘回到闭口位置)，取药完成后调用。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                servo_.Close();
                return true;
            });

        mcp_server.AddTool(
            "self.carousel.get_status",
            "获取转盘当前状态",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                char buf[64];
                snprintf(buf, sizeof(buf),
                         "{\"current_slot\":%d,\"is_moving\":%s}",
                         servo_.GetCurrentSlot(),
                         servo_.IsMoving() ? "true" : "false");
                return std::string(buf);
            });

        // ---- 手动出药 ----
        mcp_server.AddTool(
            "self.dispense",
            "执行指定药槽的出药动作(转动转盘到位)。slot: 药槽号1-7。",
            PropertyList({Property("slot", kPropertyTypeInteger, 1, 1, 7)}),
            [this](const PropertyList& properties) -> ReturnValue {
                int slot = properties["slot"].value<int>();
                const SlotConfig* cfg = reminder_.GetSlotConfig(slot);
                if (cfg == nullptr) {
                    return "该槽位未配置药品";
                }
                if (servo_.IsMoving()) {
                    return "转盘正在转动中";
                }
                auto& app = Application::GetInstance();
                app.Schedule([this, slot]() {
                    DispenseMedicine(slot);
                });
                return true;
            });

        ESP_LOGI(TAG, "MCP 工具注册完成");
    }

    // ---- HTTP Server (网页配置入口) ----
    static esp_err_t HttpHealthHandler(httpd_req_t* req) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
        return ESP_OK;
    }

    static esp_err_t HttpGetPlanHandler(httpd_req_t* req) {
        auto* board = static_cast<MedicineBoxBoard*>(req->user_ctx);
        std::string json = board->reminder_.GetPlanJson();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json.c_str());
        return ESP_OK;
    }

    static esp_err_t HttpSetPlanHandler(httpd_req_t* req) {
        auto* board = static_cast<MedicineBoxBoard*>(req->user_ctx);
        char buf[4096];
        int total = 0;
        while (total < (int)sizeof(buf) - 1) {
            int ret = httpd_req_recv(req, buf + total, sizeof(buf) - 1 - total);
            if (ret <= 0) break;
            total += ret;
        }
        buf[total] = 0;

        if (total == 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
            return ESP_FAIL;
        }

        if (board->reminder_.SetPlanFromJson(buf)) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"ok\":true}");
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON parse error");
        }
        return ESP_OK;
    }

    static esp_err_t HttpGetDailyHandler(httpd_req_t* req) {
        auto* board = static_cast<MedicineBoxBoard*>(req->user_ctx);
        auto reminders = board->reminder_.GenerateDailyReminders();
        cJSON* arr = cJSON_CreateArray();
        for (auto& r : reminders) {
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "slot", r.slot);
            cJSON_AddStringToObject(obj, "drug_name", r.drug_name.c_str());
            cJSON_AddNumberToObject(obj, "dose_count", r.dose_count);
            cJSON_AddNumberToObject(obj, "hour", r.hour);
            cJSON_AddNumberToObject(obj, "minute", r.minute);
            cJSON_AddBoolToObject(obj, "enabled", r.enabled);
            cJSON_AddItemToArray(arr, obj);
        }
        char* str = cJSON_PrintUnformatted(arr);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, str);
        cJSON_free(str);
        cJSON_Delete(arr);
        return ESP_OK;
    }

    void InitializeHttpServer() {
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.uri_match_fn = httpd_uri_match_wildcard;
        cfg.max_uri_handlers = 8;

        if (httpd_start(&http_server_, &cfg) != ESP_OK) {
            ESP_LOGW(TAG, "HTTP Server 启动失败");
            return;
        }

        httpd_uri_t uri_health = {.uri = "/api/health", .method = HTTP_GET,
                                  .handler = HttpHealthHandler, .user_ctx = this};
        httpd_register_uri_handler(http_server_, &uri_health);

        httpd_uri_t uri_get_plan = {.uri = "/api/medicine/plan", .method = HTTP_GET,
                                    .handler = HttpGetPlanHandler, .user_ctx = this};
        httpd_register_uri_handler(http_server_, &uri_get_plan);

        httpd_uri_t uri_set_plan = {.uri = "/api/medicine/plan", .method = HTTP_POST,
                                    .handler = HttpSetPlanHandler, .user_ctx = this};
        httpd_register_uri_handler(http_server_, &uri_set_plan);

        httpd_uri_t uri_daily = {.uri = "/api/medicine/daily", .method = HTTP_GET,
                                 .handler = HttpGetDailyHandler, .user_ctx = this};
        httpd_register_uri_handler(http_server_, &uri_daily);

        ESP_LOGI(TAG, "HTTP Server 已启动 (端口 %d)", cfg.server_port);
    }

public:
    MedicineBoxBoard()
        : boot_button_(BOOT_BUTTON_GPIO)
        , servo_(SERVO_PWM_GPIO)
        , uart_task_handle_(nullptr)
        , http_server_(nullptr)
        , health_detection_active_(false)
        , finger_retry_count_(0)
    {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeUart();

        // 初始化 MAX30102
        if (max30102_.Initialize()) {
            ESP_LOGI(TAG, "MAX30102 传感器就绪");
        } else {
            ESP_LOGW(TAG, "MAX30102 初始化失败，健康检测功能不可用");
        }

        // 启动 UART 视觉识别任务
        xTaskCreate(UartTask, "vision_uart", 4096, this, 5, &uart_task_handle_);

        // 启动用药提醒定时器 (每30秒检查)
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = ReminderTimerCallback;
        timer_args.arg = this;
        timer_args.name = "med_reminder";
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reminder_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(reminder_timer_, 30 * 1000 * 1000));

        // 注册 MCP 工具
        RegisterMcpTools();

        // 启动 HTTP Server (网页配置入口)
        InitializeHttpServer();

        // 恢复背光
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    ~MedicineBoxBoard() {
        if (http_server_) httpd_stop(http_server_);
        if (uart_task_handle_) vTaskDelete(uart_task_handle_);
        if (reminder_timer_) {
            esp_timer_stop(reminder_timer_);
            esp_timer_delete(reminder_timer_);
        }
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

DECLARE_BOARD(MedicineBoxBoard);
