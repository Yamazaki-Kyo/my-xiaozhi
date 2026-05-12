#ifndef MEDICINE_BOX_DISPLAY_H
#define MEDICINE_BOX_DISPLAY_H

#include "display/lcd_display.h"
#include <lvgl.h>
#include <string>

#define TAG_MBD "MedBoxDisplay"

LV_FONT_DECLARE(font_puhui_14_1);

class MedicineBoxDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* info_panel_ = nullptr;
    lv_obj_t* network_label_extra_ = nullptr;
    lv_obj_t* push_status_label_ = nullptr;
    lv_obj_t* timer_events_label_ = nullptr;

    std::string network_displayed_;
    std::string push_displayed_;
    std::string events_displayed_;

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
            lv_obj_align(emoji_box_, LV_ALIGN_TOP_LEFT, 4, 40);
        }

        auto screen = lv_screen_active();
        int sidebar_w = 105;

        info_panel_ = lv_obj_create(screen);
        lv_obj_set_size(info_panel_, sidebar_w, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(info_panel_, 6, 0);
        lv_obj_set_style_bg_opa(info_panel_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(info_panel_, lv_color_hex(0x101025), 0);
        lv_obj_set_style_border_width(info_panel_, 0, 0);
        lv_obj_set_style_pad_all(info_panel_, 6, 0);
        lv_obj_set_style_pad_row(info_panel_, 4, 0);
        lv_obj_set_flex_flow(info_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(info_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scrollbar_mode(info_panel_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(info_panel_, LV_ALIGN_TOP_RIGHT, -4, 36);

        int label_w = sidebar_w - 12;

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
        lv_obj_set_style_text_color(timer_events_label_, lv_color_hex(0xFFB347), 0);
        lv_obj_set_style_text_font(timer_events_label_, &font_puhui_14_1, 0);
        lv_label_set_text(timer_events_label_, "定时任务\n无");
        events_displayed_ = "定时任务\n无";
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

    void SetTimerEvents(const std::string& events) {
        std::string text = events.empty() ? "定时任务\n无" : "定时任务\n" + events;
        if (text == events_displayed_) return;
        events_displayed_ = text;
        if (timer_events_label_) {
            DisplayLockGuard lock(this);
            lv_label_set_text(timer_events_label_, text.c_str());
        }
    }
};

#endif
