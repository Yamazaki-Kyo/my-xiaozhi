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
          max-width: 460px; width: 100%; margin: 16px;
          box-shadow: 0 8px 32px rgba(0,0,0,.4); }
  h2 { text-align: center; margin-bottom: 4px; color: #e94560; }
  .sub { text-align: center; font-size: 12px; color: #888; margin-bottom: 8px; }

  .main-display { text-align: center; margin: 6px 0; }
  .speed-big { font-size: 52px; font-weight: bold; }
  .speed-big.cw { color: #4af; }
  .speed-big.ccw { color: #f4a; }
  .speed-big.stop { color: #888; }
  .speed-sub { font-size: 13px; color: #aaa; margin-top: 2px; }

  .status-bar { display: flex; gap: 6px; justify-content: center; margin: 6px 0; flex-wrap: wrap; }
  .chip { font-size: 11px; padding: 3px 8px; border-radius: 20px;
          background: #0f3460; color: #aaa; }
  .chip.ok { background: #1a4a1a; color: #4f4; }
  .chip.active { background: #1a1a4a; color: #88f; }
  .home-led { display: inline-block; width: 8px; height: 8px; border-radius: 50%;
              background: #444; margin-right: 3px; }
  .home-led.on { background: #4f4; box-shadow: 0 0 6px #4f4; }

  .section { background: #0f3460; border-radius: 10px; padding: 10px; margin: 8px 0; }
  .section h3 { font-size: 12px; color: #aaa; margin-bottom: 6px; }

  .slider-row { margin: 5px 0; }
  .slider-row label { font-size: 11px; color: #ccc; display: flex; justify-content: space-between; }
  .slider-row input[type=range] { width: 100%; margin: 3px 0; }

  .btn-row { display: flex; gap: 5px; justify-content: center; margin: 5px 0; flex-wrap: wrap; }
  button { border: none; border-radius: 8px; padding: 8px 12px; font-size: 13px;
           cursor: pointer; font-weight: bold; transition: .15s; color: #fff; }
  button:active { transform: scale(.95); }
  .btn-preset { background: #533483; min-width: 70px; }
  .btn-preset.cw { background: #1a4a8a; }
  .btn-preset.ccw { background: #8a1a4a; }
  .btn-stop { background: #c0392b; font-size: 16px; padding: 10px 44px; }

  .msg { text-align: center; font-size: 11px; color: #666; margin-top: 6px; min-height: 14px; }
</style>
</head>
<body>
<div class="card">
  <h2>舵机调试</h2>
  <div class="sub">360° 连续旋转 | 因子=<span id="factorText">0.30</span> °/s/μs</div>

  <div class="main-display">
    <div class="speed-big stop" id="speedBig">0 °/s</div>
    <div class="speed-sub">PWM <span id="pwmText">1500</span> μs | 偏差 <span id="devText">0</span> μs</div>
  </div>

  <div class="status-bar">
    <span class="chip" id="chipHome"><span class="home-led" id="homeLed"></span>归零</span>
    <span class="chip" id="chipDead">死区: ±0μs</span>
  </div>

  <!-- 主控滑块 -->
  <div class="section">
    <h3>速度控制 (±<span id="maxSpeed">90</span>°/s 满量程)</h3>
    <div class="slider-row">
      <input type="range" id="speedSlider" min="-300" max="300" value="0" step="3"
             oninput="onSlider(this.value)">
      <label><span>CW</span><span id="sliderCenter">0 停止</span><span>CCW</span></label>
    </div>
  </div>

  <!-- 预设速度 -->
  <div class="section">
    <h3>预设速度</h3>
    <div class="btn-row">
      <button class="btn-preset cw" onclick="setSpeed(-33)">CW 10°/s</button>
      <button class="btn-preset cw" onclick="setSpeed(-67)">CW 20°/s</button>
      <button class="btn-preset cw" onclick="setSpeed(-133)">CW 40°/s</button>
      <button class="btn-preset cw" onclick="setSpeed(-200)">CW 60°/s</button>
    </div>
    <div class="btn-row">
      <button class="btn-preset ccw" onclick="setSpeed(33)">CCW 10°/s</button>
      <button class="btn-preset ccw" onclick="setSpeed(67)">CCW 20°/s</button>
      <button class="btn-preset ccw" onclick="setSpeed(133)">CCW 40°/s</button>
      <button class="btn-preset ccw" onclick="setSpeed(200)">CCW 60°/s</button>
    </div>
  </div>

  <!-- 参数微调 -->
  <div class="section">
    <h3>参数微调</h3>
    <div class="slider-row">
      <label>速度因子 <span id="factorVal">0.30</span> °/s/μs</label>
      <input type="range" id="factorSlider" min="0.10" max="1.00" value="0.30" step="0.05"
             oninput="updateFactor(this.value)">
      <span style="font-size:10px;color:#666">舵机差异校准: 测出实际速度, 算 速度÷偏差=因子</span>
    </div>
    <div class="slider-row">
      <label>停止中心偏移 <span><span id="trimVal">0</span>μs</span></label>
      <input type="range" id="trimSlider" min="-50" max="50" value="0" step="1"
             oninput="updateTrim(this.value)">
    </div>
    <div class="slider-row">
      <label>死区范围 <span>±<span id="deadVal">0</span>μs</span></label>
      <input type="range" id="deadSlider" min="0" max="30" value="0" step="1"
             oninput="updateDead(this.value)">
    </div>
  </div>

  <div class="btn-row">
    <button class="btn-stop" onclick="doStop()">停止</button>
  </div>

  <div class="msg" id="msg"></div>
</div>

<script>
let speedFactor = 0.3;
let currentDev = 0;
let currentPwm = 1500;
let moving = false;
let homeTriggered = false;
let stopTrim = 0;
let deadBand = 0;
let center = 1500;

function devToSpeed(d) { return Math.round(Math.abs(d) * speedFactor); }
function speedToDev(s) { return Math.round(s / speedFactor); }

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

  currentDev = data.deviation || 0;
  currentPwm = data.pulse || 1500;
  moving = data.moving;
  homeTriggered = data.home_triggered;
  speedFactor = data.speed_factor || 0.3;
  stopTrim = data.stop_trim || 0;
  deadBand = data.dead_band || 0;
  center = data.center || 1500;

  let spd = devToSpeed(currentDev);
  let spdEl = document.getElementById('speedBig');
  if (!moving || Math.abs(currentDev) <= deadBand) {
    spdEl.textContent = '0 °/s';
    spdEl.className = 'speed-big stop';
  } else if (currentDev > 0) {
    spdEl.textContent = spd + ' °/s CCW';
    spdEl.className = 'speed-big ccw';
  } else {
    spdEl.textContent = spd + ' °/s CW';
    spdEl.className = 'speed-big cw';
  }

  document.getElementById('pwmText').textContent = currentPwm;
  document.getElementById('devText').textContent = (currentDev >= 0 ? '+' : '') + currentDev;

  let hl = document.getElementById('homeLed');
  let hc = document.getElementById('chipHome');
  hl.className = 'home-led' + (homeTriggered ? ' on' : '');
  hc.className = 'chip' + (homeTriggered ? ' ok' : '');

  document.getElementById('chipDead').textContent = '死区: ±' + deadBand + 'μs';

  let maxSpd = Math.round(300 * speedFactor);
  document.getElementById('maxSpeed').textContent = maxSpd;
  document.getElementById('factorText').textContent = speedFactor.toFixed(2);

  let ss = document.getElementById('speedSlider');
  if (Math.abs(ss.value - currentDev) > 5) ss.value = currentDev;

  document.getElementById('factorSlider').value = speedFactor;
  document.getElementById('factorVal').textContent = speedFactor.toFixed(2);
  document.getElementById('trimSlider').value = stopTrim;
  document.getElementById('trimVal').textContent = (stopTrim >= 0 ? '+' : '') + stopTrim;
  document.getElementById('deadSlider').value = deadBand;
  document.getElementById('deadVal').textContent = deadBand;
}

function onSlider(v) {
  let dev = parseInt(v);
  document.getElementById('msg').textContent = '偏差: ' + (dev >= 0 ? '+' : '') + dev
    + 'μs ≈ ' + devToSpeed(dev) + '°/s';
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

async function updateFactor(v) {
  speedFactor = parseFloat(v);
  document.getElementById('factorVal').textContent = speedFactor.toFixed(2);
  await api('POST', '/api/servo/speed_factor', { speed_factor: speedFactor });
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
                cJSON_AddNumberToObject(r, "speed_factor", dispenser_->GetSpeedFactor());
                cJSON_AddNumberToObject(r, "speed_estimate", dispenser_->GetSpeedEstimate());
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
                cJSON_AddNumberToObject(r, "speed_estimate", dispenser_->GetSpeedEstimate());
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

        // POST /api/servo/speed_factor
        RegisterUri("/api/servo/speed_factor", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[64];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';
                cJSON* json = cJSON_Parse(buf);
                if (!json) return ESP_FAIL;
                cJSON* item = cJSON_GetObjectItem(json, "speed_factor");
                if (cJSON_IsNumber(item))
                    dispenser_->SetSpeedFactor((float)item->valuedouble);
                cJSON_Delete(json);

                cJSON* r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "speed_factor", dispenser_->GetSpeedFactor());
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
