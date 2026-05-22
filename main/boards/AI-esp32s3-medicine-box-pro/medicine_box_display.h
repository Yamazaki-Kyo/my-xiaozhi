#ifndef MEDICINE_BOX_DISPLAY_H
#define MEDICINE_BOX_DISPLAY_H

#include "display/lcd_display.h"
#include "config.h"
#include <lvgl.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>

#define TAG_MBD "MedBoxDisplay"

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_puhui_basic_30_4);

// 根据屏幕宽度自适应布局
#if DISPLAY_WIDTH >= 200
  // ==== 240x240 大屏布局 ====
  #define LAYOUT_SIDEBAR_W    105
  #define LAYOUT_SIDEBAR_H    140
  #define LAYOUT_SIDEBAR_X    -4
  #define LAYOUT_SIDEBAR_Y    36
  #define LAYOUT_LABEL_W      93
  #define LAYOUT_DOSE_W       110
  #define LAYOUT_DOSE_H       120
  #define LAYOUT_DOSE_X       4
  #define LAYOUT_DOSE_Y       -16
  #define LAYOUT_EMOJI_X      4
  #define LAYOUT_EMOJI_Y      40
  #define LAYOUT_HEALTH_TITLE_Y 10
  #define LAYOUT_HEALTH_TITLE_FONT &font_puhui_basic_30_4
  #define LAYOUT_SUB_W         108
  #define LAYOUT_SUB_H         120
  #define LAYOUT_CANVAS_Y      44
  #define LAYOUT_CANVAS_LEFT_X 4
  #define LAYOUT_CANVAS_RIGHT_X -4
  #define LAYOUT_CANVAS_TITLE_Y 34
  #define LAYOUT_HR_LABEL_Y    168
  #define LAYOUT_SPO2_LABEL_Y  168
  #define LAYOUT_COUNTDOWN_Y   -14
#else
  // ==== 128x160 ST7735 小屏布局 ====
  #define LAYOUT_SIDEBAR_W    74
  #define LAYOUT_SIDEBAR_H    110
  #define LAYOUT_SIDEBAR_X    -2
  #define LAYOUT_SIDEBAR_Y    24
  #define LAYOUT_LABEL_W      64
  #define LAYOUT_DOSE_W       DISPLAY_WIDTH
  #define LAYOUT_DOSE_H       DISPLAY_HEIGHT
  #define LAYOUT_DOSE_X       0
  #define LAYOUT_DOSE_Y       0
  #define LAYOUT_EMOJI_X      2
  #define LAYOUT_EMOJI_Y      26
  #define LAYOUT_HEALTH_TITLE_Y 4
  #define LAYOUT_HEALTH_TITLE_FONT &font_puhui_14_1
  #define LAYOUT_SUB_W         58
  #define LAYOUT_SUB_H         72
  #define LAYOUT_CANVAS_Y      48
  #define LAYOUT_CANVAS_LEFT_X 3
  #define LAYOUT_CANVAS_RIGHT_X -3
  #define LAYOUT_CANVAS_TITLE_Y 18
  #define LAYOUT_HEALTH_STATUS_Y 34
  #define LAYOUT_HR_LABEL_Y    124
  #define LAYOUT_SPO2_LABEL_Y  140
  #define LAYOUT_COUNTDOWN_Y   -8
#endif

class MedicineBoxDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* info_panel_ = nullptr;
    lv_obj_t* network_label_extra_ = nullptr;
    lv_obj_t* push_status_label_ = nullptr;
    lv_obj_t* timer_events_label_ = nullptr;
    lv_obj_t* dose_overlay_ = nullptr;
    lv_obj_t* dose_text_ = nullptr;
    lv_obj_t* dose_slot_label_ = nullptr;
    lv_obj_t* dose_unit_label_ = nullptr;

    // ---- 健康检测 PPG 波形 UI (左右双画布) ----
    lv_obj_t* health_overlay_ = nullptr;
    lv_obj_t* health_canvas_ir_ = nullptr;
    lv_obj_t* health_canvas_red_ = nullptr;
    lv_obj_t* health_title_label_ = nullptr;
    lv_obj_t* health_hr_label_ = nullptr;
    lv_obj_t* health_spo2_label_ = nullptr;
    lv_obj_t* health_status_label_ = nullptr;
    lv_obj_t* health_countdown_label_ = nullptr;

    static constexpr int SUB_W = LAYOUT_SUB_W;
    static constexpr int SUB_H = LAYOUT_SUB_H;

    // 左画布 (IR 心率) 状态
    int16_t ir_wave_col_ = 0;
    float ir_baseline_ = 0;
    int16_t ir_prev_col_ = -1, ir_prev_y_ = 0;

    // 右画布 (RED 血氧) 状态
    int16_t red_wave_col_ = 0;
    float red_baseline_ = 0;
    int16_t red_prev_col_ = -1, red_prev_y_ = 0;

    bool ppg_baseline_init_ = false;

    esp_timer_handle_t scroll_timer_ = nullptr;

    std::string network_displayed_;
    std::string push_displayed_;
    std::string plan_displayed_;

    struct ScrollCtx {
        lv_obj_t* panel;
        int pause;
        int dir;
        int target_y;
    };
    ScrollCtx scroll_ctx_;

    static void PlanScrollCb(void* arg) {
        auto* ctx = (ScrollCtx*)arg;
        auto* panel = ctx->panel;

        if (!lvgl_port_lock(0)) return;

        if (ctx->pause > 0) {
            ctx->pause--;
            if (ctx->pause == 0) ctx->dir = -1;
            lvgl_port_unlock();
            return;
        }
        if (ctx->pause < 0) {
            ctx->pause++;
            if (ctx->pause == 0) ctx->dir = 1;
            lvgl_port_unlock();
            return;
        }

        ctx->target_y += ctx->dir;
        lv_obj_scroll_to_y(panel, ctx->target_y, LV_ANIM_OFF);

        int32_t actual = lv_obj_get_scroll_y(panel);
        if (actual != ctx->target_y) {
            ctx->target_y = actual;
            ctx->pause = (ctx->dir > 0) ? 40 : -30;
            ctx->dir = 0;
        }
        lvgl_port_unlock();
    }

public:
    MedicineBoxDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                       int width, int height, int offset_x, int offset_y,
                       bool mirror_x, bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                        mirror_x, mirror_y, swap_xy) {}

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        DisplayLockGuard lock(this);

        if (emoji_box_) {
            lv_obj_align(emoji_box_, LV_ALIGN_TOP_LEFT, LAYOUT_EMOJI_X, LAYOUT_EMOJI_Y);
        }

        // 小屏缩小 AI 表情字体 (默认 font_awesome_30_4 在 128x160 上过大)
#if DISPLAY_WIDTH < 200
        if (emoji_label_) {
            lv_obj_set_style_text_font(emoji_label_, &font_puhui_14_1, 0);
        }
