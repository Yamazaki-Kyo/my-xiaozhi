#ifndef _MEDICINE_REMINDER_H_
#define _MEDICINE_REMINDER_H_

#include <cstdint>
#include <string>
#include <vector>

// 单条提醒记录
struct ReminderEntry {
    int slot;             // 药槽号 1-7
    std::string drug_name;
    std::string shape_label;
    int dose_count;       // 每次颗数
    int hour;             // 提醒时间 小时 (0-23)
    int minute;           // 提醒时间 分钟 (0-59)
    bool enabled;         // 是否启用
    bool completed;       // 今日是否已完成
};

// 单个药槽配置
struct SlotConfig {
    int slot;                  // 槽号 1-7
    std::string drug_name;
    std::string shape_label;
    int dose_count;            // 每次颗数 (1-9)
    int daily_frequency;       // 每日次数 (1-5)
    int hours[5];              // 提醒时间 小时
    int minutes[5];            // 提醒时间 分钟
};

// 药品提醒计划
struct MedicinePlan {
    SlotConfig slots[7];       // 7个药槽配置
    int slot_configured[7];    // 是否已配置 (0/1)
    bool master_enabled;       // 总开关
};

class MedicineReminder {
public:
    MedicineReminder();
    ~MedicineReminder();

    // 从 NVS 加载计划
    void LoadPlan();

    // 保存计划到 NVS
    void SavePlan();

    // 从 JSON 字符串设置完整计划 (来自网页端)
    bool SetPlanFromJson(const char* json_str);

    // 导出当前计划为 JSON 字符串
    std::string GetPlanJson();

    // 获取指定药槽配置
    const SlotConfig* GetSlotConfig(int slot) const;

    // 设置总开关
    void SetMasterEnabled(bool enabled);
    bool IsMasterEnabled() const;

    // 启用/禁用单个提醒
    void SetReminderEnabled(int slot, int reminder_index, bool enabled);

    // 生成今日提醒队列 (按时间排序)
    std::vector<ReminderEntry> GenerateDailyReminders();

    // 标记提醒为已完成
    void MarkCompleted(int slot, int hour, int minute);

    // 重置今日所有完成状态
    void ResetDailyCompletions();

    // 获取指定槽的下一个未完成提醒 (无则返回nullptr)
    const ReminderEntry* GetNextPendingReminder();

    // 定时检查：返回当前时间应该触发的提醒列表
    std::vector<ReminderEntry> CheckAndTriggerReminders();

    // 当前日期（用于判断是否需要重置每日状态）
    bool ShouldResetDaily();

private:
    MedicinePlan plan_;
    int last_check_day_;  // 上次检查的日期 (用于每日重置)
};

#endif // _MEDICINE_REMINDER_H_
