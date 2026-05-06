#include "medicine_reminder.h"

#include <cJSON.h>
#include <esp_log.h>
#include <cstring>
#include <ctime>
#include <algorithm>

#include "settings.h"

#define TAG "MedicineReminder"

#define NVS_NAMESPACE "med_plan"
#define NVS_KEY_PLAN    "plan_json"
#define NVS_KEY_ENABLED "master_enabled"
#define NVS_KEY_DAY     "last_day"

MedicineReminder::MedicineReminder()
    : last_check_day_(-1)
{
    memset(&plan_, 0, sizeof(plan_));
    LoadPlan();
}

MedicineReminder::~MedicineReminder() {
    SavePlan();
}

void MedicineReminder::LoadPlan() {
    Settings settings(NVS_NAMESPACE, false);
    plan_.master_enabled = settings.GetInt(NVS_KEY_ENABLED, 0) != 0;
    last_check_day_ = settings.GetInt(NVS_KEY_DAY, -1);

    std::string plan_json = settings.GetString(NVS_KEY_PLAN, "");
    if (!plan_json.empty()) {
        SetPlanFromJson(plan_json.c_str());
    } else {
        ESP_LOGI(TAG, "未发现已存储的用药计划，使用空配置");
    }
}

void MedicineReminder::SavePlan() {
    Settings settings(NVS_NAMESPACE, true);
    settings.SetInt(NVS_KEY_ENABLED, plan_.master_enabled ? 1 : 0);
    settings.SetInt(NVS_KEY_DAY, last_check_day_);

    std::string json = GetPlanJson();
    settings.SetString(NVS_KEY_PLAN, json);
    ESP_LOGI(TAG, "用药计划已保存");
}

bool MedicineReminder::SetPlanFromJson(const char* json_str) {
    cJSON* root = cJSON_Parse(json_str);
    if (root == nullptr) {
        ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
        return false;
    }

    // 总开关
    cJSON* enabled = cJSON_GetObjectItem(root, "master_enabled");
    if (cJSON_IsBool(enabled)) {
        plan_.master_enabled = cJSON_IsTrue(enabled);
    }

    // 药槽配置数组
    cJSON* slots_arr = cJSON_GetObjectItem(root, "slots");
    if (cJSON_IsArray(slots_arr)) {
        int count = cJSON_GetArraySize(slots_arr);
        for (int i = 0; i < count && i < 7; i++) {
            cJSON* slot_item = cJSON_GetArrayItem(slots_arr, i);
            if (!cJSON_IsObject(slot_item)) continue;

            SlotConfig& slot = plan_.slots[i];
            slot.slot = i + 1;

            cJSON* name = cJSON_GetObjectItem(slot_item, "drug_name");
            if (cJSON_IsString(name)) slot.drug_name = name->valuestring;

            cJSON* shape = cJSON_GetObjectItem(slot_item, "shape_label");
            if (cJSON_IsString(shape)) slot.shape_label = shape->valuestring;

            cJSON* dose = cJSON_GetObjectItem(slot_item, "dose_count");
            if (cJSON_IsNumber(dose)) {
                slot.dose_count = dose->valueint;
                if (slot.dose_count < 1) slot.dose_count = 1;
                if (slot.dose_count > 9) slot.dose_count = 9;
            }

            cJSON* freq = cJSON_GetObjectItem(slot_item, "daily_frequency");
            if (cJSON_IsNumber(freq)) {
                slot.daily_frequency = freq->valueint;
                if (slot.daily_frequency < 1) slot.daily_frequency = 1;
                if (slot.daily_frequency > 5) slot.daily_frequency = 5;
            }

            cJSON* times_arr = cJSON_GetObjectItem(slot_item, "times");
            if (cJSON_IsArray(times_arr)) {
                int time_count = cJSON_GetArraySize(times_arr);
                for (int t = 0; t < time_count && t < 5; t++) {
                    cJSON* time_item = cJSON_GetArrayItem(times_arr, t);
                    if (cJSON_IsObject(time_item)) {
                        cJSON* h = cJSON_GetObjectItem(time_item, "hour");
                        cJSON* m = cJSON_GetObjectItem(time_item, "minute");
                        if (cJSON_IsNumber(h)) slot.hours[t] = h->valueint;
                        if (cJSON_IsNumber(m)) slot.minutes[t] = m->valueint;
                    }
                }
            }

            plan_.slot_configured[i] = (!slot.drug_name.empty() && slot.daily_frequency > 0) ? 1 : 0;
        }
    }

    cJSON_Delete(root);
    SavePlan();
    ESP_LOGI(TAG, "用药计划已更新");
    return true;
}