#endif

        auto screen = lv_screen_active();

        info_panel_ = lv_obj_create(screen);
        lv_obj_set_size(info_panel_, LAYOUT_SIDEBAR_W, LAYOUT_SIDEBAR_H);
        lv_obj_set_style_radius(info_panel_, 6, 0);
        lv_obj_set_style_bg_opa(info_panel_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(info_panel_, lv_color_hex(0x101025), 0);
        lv_obj_set_style_border_width(info_panel_, 0, 0);
        lv_obj_set_style_pad_all(info_panel_, 4, 0);
        lv_obj_set_style_pad_row(info_panel_, 3, 0);
        lv_obj_set_flex_flow(info_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(info_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scrollbar_mode(info_panel_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(info_panel_, LV_ALIGN_TOP_RIGHT, LAYOUT_SIDEBAR_X, LAYOUT_SIDEBAR_Y);

        int label_w = LAYOUT_LABEL_W;

        network_label_extra_ = lv_label_create(info_panel_);
        lv_obj_set_width(network_label_extra_, label_w);
        lv_label_set_long_mode(network_label_extra_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(network_label_extra_, lv_color_hex(0x4ECCA3), 0);
        lv_obj_set_style_text_font(network_label_extra_, &font_puhui_14_1, 0);
        lv_label_set_text(network_label_extra_, "等待网络...");
        network_displayed_ = "等待网络...";

        push_status_label_ = lv_label_create(info_panel_);
        lv_obj_set_width(push_status_label_, label_w);
        lv_label_set_long_mode(push_status_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(push_status_label_, lv_color_hex(0x6CB4EE), 0);
        lv_obj_set_style_text_font(push_status_label_, &font_puhui_14_1, 0);
        lv_label_set_text(push_status_label_, "微信推送\n--");
        push_displayed_ = "微信推送\n--";

        timer_events_label_ = lv_label_create(info_panel_);
        lv_obj_set_width(timer_events_label_, label_w);
        lv_label_set_long_mode(timer_events_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(timer_events_label_, lv_color_hex(0x7DB892), 0);
        lv_obj_set_style_text_font(timer_events_label_, &font_puhui_14_1, 0);
        lv_label_set_text(timer_events_label_, "用药计划\n等待设置...");
        plan_displayed_ = "用药计划\n等待设置...";

        dose_overlay_ = lv_obj_create(screen);
        lv_obj_set_size(dose_overlay_, LAYOUT_DOSE_W, LAYOUT_DOSE_H);
        lv_obj_set_style_radius(dose_overlay_, 8, 0);
        lv_obj_set_style_bg_opa(dose_overlay_, LV_OPA_70, 0);
        lv_obj_set_style_bg_color(dose_overlay_, lv_color_hex(0x101025), 0);
        lv_obj_set_style_border_width(dose_overlay_, 2, 0);
        lv_obj_set_style_border_color(dose_overlay_, lv_color_hex(0x4ECCA3), 0);
        lv_obj_set_style_pad_all(dose_overlay_, 6, 0);
#if DISPLAY_WIDTH >= 200
        lv_obj_align(dose_overlay_, LV_ALIGN_LEFT_MID, LAYOUT_DOSE_X, LAYOUT_DOSE_Y);
#else
        lv_obj_align(dose_overlay_, LV_ALIGN_CENTER, 0, 0);
#endif
        lv_obj_add_flag(dose_overlay_, LV_OBJ_FLAG_HIDDEN);

        dose_slot_label_ = lv_label_create(dose_overlay_);
        lv_obj_set_style_text_color(dose_slot_label_, lv_color_hex(0x6CB4EE), 0);
        lv_obj_set_style_text_font(dose_slot_label_, &font_puhui_14_1, 0);
        lv_obj_set_style_text_align(dose_slot_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(dose_slot_label_, LV_ALIGN_TOP_MID, 0, 4);

        dose_text_ = lv_label_create(dose_overlay_);
        lv_obj_set_style_text_color(dose_text_, lv_color_hex(0xFFFFFF), 0);
#if DISPLAY_WIDTH >= 200
        lv_obj_set_style_text_font(dose_text_, &font_puhui_basic_30_4, 0);
#else
        lv_obj_set_style_text_font(dose_text_, &font_puhui_14_1, 0);
#endif
        lv_obj_set_style_text_align(dose_text_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(dose_text_, LV_ALIGN_CENTER, 0, 0);

        dose_unit_label_ = lv_label_create(dose_overlay_);
        lv_obj_set_style_text_color(dose_unit_label_, lv_color_hex(0x6CB4EE), 0);
        lv_obj_set_style_text_font(dose_unit_label_, &font_puhui_14_1, 0);
        lv_obj_set_style_text_align(dose_unit_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(dose_unit_label_, LV_ALIGN_BOTTOM_MID, 0, -4);

        scroll_ctx_.panel = info_panel_;
        scroll_ctx_.pause = 0;
        scroll_ctx_.dir = 1;
        scroll_ctx_.target_y = 0;
        esp_timer_create_args_t scroll_args = {};
        scroll_args.callback = PlanScrollCb;
        scroll_args.arg = &scroll_ctx_;
        scroll_args.dispatch_method = ESP_TIMER_TASK;
        scroll_args.name = "med_scroll";
        esp_timer_create(&scroll_args, &scroll_timer_);
        esp_timer_start_periodic(scroll_timer_, 50000);
    }

    ~MedicineBoxDisplay() {
        if (scroll_timer_) {
            esp_timer_stop(scroll_timer_);
            esp_timer_delete(scroll_timer_);
            scroll_timer_ = nullptr;
        }
    }

    void SetNetworkInfo(const std::string& ip) {
        std::string text = "IP: " + ip;
        if (text == network_displayed_) return;
        network_displayed_ = text;
        if (network_label_extra_) {
            DisplayLockGuard lock(this);
            lv_label_set_text(network_label_extra_, text.c_str());
        }
    }

    void SetPushStatus(const std::string& status) {
        std::string text = "微信推送\n" + status;
        if (text == push_displayed_) return;
        push_displayed_ = text;
        if (push_status_label_) {
            DisplayLockGuard lock(this);
            lv_label_set_text(push_status_label_, text.c_str());
        }
    }

    void SetMedPlan(const std::string& plan_summary) {
        std::string text = "用药计划\n" + (plan_summary.empty() ? "等待设置..." : plan_summary);
        if (text == plan_displayed_) return;
        plan_displayed_ = text;
        if (timer_events_label_) {
            DisplayLockGuard lock(this);
            lv_label_set_text(timer_events_label_, text.c_str());
            lv_obj_scroll_to_y(info_panel_, 0, LV_ANIM_OFF);
            scroll_ctx_.pause = 0;
            scroll_ctx_.dir = 1;
        }
    }

    void ShowDoseOverlay(int slot, int dose) {
        if (!dose_overlay_ || !dose_text_ || !dose_slot_label_ || !dose_unit_label_) return;
        DisplayLockGuard lock(this);
        char buf[32];
        lv_label_set_text(dose_slot_label_, "请取出");
        snprintf(buf, sizeof(buf), "%d", dose);
        lv_label_set_text(dose_text_, buf);
        snprintf(buf, sizeof(buf), "颗药 · 槽%d", slot);
        lv_label_set_text(dose_unit_label_, buf);
        lv_obj_remove_flag(dose_overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    void HideDoseOverlay() {
        if (!dose_overlay_) return;
        DisplayLockGuard lock(this);
        lv_obj_add_flag(dose_overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    // ======== 健康检测 PPG 波形 UI (左右双画布) ========

    static lv_draw_buf_t* AllocSubCanvasBuf(lv_obj_t* canvas) {
        size_t bytes = SUB_W * SUB_H * sizeof(lv_color_t);
        auto* px = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (!px) px = (uint8_t*)malloc(bytes);
        if (!px) return nullptr;
        memset(px, 0, bytes);

        auto* db = (lv_draw_buf_t*)malloc(sizeof(lv_draw_buf_t));
        if (!db) { free(px); return nullptr; }
        lv_draw_buf_init(db, SUB_W, SUB_H, LV_COLOR_FORMAT_RGB565, 0, px, bytes);
        lv_canvas_set_draw_buf(canvas, db);
        return db;
    }

    static void FreeSubCanvasBuf(lv_obj_t* canvas) {
        if (!canvas) return;
        auto* db = lv_canvas_get_draw_buf(canvas);
        if (db) {
            if (db->data) free((void*)db->data);
            free(db);
        }
    }

    bool ShowHealthOverlay() {
        if (health_overlay_) return false;
        DisplayLockGuard lock(this);
        auto screen = lv_screen_active();

        health_overlay_ = lv_obj_create(screen);
        lv_obj_set_size(health_overlay_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_style_radius(health_overlay_, 0, 0);
        lv_obj_set_style_bg_opa(health_overlay_, LV_OPA_90, 0);
        lv_obj_set_style_bg_color(health_overlay_, lv_color_hex(0x0A0A1E), 0);
        lv_obj_set_style_border_width(health_overlay_, 0, 0);
        lv_obj_set_style_pad_all(health_overlay_, 0, 0);
        lv_obj_align(health_overlay_, LV_ALIGN_CENTER, 0, 0);

        // 标题
        health_title_label_ = lv_label_create(health_overlay_);
        lv_obj_set_style_text_color(health_title_label_, lv_color_hex(0x4ECCA3), 0);
        lv_obj_set_style_text_font(health_title_label_, LAYOUT_HEALTH_TITLE_FONT, 0);
        lv_label_set_text(health_title_label_, "健康检测");
        lv_obj_align(health_title_label_, LV_ALIGN_TOP_MID, 0, LAYOUT_HEALTH_TITLE_Y);

        // ---- 左画布: IR 心率 (绿色) ----
        health_canvas_ir_ = lv_canvas_create(health_overlay_);
        lv_obj_set_size(health_canvas_ir_, SUB_W, SUB_H);
        lv_obj_set_style_bg_opa(health_canvas_ir_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(health_canvas_ir_, lv_color_hex(0x0D0D28), 0);
        lv_obj_set_style_border_width(health_canvas_ir_, 1, 0);
        lv_obj_set_style_border_color(health_canvas_ir_, lv_color_hex(0x00AA44), 0);
        lv_obj_align(health_canvas_ir_, LV_ALIGN_TOP_LEFT, LAYOUT_CANVAS_LEFT_X, LAYOUT_CANVAS_Y);
        if (!AllocSubCanvasBuf(health_canvas_ir_)) { HideHealthOverlay(); return false; }

        // 左画布标题
        auto* ir_title = lv_label_create(health_overlay_);
        lv_obj_set_style_text_color(ir_title, lv_color_hex(0x00FF55), 0);
        lv_obj_set_style_text_font(ir_title, &font_puhui_14_1, 0);
        lv_label_set_text(ir_title, "心率 PPG");
        lv_obj_align(ir_title, LV_ALIGN_TOP_LEFT, LAYOUT_CANVAS_LEFT_X, LAYOUT_CANVAS_TITLE_Y);

        // ---- 右画布: RED 血氧 (红色) ----
        health_canvas_red_ = lv_canvas_create(health_overlay_);
        lv_obj_set_size(health_canvas_red_, SUB_W, SUB_H);
        lv_obj_set_style_bg_opa(health_canvas_red_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(health_canvas_red_, lv_color_hex(0x0D0D28), 0);
        lv_obj_set_style_border_width(health_canvas_red_, 1, 0);
        lv_obj_set_style_border_color(health_canvas_red_, lv_color_hex(0xAA2222), 0);
        lv_obj_align(health_canvas_red_, LV_ALIGN_TOP_RIGHT, LAYOUT_CANVAS_RIGHT_X, LAYOUT_CANVAS_Y);
        if (!AllocSubCanvasBuf(health_canvas_red_)) { HideHealthOverlay(); return false; }

        // 右画布标题
        auto* red_title = lv_label_create(health_overlay_);
        lv_obj_set_style_text_color(red_title, lv_color_hex(0xFF3333), 0);
        lv_obj_set_style_text_font(red_title, &font_puhui_14_1, 0);
        lv_label_set_text(red_title, "血氧 PPG");
        lv_obj_align(red_title, LV_ALIGN_TOP_RIGHT, LAYOUT_CANVAS_RIGHT_X, LAYOUT_CANVAS_TITLE_Y);

        // 重置状态
        ir_wave_col_ = 0;   red_wave_col_ = 0;
        ir_baseline_ = 0;   red_baseline_ = 0;
        ir_prev_col_ = -1;  red_prev_col_ = -1;
        ppg_baseline_init_ = false;

        // 心率数值标签
        health_hr_label_ = lv_label_create(health_overlay_);
        lv_obj_set_style_text_color(health_hr_label_, lv_color_hex(0x00FF55), 0);
        lv_obj_set_style_text_font(health_hr_label_, &font_puhui_14_1, 0);
        lv_label_set_text(health_hr_label_, "心率: -- bpm");
#if DISPLAY_WIDTH >= 200
        lv_obj_align(health_hr_label_, LV_ALIGN_TOP_LEFT, LAYOUT_CANVAS_LEFT_X, LAYOUT_HR_LABEL_Y);
#else
        lv_obj_align(health_hr_label_, LV_ALIGN_TOP_MID, 0, LAYOUT_HR_LABEL_Y);
#endif

        // 血氧数值标签
        health_spo2_label_ = lv_label_create(health_overlay_);
        lv_obj_set_style_text_color(health_spo2_label_, lv_color_hex(0xFF3333), 0);
        lv_obj_set_style_text_font(health_spo2_label_, &font_puhui_14_1, 0);
        lv_label_set_text(health_spo2_label_, "血氧: --%");
#if DISPLAY_WIDTH >= 200
        lv_obj_align(health_spo2_label_, LV_ALIGN_TOP_RIGHT, LAYOUT_CANVAS_RIGHT_X, LAYOUT_SPO2_LABEL_Y);
#else
        lv_obj_align(health_spo2_label_, LV_ALIGN_TOP_MID, 0, LAYOUT_SPO2_LABEL_Y);
#endif

        // 状态 / 倒计时
        health_countdown_label_ = lv_label_create(health_overlay_);
        lv_obj_set_style_text_color(health_countdown_label_, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(health_countdown_label_, &font_puhui_14_1, 0);
        lv_label_set_text(health_countdown_label_, "请将手指放在传感器上");
#if DISPLAY_WIDTH >= 200
        lv_obj_align(health_countdown_label_, LV_ALIGN_BOTTOM_MID, 0, LAYOUT_COUNTDOWN_Y);
#else
        lv_obj_align(health_countdown_label_, LV_ALIGN_TOP_MID, 0, LAYOUT_HEALTH_STATUS_Y);
#endif

        return true;
    }

    void HideHealthOverlay() {
        DisplayLockGuard lock(this);
        FreeSubCanvasBuf(health_canvas_ir_);
        if (health_canvas_ir_) { lv_obj_delete(health_canvas_ir_); health_canvas_ir_ = nullptr; }
        FreeSubCanvasBuf(health_canvas_red_);
        if (health_canvas_red_) { lv_obj_delete(health_canvas_red_); health_canvas_red_ = nullptr; }
        if (health_overlay_) { lv_obj_delete(health_overlay_); health_overlay_ = nullptr; }
        health_title_label_ = nullptr;
        health_hr_label_ = nullptr;
        health_spo2_label_ = nullptr;
        health_status_label_ = nullptr;
        health_countdown_label_ = nullptr;
    }

    void UpdatePpgWaveform(const uint32_t* ir_samples, const uint32_t* red_samples, int count) {
        if (!health_canvas_ir_ || !health_canvas_red_ || !ir_samples || !red_samples || count <= 0) return;
        DisplayLockGuard lock(this);

        auto* db_ir  = lv_canvas_get_draw_buf(health_canvas_ir_);
        auto* db_red = lv_canvas_get_draw_buf(health_canvas_red_);
        if (!db_ir || !db_ir->data || !db_red || !db_red->data) return;
        auto* px_ir  = (lv_color_t*)db_ir->data;
        auto* px_red = (lv_color_t*)db_red->data;

        static const lv_color_t COL_IR  = lv_color_hex(0x00FF55);
        static const lv_color_t COL_RED = lv_color_hex(0xFF3333);
        static const lv_color_t COL_BG  = lv_color_hex(0x000000);
        const int CENTER = SUB_H / 2;

        for (int i = 0; i < count; i++) {
            uint32_t raw_ir  = ir_samples[i]  & 0x03FFFF;
            uint32_t raw_red = red_samples[i] & 0x03FFFF;
            if (raw_ir < 100 || raw_red < 100) continue;

            if (!ppg_baseline_init_) {
                ir_baseline_  = (float)raw_ir;
                red_baseline_ = (float)raw_red;
                ppg_baseline_init_ = true;
            } else {
                ir_baseline_  += ((float)raw_ir  - ir_baseline_)  * 0.05f;
                red_baseline_ += ((float)raw_red - red_baseline_) * 0.05f;
            }

            float ac_ir  = (float)raw_ir  - ir_baseline_;
            float ac_red = (float)raw_red - red_baseline_;

            int ir_y  = CENTER - (int)(ac_ir  * 0.010f);
            int red_y = CENTER - (int)(ac_red * 0.010f);
            ir_y  = (ir_y  < 3) ? 3 : ((ir_y  >= SUB_H - 3) ? SUB_H - 3 : ir_y);
            red_y = (red_y < 3) ? 3 : ((red_y >= SUB_H - 3) ? SUB_H - 3 : red_y);

            int ir_col  = ir_wave_col_;
            int red_col = red_wave_col_;
            ir_wave_col_  = (ir_wave_col_  + 1) % SUB_W;
            red_wave_col_ = (red_wave_col_ + 1) % SUB_W;

            // 清除 IR 画布当前列+下一列
            int ir_next = (ir_col + 1) % SUB_W;
            for (int r = 0; r < SUB_H; r++) {
                px_ir[r * SUB_W + ir_col] = COL_BG;
                px_ir[r * SUB_W + ir_next] = COL_BG;
            }
            // 清除 RED 画布当前列+下一列
            int red_next = (red_col + 1) % SUB_W;
            for (int r = 0; r < SUB_H; r++) {
                px_red[r * SUB_W + red_col] = COL_BG;
                px_red[r * SUB_W + red_next] = COL_BG;
            }

            // IR 绿色线段 (左画布)
            if (ir_prev_col_ >= 0)
                DrawLineOnBuf(px_ir, SUB_W, SUB_H, ir_prev_col_, ir_prev_y_, ir_col, ir_y, COL_IR);
            else
                px_ir[ir_y * SUB_W + ir_col] = COL_IR;
            ir_prev_col_ = ir_col;
            ir_prev_y_   = ir_y;

            // RED 红色线段 (右画布)
            if (red_prev_col_ >= 0)
                DrawLineOnBuf(px_red, SUB_W, SUB_H, red_prev_col_, red_prev_y_, red_col, red_y, COL_RED);
            else
                px_red[red_y * SUB_W + red_col] = COL_RED;
            red_prev_col_ = red_col;
            red_prev_y_   = red_y;
        }

        lv_obj_invalidate(health_canvas_ir_);
        lv_obj_invalidate(health_canvas_red_);
    }

    void ResetPpgBaseline() {
        ir_wave_col_ = 0;  red_wave_col_ = 0;
        ir_baseline_ = 0;  red_baseline_ = 0;
        ir_prev_col_ = -1; red_prev_col_ = -1;
        ppg_baseline_init_ = false;
    }

private:
    static void DrawLineOnBuf(lv_color_t* px, int w, int h,
                               int x0, int y0, int x1, int y1,
                               lv_color_t color) {
        int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
        int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;

        (void)h;
        while (true) {
            for (int yy = y0 - 1; yy <= y0 + 1; yy++) {
                if (x0 >= 0 && x0 < w && yy >= 0 && yy < h)
                    px[yy * w + x0] = color;
            }
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }

public:

    void UpdateHealthReadings(int hr, int spo2) {
        if (!health_hr_label_ || !health_spo2_label_) return;
        DisplayLockGuard lock(this);
        char buf[32];
        if (hr > 0) {
            snprintf(buf, sizeof(buf), "心率: %d bpm", hr);
            lv_label_set_text(health_hr_label_, buf);
        }
        if (spo2 > 0) {
            snprintf(buf, sizeof(buf), "血氧: %d%%", spo2);
            lv_label_set_text(health_spo2_label_, buf);
        }
    }

    void UpdateHealthStatus(const char* text) {
        if (!health_countdown_label_) return;
        DisplayLockGuard lock(this);
        lv_label_set_text(health_countdown_label_, text);
    }

    void ShowMedAlert(const std::string& drug, int slot, int dose) {
        char msg[128];
        snprintf(msg, sizeof(msg), "该吃药了! %s 槽%d·%d颗", drug.c_str(), slot, dose);
        ShowNotification(msg, 10000);
    }

    void DismissNotification() {
        DisplayLockGuard lock(this);
        if (notification_label_ != nullptr) {
            lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (status_label_ != nullptr) {
            lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (notification_timer_ != nullptr) {
            esp_timer_stop(notification_timer_);
        }
        if (dose_overlay_ != nullptr) {
            lv_obj_add_flag(dose_overlay_, LV_OBJ_FLAG_HIDDEN);
        }
    }
};

#endif
