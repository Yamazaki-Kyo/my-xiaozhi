#ifndef SERVO_DEBUG_SERVER_H
#define SERVO_DEBUG_SERVER_H

#include <esp_http_server.h>
#include <esp_log.h>
#include <cJSON.h>
#include <cstdio>
#include "servo_controller.h"

#define TAG_SDS "ServoDebug"

static const char SERVO_DEBUG_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>舵机调试 - 360°</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { font-family: -apple-system, sans-serif; background: #1a1a2e;
         color: #eee; min-height: 100vh; display: flex;
         justify-content: center; align-items: center; }
  .card { background: #16213e; border-radius: 16px; padding: 24px;
          max-width: 440px; width: 100%; margin: 16px;
          box-shadow: 0 8px 32px rgba(0,0,0,.4); }
  h2 { text-align: center; margin-bottom: 4px; color: #e94560; }
  .sub { text-align: center; font-size: 13px; color: #888; margin-bottom: 12px; }

  .pwm-display { text-align: center; margin: 8px 0; }
  .pwm-value { font-size: 48px; font-weight: bold; color: #0f3460;
               background: #e94560; display: inline-block; padding: 4px 20px;
               border-radius: 12px; min-width: 130px; }
  .pwm-label { font-size: 12px; color: #ccc; margin-top: 2px; }

  .dir-indicator { text-align: center; font-size: 20px; margin: 2px 0;
                   height: 28px; color: #888; }
  .dir-indicator.cw { color: #4af; }
  .dir-indicator.ccw { color: #f4a; }

  .status-bar { display: flex; gap: 8px; justify-content: center; margin: 6px 0; flex-wrap: wrap; }
  .chip { font-size: 11px; padding: 3px 8px; border-radius: 20px;
          background: #0f3460; color: #aaa; }
  .chip.ok { background: #1a4a1a; color: #4f4; }
  .chip.warn { background: #332200; color: #ffaa00; }
  .chip.active { background: #1a1a4a; color: #88f; }

  .section { background: #0f3460; border-radius: 10px; padding: 10px; margin: 8px 0; }
  .section h3 { font-size: 12px; color: #aaa; margin-bottom: 6px; }

  .slider-row { margin: 6px 0; }
  .slider-row label { font-size: 12px; color: #ccc; display: flex; justify-content: space-between; }
  .slider-row input[type=range] { width: 100%; margin: 3px 0; }

  .btn-row { display: flex; gap: 6px; justify-content: center; margin: 6px 0; flex-wrap: wrap; }
  button { border: none; border-radius: 8px; padding: 8px 12px; font-size: 13px;
           cursor: pointer; font-weight: bold; transition: .15s; color: #fff; }
  button:active { transform: scale(.95); }
  .btn-preset { background: #533483; }
  .btn-preset.cw { background: #1a4a8a; }
  .btn-preset.ccw { background: #8a1a4a; }
  .btn-stop { background: #c0392b; font-size: 16px; padding: 10px 40px; }

  .home-led { display: inline-block; width: 10px; height: 10px; border-radius: 50%;
              background: #444; margin-right: 4px; }
  .home-led.on { background: #4f4; box-shadow: 0 0 8px #4f4; }

  .msg { text-align: center; font-size: 12px; color: #666; margin-top: 6px; min-height: 16px; }
</style>
</head>
<body>
<div class="card">
  <h2>舵机调试</h2>
  <div class="sub">360° 连续旋转舵机</div>

  <div class="pwm-display">
    <div class="pwm-value" id="pwmText">1500</div>
    <div class="pwm-label">PWM 脉宽 (μs) · 停止中心=<span id="centerText">1500</span>μs</div>
  </div>

  <div class="dir-indicator" id="dirText">已停止</div>

  <div class="status-bar">
    <span class="chip" id="chipHome"><span class="home-led" id="homeLed"></span>归零</span>
    <span class="chip" id="chipSpeed">偏差: 0μs</span>
    <span class="chip" id="chipDead">死区: ±0μs</span>
  </div>

  <!-- 主控滑块 -->
  <div class="section">
    <h3>方向 / 速度</h3>
    <div class="slider-row">
      <input type="range" id="speedSlider" min="-200" max="200" value="0" step="5"
             oninput="onSlider(this.value)">
      <label><span>CW 快</span><span>0 停止</span><span>CCW 快</span></label>
    </div>
  </div>

  <!-- 死区 & 停止中心微调 -->
  <div class="section">
    <h3>死区 & 停止中心微调</h3>
    <div class="slider-row">
      <label>停止中心偏移 <span><span id="trimVal">0</span>μs</span></label>
      <input type="range" id="trimSlider" min="-50" max="50" value="0" step="1"
             oninput="updateTrim(this.value)">
    </div>
    <div class="slider-row">
      <label>死区范围 <span>±<span id="deadVal">0</span>μs</span></label>
      <input type="range" id="deadSlider" min="0" max="30" value="0" step="1"
             oninput="updateDead(this.value)">
      <span style="font-size:10px;color:#666">偏差在死区内=停止，越过死区才开始转</span>
    </div>
  </div>

  <!-- 预设速度 -->
  <div class="section">
    <h3>预设速度</h3>
    <div class="btn-row">
      <button class="btn-preset cw" onclick="setSpeed(-20)">CW 慢</button>
      <button class="btn-preset cw" onclick="setSpeed(-50)">CW 中</button>
      <button class="btn-preset cw" onclick="setSpeed(-100)">CW 快</button>
      <button class="btn-preset ccw" onclick="setSpeed(20)">CCW 慢</button>
      <button class="btn-preset ccw" onclick="setSpeed(50)">CCW 中</button>
      <button class="btn-preset ccw" onclick="setSpeed(100)">CCW 快</button>
    </div>
  </div>

  <div class="btn-row">
    <button class="btn-stop" onclick="doStop()">停止</button>
  </div>

  <div class="msg" id="msg"></div>
</div>

<script>
let currentPwm = 1500;
let currentDev = 0;
let moving = false;
let homeTriggered = false;
let stopTrim = 0;
let deadBand = 0;
let center = 1500;

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
    return null;
  }
}

async function refresh() {
  let data = await api('GET', '/api/servo/status');
  if (!data) return;

  currentPwm = data.pulse;
  currentDev = data.deviation;
  moving = data.moving;
  homeTriggered = data.home_triggered;
  stopTrim = data.stop_trim || 0;
  deadBand = data.dead_band || 0;
  center = data.center || 1500;

  document.getElementById('pwmText').textContent = currentPwm;
  document.getElementById('centerText').textContent = center;

  let dirEl = document.getElementById('dirText');
  let ab = Math.abs(currentDev);
  if (!moving || ab <= deadBand) {
    dirEl.textContent = '已停止';
    dirEl.className = 'dir-indicator';
  } else if (currentDev > 0) {
    dirEl.textContent = '逆时针 CCW (' + currentDev + 'μs)';
    dirEl.className = 'dir-indicator ccw';
  } else {
    dirEl.textContent = '顺时针 CW (' + (-currentDev) + 'μs)';
    dirEl.className = 'dir-indicator cw';
  }

  let hl = document.getElementById('homeLed');
  let hc = document.getElementById('chipHome');
  if (homeTriggered) {
    hl.className = 'home-led on';
    hc.className = 'chip ok';
  } else {
    hl.className = 'home-led';
    hc.className = 'chip';
  }

  document.getElementById('chipSpeed').textContent =
    '偏差: ' + (currentDev >= 0 ? '+' : '') + currentDev + 'μs';
  document.getElementById('chipDead').textContent =
    '死区: ±' + deadBand + 'μs';

  let ss = document.getElementById('speedSlider');
  if (Math.abs(ss.value - currentDev) > 5) ss.value = currentDev;

  document.getElementById('trimSlider').value = stopTrim;
  document.getElementById('trimVal').textContent = (stopTrim >= 0 ? '+' : '') + stopTrim;
  document.getElementById('deadSlider').value = deadBand;
  document.getElementById('deadVal').textContent = deadBand;
}

function onSlider(v) {
  let dev = parseInt(v);
  document.getElementById('msg').textContent = '偏差: ' + (dev >= 0 ? '+' : '') + dev + 'μs';
  sendSpeed(dev);
}

async function setSpeed(dev) {
  document.getElementById('speedSlider').value = dev;
  await sendSpeed(dev);
}

async function sendSpeed(dev) {
  currentDev = dev;
  await api('POST', '/api/servo/rotate', { deviation: dev });
}

async function doStop() {
  document.getElementById('speedSlider').value = 0;
  currentDev = 0;
  await api('POST', '/api/servo/stop');
  document.getElementById('msg').textContent = '已停止';
}

async function updateTrim(v) {
  stopTrim = parseInt(v);
  document.getElementById('trimVal').textContent = (stopTrim >= 0 ? '+' : '') + stopTrim;
  await api('POST', '/api/servo/stop_trim', { stop_trim: stopTrim });
}

async function updateDead(v) {
  deadBand = parseInt(v);
  document.getElementById('deadVal').textContent = deadBand;
  await api('POST', '/api/servo/dead_band', { dead_band: deadBand });
}

refresh();
setInterval(refresh, 400);
</script>
</body>
</html>
)rawliteral";

class ServoDebugServer {
public:
    ServoDebugServer(MedicineDispenser* dispenser)
        : dispenser_(dispenser) {}

    void RegisterHandlers(httpd_handle_t server) {
        server_ = server;

        RegisterUri("/servo-debug", HTTP_GET,
            [](httpd_req_t* req) {
                httpd_resp_set_type(req, "text/html; charset=utf-8");
                httpd_resp_send(req, SERVO_DEBUG_HTML, HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            });

        // GET /api/servo/status
        RegisterUri("/api/servo/status", HTTP_GET,
            [this](httpd_req_t* req) {
                cJSON* r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "pulse", dispenser_->GetPulse());
                cJSON_AddNumberToObject(r, "deviation", dispenser_->GetDeviation());
                cJSON_AddBoolToObject(r, "moving", dispenser_->IsMoving());
                cJSON_AddBoolToObject(r, "home_triggered", dispenser_->IsHomeTriggered());
                cJSON_AddNumberToObject(r, "stop_trim", dispenser_->GetStopTrim());
                cJSON_AddNumberToObject(r, "dead_band", dispenser_->GetDeadBand());
                cJSON_AddNumberToObject(r, "center", dispenser_->EffectiveCenter());
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        // POST /api/servo/rotate
        RegisterUri("/api/servo/rotate", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[64];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';
                cJSON* json = cJSON_Parse(buf);
                if (!json) return ESP_FAIL;
                cJSON* item = cJSON_GetObjectItem(json, "deviation");
                if (cJSON_IsNumber(item))
                    dispenser_->Rotate(item->valueint);
                cJSON_Delete(json);

                cJSON* r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "pulse", dispenser_->GetPulse());
                cJSON_AddNumberToObject(r, "deviation", dispenser_->GetDeviation());
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        // POST /api/servo/stop
        RegisterUri("/api/servo/stop", HTTP_POST,
            [this](httpd_req_t* req) {
                dispenser_->Stop();
                cJSON* r = cJSON_CreateObject();
                cJSON_AddBoolToObject(r, "ok", true);
                cJSON_AddNumberToObject(r, "pulse", dispenser_->GetPulse());
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        // POST /api/servo/stop_trim
        RegisterUri("/api/servo/stop_trim", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[64];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';
                cJSON* json = cJSON_Parse(buf);
                if (!json) return ESP_FAIL;
                cJSON* item = cJSON_GetObjectItem(json, "stop_trim");
                if (cJSON_IsNumber(item))
                    dispenser_->SetStopTrim(item->valueint);
                cJSON_Delete(json);

                cJSON* r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "center", dispenser_->EffectiveCenter());
                cJSON_AddNumberToObject(r, "stop_trim", dispenser_->GetStopTrim());
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        // POST /api/servo/dead_band
        RegisterUri("/api/servo/dead_band", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[64];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';
                cJSON* json = cJSON_Parse(buf);
                if (!json) return ESP_FAIL;
                cJSON* item = cJSON_GetObjectItem(json, "dead_band");
                if (cJSON_IsNumber(item))
                    dispenser_->SetDeadBand(item->valueint);
                cJSON_Delete(json);

                cJSON* r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "dead_band", dispenser_->GetDeadBand());
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        ESP_LOGI(TAG_SDS, "舵机调试页面: http://<IP>/servo-debug");
    }

private:
    MedicineDispenser* dispenser_;
    httpd_handle_t server_;

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
};

#endif