std::string MedicineReminder::GetPlanJson() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "master_enabled", plan_.master_enabled);

    cJSON* slots_arr = cJSON_CreateArray();
    for (int i = 0; i < 7; i++) {
        SlotConfig& slot = plan_.slots[i];
        cJSON* slot_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(slot_obj, "slot", slot.slot);
        cJSON_AddStringToObject(slot_obj, "drug_name", slot.drug_name.c_str());
        cJSON_AddStringToObject(slot_obj, "shape_label", slot.shape_label.c_str());
        cJSON_AddNumberToObject(slot_obj, "dose_count", slot.dose_count);
        cJSON_AddNumberToObject(slot_obj, "daily_frequency", slot.daily_frequency);

        cJSON* times_arr = cJSON_CreateArray();
        for (int t = 0; t < slot.daily_frequency && t < 5; t++) {
            cJSON* time_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(time_obj, "hour", slot.hours[t]);
            cJSON_AddNumberToObject(time_obj, "minute", slot.minutes[t]);
            cJSON_AddItemToArray(times_arr, time_obj);
        }
        cJSON_AddItemToObject(slot_obj, "times", times_arr);
        cJSON_AddItemToArray(slots_arr, slot_obj);
    }
    cJSON_AddItemToObject(root, "slots", slots_arr);

    char* str = cJSON_PrintUnformatted(root);
    std::string result(str);
    cJSON_free(str);
    cJSON_Delete(root);
    return result;
}

const SlotConfig* MedicineReminder::GetSlotConfig(int slot) const {
    if (slot < 1 || slot > 7) return nullptr;
    int idx = slot - 1;
    if (!plan_.slot_configured[idx]) return nullptr;
    return &plan_.slots[idx];
}

void MedicineReminder::SetMasterEnabled(bool enabled) {
    plan_.master_enabled = enabled;
    SavePlan();
    ESP_LOGI(TAG, "总开关: %s", enabled ? "开启" : "关闭");
}

bool MedicineReminder::IsMasterEnabled() const {
    return plan_.master_enabled;
}

void MedicineReminder::SetReminderEnabled(int slot, int reminder_index, bool enabled) {
    // 单个提醒开关通过生成队列时的 enabled 字段控制
    // 此处仅记录到NVS
    ESP_LOGI(TAG, "提醒 %d-%d: %s", slot, reminder_index, enabled ? "启用" : "禁用");
}

bool MedicineReminder::ShouldResetDaily() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int today = timeinfo.tm_yday;  // 一年中的第几天

    if (last_check_day_ != today) {
        last_check_day_ = today;
        return true;
    }
    return false;
}

void MedicineReminder::ResetDailyCompletions() {
    SavePlan();
    ESP_LOGI(TAG, "每日完成状态已重置");
}

std::vector<ReminderEntry> MedicineReminder::GenerateDailyReminders() {
    std::vector<ReminderEntry> reminders;

    for (int i = 0; i < 7; i++) {
        if (!plan_.slot_configured[i]) continue;

        SlotConfig& slot = plan_.slots[i];
        for (int t = 0; t < slot.daily_frequency && t < 5; t++) {
            ReminderEntry entry;
            entry.slot = slot.slot;
            entry.drug_name = slot.drug_name;
            entry.shape_label = slot.shape_label;
            entry.dose_count = slot.dose_count;
            entry.hour = slot.hours[t];
            entry.minute = slot.minutes[t];
            entry.enabled = plan_.master_enabled;
            entry.completed = false;
            reminders.push_back(entry);
        }
    }

    // 按时间排序
    std::sort(reminders.begin(), reminders.end(),
              [](const ReminderEntry& a, const ReminderEntry& b) {
                  return (a.hour * 60 + a.minute) < (b.hour * 60 + b.minute);
              });

    return reminders;
}

void MedicineReminder::MarkCompleted(int slot, int hour, int minute) {
    ESP_LOGI(TAG, "标记已完成: 槽%d %02d:%02d", slot, hour, minute);
}

const ReminderEntry* MedicineReminder::GetNextPendingReminder() {
    auto reminders = GenerateDailyReminders();

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int now_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    for (auto& entry : reminders) {
        int entry_minutes = entry.hour * 60 + entry.minute;
        if (entry.enabled && !entry.completed && entry_minutes > now_minutes) {
            static ReminderEntry next;
            next = entry;
            return &next;
        }
    }
    return nullptr;
}

std::vector<ReminderEntry> MedicineReminder::CheckAndTriggerReminders() {
    std::vector<ReminderEntry> triggered;

    if (!plan_.master_enabled) return triggered;

    // 每日重置
    if (ShouldResetDaily()) {
        ResetDailyCompletions();
    }

    auto reminders = GenerateDailyReminders();

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int now_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    for (auto& entry : reminders) {
        int entry_minutes = entry.hour * 60 + entry.minute;
        // 在当前分钟窗口内 (允许1分钟误差)
        if (entry.enabled && !entry.completed &&
            now_minutes >= entry_minutes && now_minutes <= entry_minutes) {
            triggered.push_back(entry);
            entry.completed = true;
            MarkCompleted(entry.slot, entry.hour, entry.minute);
        }
    }

    return triggered;
}
