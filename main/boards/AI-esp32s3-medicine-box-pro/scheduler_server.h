#ifndef SCHEDULER_SERVER_H
#define SCHEDULER_SERVER_H

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <vector>
#include <mutex>
#include <ctime>

#define TAG_SCH "Scheduler"

static const char SCHEDULER_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>定时调度器 - AI 智能药盒 Pro</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { font-family: -apple-system, sans-serif; background: #1a1a2e;
         color: #eee; min-height: 100vh; display: flex;
         justify-content: center; align-items: flex-start; padding-top: 24px; }
  .card { background: #16213e; border-radius: 16px; padding: 24px;
          max-width: 480px; width: 100%; margin: 16px;
          box-shadow: 0 8px 32px rgba(0,0,0,.4); }
  h2 { text-align: center; margin-bottom: 4px; color: #e94560; }
  .sub { text-align: center; font-size: 14px; color: #888; margin-bottom: 16px; }

  .clock { text-align: center; margin: 12px 0; }
  .clock-time { font-size: 48px; font-weight: bold; color: #0f3460;
                background: #e94560; display: inline-block; padding: 4px 20px;
                border-radius: 12px; }
  .clock-label { font-size: 13px; color: #666; margin-top: 4px; }
  .clock-date { font-size: 16px; color: #aaa; margin-top: 2px; }

  .warning { background: #332200; color: #ffaa00; padding: 10px 14px; border-radius: 8px;
             font-size: 13px; margin-bottom: 16px; text-align: center; display: none; }

  .form-row { display: flex; gap: 6px; align-items: center; margin: 8px 0; flex-wrap: wrap; }
  input, select { background: #0f3460; color: #eee; border: 1px solid #333;
                  border-radius: 8px; padding: 10px 12px; font-size: 16px; }
  input[type=time] { flex: 1; color-scheme: dark; min-width: 0; }
  select { min-width: 0; }
  .sel-month { width: 66px; }
  .sel-day { width: 66px; }
  .sel-action { width: 80px; }
  .sel-repeat { width: 80px; }
  .sel-end { width: 60px; }
  .date-badge { font-size: 11px; padding: 1px 6px; border-radius: 10px;
                background: #1a3a2a; color: #4ecca3; margin-right: 4px; }
  .rpt-badge { font-size: 11px; padding: 1px 6px; border-radius: 10px;
               margin-left: 4px; background: #2a2a4a; color: #aaa; }
  .rpt-badge.daily { background: #2a1a4a; color: #8b7cf6; }
  .rpt-badge.until { background: #3a2a1a; color: #ffb347; }
  button { border: none; border-radius: 10px; padding: 10px 18px; font-size: 15px;
           cursor: pointer; font-weight: bold; transition: .15s; color: #fff; }
  button:active { transform: scale(.95); }
  .btn-add { background: #e94560; white-space: nowrap; }
  .btn-del { background: #333; padding: 4px 12px; font-size: 13px; }

  .event-list { margin: 16px 0; }
  .event-item { background: #0f3460; border-radius: 8px; padding: 10px 14px;
                margin-bottom: 6px; display: flex; justify-content: space-between;
                align-items: center; flex-wrap: wrap; gap: 4px; }
  .event-info { display: flex; gap: 6px; align-items: center; flex-wrap: wrap; }
  .event-time { font-weight: bold; font-size: 18px; color: #e94560; }
  .event-action { font-size: 14px; color: #ccc; }
  .event-action.on { color: #4ecca3; }
  .event-action.off { color: #ff6b6b; }

  .led-status { text-align: center; margin: 12px 0; font-size: 14px; }
  .led-dot { display: inline-block; width: 12px; height: 12px; border-radius: 50%;
             margin-right: 6px; vertical-align: middle; }
  .led-dot.on { background: #4ecca3; box-shadow: 0 0 10px #4ecca3; }
  .led-dot.off { background: #555; }

  .empty { text-align: center; color: #555; padding: 20px; font-size: 14px; }
  .status { text-align: center; font-size: 13px; color: #888; margin-top: 8px; min-height: 20px; }
  .hidden { display: none; }
</style>
</head>
<body>
<div class="card">
  <h2>定时调度器</h2>
  <div class="sub">设置日期时间自动控制 LED 开关</div>

  <div class="warning" id="warning">等待时间同步中，暂时无法添加定时...</div>

  <div class="clock">
    <div class="clock-date" id="clockDate">----年--月--日</div>
    <div class="clock-time" id="clockTime">--:--</div>
    <div class="clock-label">设备当前时间</div>
  </div>

  <div class="led-status">
    <span class="led-dot off" id="ledDot"></span>
    LED 状态: <strong id="ledText">--</strong>
  </div>

  <div class="form-row">
    <select class="sel-month" id="monthInput">
      <option value="0">每月</option>
    </select>
    <select class="sel-day" id="dayInput">
      <option value="0">每天</option>
    </select>
    <input type="time" id="timeInput" title="时间 (HH:MM)">
  </div>
  <div class="form-row">
    <select class="sel-action" id="actionSelect">
      <option value="led_on">LED 亮</option>
      <option value="led_off">LED 灭</option>
    </select>
    <select class="sel-repeat" id="repeatTypeInput" onchange="onRepeatTypeChange()">
      <option value="0">单次</option>
      <option value="1">每天</option>
      <option value="2">每天直到</option>
    </select>
    <span id="endDateGroup" class="hidden">
      <select class="sel-end" id="endMonthInput"></select>
      <select class="sel-end" id="endDayInput"></select>
    </span>
    <button class="btn-add" onclick="addEvent()">添加</button>
  </div>

  <div class="event-list" id="eventList">
    <div class="empty">暂无定时事件</div>
  </div>

  <div class="status" id="status"></div>
</div>

<script>
// 初始化月/日选择器
(function(){
  let ms = document.getElementById('monthInput');
  for(let i=1; i<=12; i++) {
    let opt = document.createElement('option');
    opt.value = i; opt.textContent = i + '月';
    ms.appendChild(opt);
  }
  let ds = document.getElementById('dayInput');
  for(let i=1; i<=31; i++) {
    let opt = document.createElement('option');
    opt.value = i; opt.textContent = i + '日';
    ds.appendChild(opt);
  }
  // 初始化截止日期选择器
  let em = document.getElementById('endMonthInput');
  for(let i=1; i<=12; i++) {
    let opt = document.createElement('option');
    opt.value = i; opt.textContent = i + '月';
    em.appendChild(opt);
  }
  let ed = document.getElementById('endDayInput');
  for(let i=1; i<=31; i++) {
    let opt = document.createElement('option');
    opt.value = i; opt.textContent = i + '日';
    ed.appendChild(opt);
  }
  // 设置默认截止日期为今天
  let now = new Date();
  em.value = now.getMonth() + 1;
  ed.value = now.getDate();
})();

function onRepeatTypeChange() {
  let rt = parseInt(document.getElementById('repeatTypeInput').value);
  let grp = document.getElementById('endDateGroup');
  if (rt === 2) grp.classList.remove('hidden');
  else grp.classList.add('hidden');
}

function dateLabel(mo, dy) {
  if (mo === 0 && dy === 0) return '';
  if (mo > 0 && dy === 0) return mo + '月 每天';
  if (mo === 0 && dy > 0) return '每月' + dy + '日';
  return mo + '月' + dy + '日';
}

async function api(method, path, body) {
  try {
    let opts = { method };
    if (body) {
      opts.headers = { 'Content-Type': 'application/json' };
      opts.body = JSON.stringify(body);
    }
    let r = await fetch(path, opts);
    return await r.json();
  } catch(e) {
    document.getElementById('status').textContent = '网络错误: ' + e.message;
    return null;
  }
}

async function refresh() {
  let data = await api('GET', '/api/schedule/list');
  if (!data) return;

  if (data.time_valid) {
    document.getElementById('clockTime').textContent = data.current_time;
    if (data.current_date) {
      document.getElementById('clockDate').textContent = data.current_date;
    }
    document.getElementById('warning').style.display = 'none';
  } else {
    document.getElementById('clockTime').textContent = '--:--';
    document.getElementById('clockDate').textContent = '----年--月--日';
    document.getElementById('warning').style.display = 'block';
  }

  let dot = document.getElementById('ledDot');
  let txt = document.getElementById('ledText');
  if (data.led_state) {
    dot.className = 'led-dot on';
    txt.textContent = '亮 (GPIO3 HIGH)';
  } else {
    dot.className = 'led-dot off';
    txt.textContent = '灭 (GPIO3 LOW)';
  }

  let list = document.getElementById('eventList');
  if (!data.events || data.events.length === 0) {
    list.innerHTML = '<div class="empty">暂无定时事件</div>';
  } else {
    list.innerHTML = data.events.map(e => {
      let cls = e.action === 'led_on' ? 'on' : 'off';
      let label = e.action === 'led_on' ? 'LED 亮' : 'LED 灭';
      let timeStr = String(e.hour).padStart(2,'0') + ':' + String(e.minute).padStart(2,'0');
      let dateStr = dateLabel(e.month, e.day);
      let dateHtml = dateStr ? '<span class="date-badge">' + dateStr + '</span>' : '';
      let rptHtml = '';
      if (e.repeat_type === 1) rptHtml = '<span class="rpt-badge daily">每天</span>';
      else if (e.repeat_type === 2) rptHtml = '<span class="rpt-badge until">至' + e.end_month + '月' + e.end_day + '日</span>';
      return '<div class="event-item">' +
        '<div class="event-info">' +
        dateHtml +
        '<span class="event-time">' + timeStr + '</span>' +
        '<span class="event-action ' + cls + '">' + label + '</span>' +
        rptHtml +
        '</div>' +
        '<button class="btn-del" onclick="removeEvent(' + e.id + ')">删除</button>' +
        '</div>';
    }).join('');
  }
}

async function addEvent() {
  let timeVal = document.getElementById('timeInput').value;
  if (!timeVal) {
    document.getElementById('status').textContent = '请选择时间';
    return;
  }
  let parts = timeVal.split(':');
  let month = parseInt(document.getElementById('monthInput').value) || 0;
  let day = parseInt(document.getElementById('dayInput').value) || 0;
  let repeat_type = parseInt(document.getElementById('repeatTypeInput').value);
  let body = {
    month: month,
    day: day,
    hour: parseInt(parts[0]),
    minute: parseInt(parts[1]),
    action: document.getElementById('actionSelect').value,
    repeat_type: repeat_type
  };
  if (repeat_type === 2) {
    body.end_month = parseInt(document.getElementById('endMonthInput').value);
    body.end_day = parseInt(document.getElementById('endDayInput').value);
  }
  let data = await api('POST', '/api/schedule/add', body);
  if (data) {
    document.getElementById('status').textContent =
      data.success ? '已添加定时事件' : ('错误: ' + data.error);
    if (data.success) refresh();
  }
}

async function removeEvent(id) {
  let data = await api('DELETE', '/api/schedule/remove', { id: id });
  if (data) {
    document.getElementById('status').textContent =
      data.success ? '已删除' : ('错误: ' + data.error);
    if (data.success) refresh();
  }
}

refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)rawliteral";

struct ScheduledEvent {
    int id;
    int month;      // 1-12, 0=不限定(每月/每天)
    int day;        // 1-31, 0=不限定(每天)
    int hour;
    int minute;
    int action;     // 0=LED灭, 1=LED亮
    int repeat_type; // 0=单次, 1=每天重复, 2=每天重复直到指定日期
    int end_month;  // 结束月份 (repeat_type=2 时有效, 1-12)
    int end_day;    // 结束日期 (repeat_type=2 时有效, 1-31)
};

class SchedulerServer {
public:
    SchedulerServer(gpio_num_t led_pin)
        : led_pin_(led_pin), server_(nullptr), next_id_(1) {
        // 初始化 LED GPIO
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << led_pin_);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(led_pin_, 0);
        led_state_ = false;

        // 从 NVS 加载断电前保存的事件
        LoadFromNVS();

        // 启动定时器，每 2 秒检查一次
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = &SchedulerServer::TimerCallback;
        timer_args.arg = this;
        timer_args.dispatch_method = ESP_TIMER_TASK;
        timer_args.name = "scheduler_tick";
        esp_timer_create(&timer_args, &timer_);
        esp_timer_start_periodic(timer_, 2000000); // 2 seconds
    }

    /// @brief HTTP 路由注册（网络就绪后由 Board 调用）
    void RegisterHttpHandlers(httpd_handle_t server) {
        server_ = server;
        RegisterHandlers();
    }

    /// @brief 设置事件触发回调（用于播报/通知等）
    void SetEventCallback(std::function<void(const char*)> cb) {
        on_event_fired_ = std::move(cb);
    }

    /// @brief 添加定时事件（供 MCP 工具和 Web API 调用）
    /// @param month 1-12, 0=不限定月份
    /// @param day   1-31, 0=不限定日期
    /// @param repeat_type 0=单次, 1=每天重复, 2=每天重复直到指定日期
    /// @param end_month 结束月份 (仅 repeat_type=2 时有效)
    /// @param end_day   结束日期 (仅 repeat_type=2 时有效)
    /// @return 成功返回 true，失败时 error_out 包含错误描述
    bool AddEvent(int hour, int minute, int action,
                  int repeat_type = 0,
                  int month = 0, int day = 0,
                  int end_month = 0, int end_day = 0,
                  std::string* error_out = nullptr) {
        // 检查时间是否有效
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        if (tm_now.tm_year + 1900 < 2025) {
            if (error_out) *error_out = "设备时间未同步，请等待联网后重试";
            return false;
        }

        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            if (error_out) *error_out = "时间无效 (hour:0-23, minute:0-59)";
            return false;
        }

        if (month < 0 || month > 12 || day < 0 || day > 31) {
            if (error_out) *error_out = "日期无效 (month:0-12, day:0-31)";
            return false;
        }

        if (repeat_type < 0 || repeat_type > 2) {
            if (error_out) *error_out = "repeat_type 无效 (0=单次, 1=每天, 2=直到日期)";
            return false;
        }

        if (repeat_type == 2) {
            if (end_month < 1 || end_month > 12 || end_day < 1 || end_day > 31) {
                if (error_out) *error_out = "结束日期无效，repeat_type=2 时需要有效的 end_month(1-12) 和 end_day(1-31)";
                return false;
            }
        }

        if (action != 0 && action != 1) {
            if (error_out) *error_out = "action 必须为 0 或 1";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& e : events_) {
            if (e.month == month && e.day == day &&
                e.hour == hour && e.minute == minute && e.action == action &&
                e.repeat_type == repeat_type &&
                e.end_month == end_month && e.end_day == end_day) {
                if (error_out) *error_out = "该日期、时间和动作的定时已存在";
                return false;
            }
        }

        ScheduledEvent evt;
        evt.id = next_id_++;
        evt.month = month;
        evt.day = day;
        evt.hour = hour;
        evt.minute = minute;
        evt.action = action;
        evt.repeat_type = repeat_type;
        evt.end_month = end_month;
        evt.end_day = end_day;
        events_.push_back(evt);

        SaveToNVS();

        char date_buf[32] = "";
        if (month > 0 || day > 0) {
            if (month > 0 && day > 0)
                snprintf(date_buf, sizeof(date_buf), "%d月%d日 ", month, day);
            else if (month > 0)
                snprintf(date_buf, sizeof(date_buf), "%d月每天 ", month);
            else
                snprintf(date_buf, sizeof(date_buf), "每月%d日 ", day);
        }
        const char* rpt_label = repeat_type == 0 ? "单次" :
                                repeat_type == 1 ? "每天" : "每天(有截止)";
        ESP_LOGI(TAG_SCH, "添加定时: %s%02d:%02d → %s (id=%d, %s)",
                 date_buf, hour, minute, action == 1 ? "亮" : "灭", evt.id, rpt_label);
        return true;
    }

    std::string GetEventsSummary() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (events_.empty()) return "";
        std::string result;
        int count = 0;
        for (auto& e : events_) {
            if (count >= 4) { result += "..."; break; }
            char buf[56];
            const char* rpt = e.repeat_type == 1 ? " 每天" :
                              e.repeat_type == 2 ? " 截止" : "";

            // 日期前缀
            char date_pre[32] = "";
            if (e.month > 0 && e.day > 0)
                snprintf(date_pre, sizeof(date_pre), "%d/%d ", e.month, e.day);
            else if (e.month > 0)
                snprintf(date_pre, sizeof(date_pre), "%d月 ", e.month);
            else if (e.day > 0)
                snprintf(date_pre, sizeof(date_pre), "%d日 ", e.day);

            snprintf(buf, sizeof(buf), "%s%02d:%02d %s%s\n",
                     date_pre, e.hour, e.minute,
                     e.action == 1 ? "亮" : "灭", rpt);
            result += buf;
            count++;
        }
        return result;
    }

private:
    gpio_num_t led_pin_;
    httpd_handle_t server_;
    int next_id_;
    bool led_state_;
    std::vector<ScheduledEvent> events_;
    std::mutex mutex_;
    esp_timer_handle_t timer_;
    std::function<void(const char*)> on_event_fired_;

    static constexpr const char* NVS_NAMESPACE = "scheduler";
    static constexpr const char* NVS_KEY = "events";

    void SaveToNVS() {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

        cJSON* arr = cJSON_CreateArray();
        for (auto& e : events_) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", e.id);
            cJSON_AddNumberToObject(item, "h", e.hour);
            cJSON_AddNumberToObject(item, "m", e.minute);
            cJSON_AddNumberToObject(item, "a", e.action);
            cJSON_AddNumberToObject(item, "mo", e.month);
            cJSON_AddNumberToObject(item, "d", e.day);
            cJSON_AddNumberToObject(item, "rt", e.repeat_type);
            cJSON_AddNumberToObject(item, "em", e.end_month);
            cJSON_AddNumberToObject(item, "ed", e.end_day);
            cJSON_AddItemToArray(arr, item);
        }
        char* js = cJSON_PrintUnformatted(arr);
        if (js) {
            nvs_set_str(handle, NVS_KEY, js);
            nvs_commit(handle);
            cJSON_free(js);
        }
        cJSON_Delete(arr);
        nvs_close(handle);
    }

    void LoadFromNVS() {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;

        size_t len = 0;
        if (nvs_get_str(handle, NVS_KEY, nullptr, &len) != ESP_OK) {
            nvs_close(handle);
            return;
        }

        char* js = new char[len];
        if (nvs_get_str(handle, NVS_KEY, js, &len) != ESP_OK) {
            delete[] js;
            nvs_close(handle);
            return;
        }
        nvs_close(handle);

        cJSON* arr = cJSON_Parse(js);
        delete[] js;
        if (!arr || !cJSON_IsArray(arr)) {
            if (arr) cJSON_Delete(arr);
            return;
        }

        int max_id = 0;
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, arr) {
            ScheduledEvent evt;
            evt.id = cJSON_GetObjectItem(item, "id")->valueint;
            evt.hour = cJSON_GetObjectItem(item, "h")->valueint;
            evt.minute = cJSON_GetObjectItem(item, "m")->valueint;
            evt.action = cJSON_GetObjectItem(item, "a")->valueint;
            cJSON* mo = cJSON_GetObjectItem(item, "mo");
            evt.month = mo ? mo->valueint : 0;
            cJSON* d = cJSON_GetObjectItem(item, "d");
            evt.day = d ? d->valueint : 0;

            // 读取新格式 repeat_type，兼容旧格式 r (repeat count)
            cJSON* rt = cJSON_GetObjectItem(item, "rt");
            if (rt) {
                evt.repeat_type = rt->valueint;
                cJSON* em = cJSON_GetObjectItem(item, "em");
                evt.end_month = em ? em->valueint : 0;
                cJSON* ed = cJSON_GetObjectItem(item, "ed");
                evt.end_day = ed ? ed->valueint : 0;
            } else {
                // 兼容旧数据：r=-1→每天, r=1→单次, r>1→每天
                cJSON* r = cJSON_GetObjectItem(item, "r");
                int old_repeat = r ? r->valueint : 1;
                evt.repeat_type = (old_repeat == 1) ? 0 : 1;
                evt.end_month = 0;
                evt.end_day = 0;
            }
            events_.push_back(evt);
            if (evt.id > max_id) max_id = evt.id;
        }
        next_id_ = max_id + 1;
        cJSON_Delete(arr);
        ESP_LOGI(TAG_SCH, "从 NVS 加载了 %d 个定时事件", (int)events_.size());
    }

    static void TimerCallback(void* arg) {
        auto* self = (SchedulerServer*)arg;
        self->CheckAndExecute();
    }

    void CheckAndExecute() {
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        if (tm_now.tm_year + 1900 < 2025) return; // 时间未同步

        int now_min = tm_now.tm_hour * 60 + tm_now.tm_min;
        int now_month = tm_now.tm_mon + 1;
        int now_day = tm_now.tm_mday;

        std::lock_guard<std::mutex> lock(mutex_);
        bool changed = false;
        for (auto it = events_.begin(); it != events_.end(); ) {
            int evt_min = it->hour * 60 + it->minute;

            // 日期匹配：0=不限定，否则必须等于当前月/日
            bool date_match = true;
            if (it->month != 0 && it->month != now_month) date_match = false;
            if (it->day != 0 && it->day != now_day) date_match = false;

            if (evt_min == now_min && date_match && tm_now.tm_sec < 5) {

                // 检查是否已过截止日期 (repeat_type=2)
                if (it->repeat_type == 2) {
                    int now_md = now_month * 100 + now_day;
                    int end_md = it->end_month * 100 + it->end_day;
                    if (now_md > end_md) {
                        // 已超过截止日期，删除此事件
                        ESP_LOGI(TAG_SCH, "定时过期: %02d:%02d (id=%d, 截止%d月%d日)",
                                 it->hour, it->minute, it->id,
                                 it->end_month, it->end_day);
                        it = events_.erase(it);
                        changed = true;
                        continue;
                    }
                }

                char date_buf[32] = "";
                if (it->month > 0 || it->day > 0) {
                    if (it->month > 0 && it->day > 0)
                        snprintf(date_buf, sizeof(date_buf), "%d月%d日 ", it->month, it->day);
                    else if (it->month > 0)
                        snprintf(date_buf, sizeof(date_buf), "%d月每天 ", it->month);
                    else
                        snprintf(date_buf, sizeof(date_buf), "每月%d日 ", it->day);
                }
                const char* rpt_label = it->repeat_type == 0 ? "单次" :
                                        it->repeat_type == 1 ? "每天" : "每天(截止)";
                ESP_LOGI(TAG_SCH, "定时触发: %s%02d:%02d → %s (id=%d, %s)",
                         date_buf, it->hour, it->minute,
                         it->action == 1 ? "LED亮" : "LED灭",
                         it->id, rpt_label);

                if (it->action == 1) {
                    gpio_set_level(led_pin_, 1);
                    led_state_ = true;
                } else {
                    gpio_set_level(led_pin_, 0);
                    led_state_ = false;
                }

                // 播报回调
                if (on_event_fired_) {
                    char msg[96];
                    char date_part[32] = "";
                    if (it->month > 0 && it->day > 0)
                        snprintf(date_part, sizeof(date_part), "%d月%d日", it->month, it->day);
                    else if (it->month > 0)
                        snprintf(date_part, sizeof(date_part), "%d月每天", it->month);
                    else if (it->day > 0)
                        snprintf(date_part, sizeof(date_part), "每月%d日", it->day);
                    const char* action_name = it->action == 1 ? "LED已开启" : "LED已关闭";
                    if (date_part[0])
                        snprintf(msg, sizeof(msg), "%s %02d:%02d %s", date_part, it->hour, it->minute, action_name);
                    else
                        snprintf(msg, sizeof(msg), "%02d:%02d %s", it->hour, it->minute, action_name);
                    on_event_fired_(msg);
                }

                if (it->repeat_type == 0) {
                    // 单次执行完毕，删除
                    it = events_.erase(it);
                    changed = true;
                    continue;
                }
                // repeat_type==1 (每天) 或 repeat_type==2 (每天直到日期): 保留
            }
            ++it;
        }
        if (changed) SaveToNVS();
    }

    using HandlerFunc = std::function<esp_err_t(httpd_req_t*)>;

    void RegisterUri(const char* uri, httpd_method_t method,
                     HandlerFunc handler) {
        auto* ctx = new HandlerFunc(std::move(handler));
        httpd_uri_t uri_cfg = {};
        uri_cfg.uri = uri;
        uri_cfg.method = method;
        uri_cfg.handler = [](httpd_req_t* req) -> esp_err_t {
            auto* h = (HandlerFunc*)req->user_ctx;
            return (*h)(req);
        };
        uri_cfg.user_ctx = ctx;
        httpd_register_uri_handler(server_, &uri_cfg);
    }

    void RegisterHandlers() {
        // GET /scheduler → HTML 页面
        RegisterUri("/scheduler", HTTP_GET,
            [](httpd_req_t* req) {
                httpd_resp_set_type(req, "text/html; charset=utf-8");
                httpd_resp_send(req, SCHEDULER_HTML, HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            });

        // GET /api/schedule/list → 事件列表 + 当前状态
        RegisterUri("/api/schedule/list", HTTP_GET,
            [this](httpd_req_t* req) {
                cJSON* root = cJSON_CreateObject();

                time_t now = time(NULL);
                struct tm tm_now;
                localtime_r(&now, &tm_now);
                bool time_valid = (tm_now.tm_year + 1900 >= 2025);
                cJSON_AddBoolToObject(root, "time_valid", time_valid);

                char time_buf[6];
                char date_buf[48];
                if (time_valid) {
                    snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
                             tm_now.tm_hour, tm_now.tm_min);
                    snprintf(date_buf, sizeof(date_buf), "%d年%d月%d日",
                             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
                } else {
                    snprintf(time_buf, sizeof(time_buf), "--:--");
                    snprintf(date_buf, sizeof(date_buf), "----年--月--日");
                }
                cJSON_AddStringToObject(root, "current_time", time_buf);
                cJSON_AddStringToObject(root, "current_date", date_buf);
                cJSON_AddBoolToObject(root, "led_state", led_state_);

                cJSON* arr = cJSON_AddArrayToObject(root, "events");
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (auto& e : events_) {
                        cJSON* item = cJSON_CreateObject();
                        cJSON_AddNumberToObject(item, "id", e.id);
                        cJSON_AddNumberToObject(item, "month", e.month);
                        cJSON_AddNumberToObject(item, "day", e.day);
                        cJSON_AddNumberToObject(item, "hour", e.hour);
                        cJSON_AddNumberToObject(item, "minute", e.minute);
                        cJSON_AddStringToObject(item, "action",
                            e.action == 1 ? "led_on" : "led_off");
                        cJSON_AddNumberToObject(item, "repeat_type", e.repeat_type);
                        cJSON_AddNumberToObject(item, "end_month", e.end_month);
                        cJSON_AddNumberToObject(item, "end_day", e.end_day);
                        cJSON_AddItemToArray(arr, item);
                    }
                }

                char* js = cJSON_PrintUnformatted(root);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(root);
                return ESP_OK;
            });

        // POST /api/schedule/add → 添加事件
        RegisterUri("/api/schedule/add", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[128];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';

                cJSON* resp = cJSON_CreateObject();
                cJSON* json = cJSON_Parse(buf);
                if (!json) {
                    cJSON_AddBoolToObject(resp, "success", false);
                    cJSON_AddStringToObject(resp, "error", "无效的 JSON");
                    goto send_resp;
                }

                // 检查时间是否有效
                {
                    time_t now = time(NULL);
                    struct tm tm_now;
                    localtime_r(&now, &tm_now);
                    if (tm_now.tm_year + 1900 < 2025) {
                        cJSON_AddBoolToObject(resp, "success", false);
                        cJSON_AddStringToObject(resp, "error", "设备时间未同步，请等待联网后重试");
                        cJSON_Delete(json);
                        goto send_resp;
                    }
                }

                {
                    cJSON* h = cJSON_GetObjectItem(json, "hour");
                    cJSON* m = cJSON_GetObjectItem(json, "minute");
                    cJSON* a = cJSON_GetObjectItem(json, "action");

                    if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m) || !cJSON_IsString(a)) {
                        cJSON_AddBoolToObject(resp, "success", false);
                        cJSON_AddStringToObject(resp, "error", "缺少必要参数 (hour, minute, action)");
                        cJSON_Delete(json);
                        goto send_resp;
                    }

                    int hour = h->valueint;
                    int minute = m->valueint;
                    const char* action_str = a->valuestring;

                    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
                        cJSON_AddBoolToObject(resp, "success", false);
                        cJSON_AddStringToObject(resp, "error", "时间无效 (hour:0-23, minute:0-59)");
                        cJSON_Delete(json);
                        goto send_resp;
                    }

                    int action_val;
                    if (strcmp(action_str, "led_on") == 0) {
                        action_val = 1;
                    } else if (strcmp(action_str, "led_off") == 0) {
                        action_val = 0;
                    } else {
                        cJSON_AddBoolToObject(resp, "success", false);
                        cJSON_AddStringToObject(resp, "error", "action 必须为 led_on 或 led_off");
                        cJSON_Delete(json);
                        goto send_resp;
                    }

                    // 解析 repeat_type (可选，默认0=单次)
                    int repeat_type = 0;
                    cJSON* rt = cJSON_GetObjectItem(json, "repeat_type");
                    if (cJSON_IsNumber(rt)) {
                        repeat_type = rt->valueint;
                        if (repeat_type < 0 || repeat_type > 2) {
                            cJSON_AddBoolToObject(resp, "success", false);
                            cJSON_AddStringToObject(resp, "error", "repeat_type 无效 (0=单次, 1=每天, 2=直到日期)");
                            cJSON_Delete(json);
                            goto send_resp;
                        }
                    }

                    // 解析 month/day (可选，默认0=不限定)
                    int month = 0, day = 0;
                    cJSON* mo = cJSON_GetObjectItem(json, "month");
                    if (cJSON_IsNumber(mo)) month = mo->valueint;
                    cJSON* dy = cJSON_GetObjectItem(json, "day");
                    if (cJSON_IsNumber(dy)) day = dy->valueint;

                    // 解析 end_month/end_day (repeat_type=2 时需要)
                    int end_month = 0, end_day = 0;
                    cJSON* em = cJSON_GetObjectItem(json, "end_month");
                    if (cJSON_IsNumber(em)) end_month = em->valueint;
                    cJSON* ed = cJSON_GetObjectItem(json, "end_day");
                    if (cJSON_IsNumber(ed)) end_day = ed->valueint;

                    std::string error;
                    if (AddEvent(hour, minute, action_val, repeat_type,
                                 month, day, end_month, end_day, &error)) {
                        cJSON_AddBoolToObject(resp, "success", true);
                    } else {
                        cJSON_AddBoolToObject(resp, "success", false);
                        cJSON_AddStringToObject(resp, "error", error.c_str());
                    }
                }
                cJSON_Delete(json);

            send_resp:
                char* js = cJSON_PrintUnformatted(resp);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(resp);
                return ESP_OK;
            });

        // DELETE /api/schedule/remove → 删除事件
        RegisterUri("/api/schedule/remove", HTTP_DELETE,
            [this](httpd_req_t* req) {
                char buf[64];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';

                cJSON* resp = cJSON_CreateObject();
                cJSON* json = cJSON_Parse(buf);
                if (!json) {
                    cJSON_AddBoolToObject(resp, "success", false);
                    cJSON_AddStringToObject(resp, "error", "无效的 JSON");
                    goto send_del;
                }

                {
                    cJSON* id_json = cJSON_GetObjectItem(json, "id");
                    if (!cJSON_IsNumber(id_json)) {
                        cJSON_AddBoolToObject(resp, "success", false);
                        cJSON_AddStringToObject(resp, "error", "缺少 id 参数");
                        cJSON_Delete(json);
                        goto send_del;
                    }

                    int target_id = id_json->valueint;
                    std::lock_guard<std::mutex> lock(mutex_);
                    bool found = false;
                    for (auto it = events_.begin(); it != events_.end(); ++it) {
                        if (it->id == target_id) {
                            ESP_LOGI(TAG_SCH, "删除定时: %02d:%02d (id=%d)",
                                     it->hour, it->minute, it->id);
                            events_.erase(it);
                            found = true;
                            break;
                        }
                    }
                    if (found) SaveToNVS();
                    cJSON_AddBoolToObject(resp, "success", found);
                    if (!found) {
                        cJSON_AddStringToObject(resp, "error", "未找到该事件");
                    }
                }
                cJSON_Delete(json);

            send_del:
                char* js = cJSON_PrintUnformatted(resp);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(resp);
                return ESP_OK;
            });
    }
};

#endif
