#ifndef SERVO_DEBUG_SERVER_H
#define SERVO_DEBUG_SERVER_H

#include <esp_http_server.h>
#include <esp_log.h>
#include <cJSON.h>
#include "servo_controller.h"

#define TAG_SDS "ServoDebug"

// 舵机调试网页（内嵌 HTML）
static const char SERVO_DEBUG_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>舵机调试 - AI 智能药盒 Pro</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { font-family: -apple-system, sans-serif; background: #1a1a2e;
         color: #eee; min-height: 100vh; display: flex;
         justify-content: center; align-items: center; }
  .card { background: #16213e; border-radius: 16px; padding: 24px;
          max-width: 420px; width: 100%; margin: 16px;
          box-shadow: 0 8px 32px rgba(0,0,0,.4); }
  h2 { text-align: center; margin-bottom: 8px; color: #e94560; }
  .sub { text-align: center; font-size: 14px; color: #888; margin-bottom: 20px; }

  .angle-display { text-align: center; margin: 16px 0; }
  .angle-value { font-size: 64px; font-weight: bold; color: #0f3460;
                 background: #e94560; display: inline-block; padding: 8px 24px;
                 border-radius: 12px; min-width: 160px; }
  .slot-label { font-size: 14px; color: #ccc; margin-top: 4px; }

  input[type=range] { width: 100%; height: 40px; -webkit-appearance: none;
                      background: #0f3460; border-radius: 10px; outline: none; }
  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none;
      width: 36px; height: 36px; background: #e94560; border-radius: 50%;
      cursor: pointer; border: 2px solid #fff; }

  .btn-row { display: flex; gap: 8px; justify-content: center; margin: 12px 0; flex-wrap: wrap; }
  button { border: none; border-radius: 10px; padding: 10px 18px; font-size: 15px;
           cursor: pointer; font-weight: bold; transition: .15s; color: #fff; }
  button:active { transform: scale(.95); }
  .btn-step { background: #0f3460; }
  .btn-slot { background: #533483; }
  .btn-zero { background: #e94560; font-size: 18px; padding: 12px 32px; }
  .btn-home { background: #0f3460; }

  .slot-grid { display: grid; grid-template-columns: repeat(4,1fr); gap: 8px; margin: 16px 0; }
  .status { text-align: center; font-size: 13px; color: #666; margin-top: 12px; }
  .warning { background: #332200; color: #ffaa00; padding: 10px 14px; border-radius: 8px;
             font-size: 13px; margin-bottom: 16px; text-align: center; }
</style>
</head>
<body>
<div class="card">
  <h2>舵机校准调试</h2>
  <div class="sub">安装舵机前先将角度归零</div>

  <div class="warning">
    <strong>安装步骤</strong><br>
    1. 点"归零"使舵机转到 0<br>
    2. 将转盘槽位 0（logo 面）对准取药口装入<br>
    3. 用槽位按钮验证各槽位对准情况
  </div>

  <div class="angle-display">
    <div class="angle-value" id="angleText">--</div>
    <div class="slot-label">角度 / 槽位 <span id="slotText">--</span></div>
  </div>

  <input type="range" id="slider" min="0" max="180" value="0"
         oninput="setAngle(this.value)">

  <div class="btn-row">
    <button class="btn-step" onclick="adjust(-10)">-10</button>
    <button class="btn-step" onclick="adjust(-5)">-5</button>
    <button class="btn-step" onclick="adjust(-1)">-1</button>
    <button class="btn-step" onclick="adjust(1)">+1</button>
    <button class="btn-step" onclick="adjust(5)">+5</button>
    <button class="btn-step" onclick="adjust(10)">+10</button>
  </div>

  <div class="btn-row">
    <button class="btn-zero" onclick="goZero()">归零 (安装位)</button>
  </div>

  <div class="slot-grid">
    <button class="btn-slot" style="background:#e94560" onclick="goSlot(0)">槽0 LOGO</button>
    <button class="btn-slot" onclick="goSlot(1)">槽1 药品</button>
    <button class="btn-slot" onclick="goSlot(2)">槽2 药品</button>
    <button class="btn-slot" onclick="goSlot(3)">槽3 药品</button>
    <button class="btn-slot" onclick="goSlot(4)">槽4 药品</button>
    <button class="btn-slot" onclick="goSlot(5)">槽5 药品</button>
    <button class="btn-slot" onclick="goSlot(6)">槽6 药品</button>
    <button class="btn-slot" onclick="goSlot(7)">槽7 药品</button>
  </div>

  <div class="status" id="status"></div>
</div>

<script>
let currentAngle = 0;

async function api(method, path, body) {
  try {
    let opts = { method };
    if (body) {
      opts.headers = { 'Content-Type': 'application/json' };
      opts.body = JSON.stringify(body);
    }
    let r = await fetch(path, opts);
    let data = await r.json();
    return data;
  } catch(e) {
    document.getElementById('status').textContent = '连接失败: ' + e.message;
    return null;
  }
}

async function refresh() {
  let data = await api('GET', '/api/servo/angle');
  if (data) {
    currentAngle = data.angle;
    document.getElementById('angleText').textContent = data.angle + '';
    document.getElementById('slotText').textContent = data.slot;
    document.getElementById('slider').value = data.angle;
    document.getElementById('status').textContent = '';
  }
}

async function setAngle(v) {
  let data = await api('POST', '/api/servo/angle', { angle: parseInt(v) });
  if (data) {
    currentAngle = data.angle;
    document.getElementById('angleText').textContent = data.angle + '';
    document.getElementById('slotText').textContent = data.slot;
    document.getElementById('status').textContent = '已设置 ' + data.angle + '';
  }
}

async function adjust(d) { await setAngle(currentAngle + d); }

async function goZero() {
  document.getElementById('status').textContent = '归零中...';
  let data = await api('POST', '/api/servo/zero');
  if (data) {
    document.getElementById('angleText').textContent = '0';
    document.getElementById('slotText').textContent = '0 (LOGO)';
    document.getElementById('slider').value = 0;
    currentAngle = 0;
    document.getElementById('status').textContent = '已归零，可以安装舵机';
  }
}

async function goSlot(n) {
  document.getElementById('status').textContent = '旋转中...';
  let data = await api('POST', '/api/servo/slot', { slot: n });
  if (data) {
    document.getElementById('angleText').textContent = data.angle + '';
    document.getElementById('slotText').textContent = data.slot;
    document.getElementById('slider').value = data.angle;
    currentAngle = data.angle;
    document.getElementById('status').textContent =
      n === 0 ? '槽位0 - LOGO 展示位' : '槽位' + n + ' - 药品槽，角度=' + data.angle + '';
  }
}

refresh();
setInterval(refresh, 2000);
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

        // GET /servo-debug
        RegisterUri("/servo-debug", HTTP_GET,
            [](httpd_req_t* req) {
                httpd_resp_set_type(req, "text/html; charset=utf-8");
                httpd_resp_send(req, SERVO_DEBUG_HTML,
                                HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            });

        // GET /api/servo/angle
        RegisterUri("/api/servo/angle", HTTP_GET,
            [this](httpd_req_t* req) {
                cJSON* root = cJSON_CreateObject();
                cJSON_AddNumberToObject(root, "angle",
                    dispenser_->GetAngle());
                cJSON_AddNumberToObject(root, "slot",
                    dispenser_->CurrentSlot());
                char* js = cJSON_PrintUnformatted(root);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(root);
                return ESP_OK;
            });

        // POST /api/servo/angle
        RegisterUri("/api/servo/angle", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[64];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';

                cJSON* json = cJSON_Parse(buf);
                if (!json) return ESP_FAIL;

                cJSON* angle_json = cJSON_GetObjectItem(json, "angle");
                if (cJSON_IsNumber(angle_json)) {
                    dispenser_->SetAngle(angle_json->valueint);
                }
                cJSON_Delete(json);

                cJSON* resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "angle",
                    dispenser_->GetAngle());
                cJSON_AddNumberToObject(resp, "slot",
                    dispenser_->CurrentSlot());
                char* js = cJSON_PrintUnformatted(resp);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(resp);
                return ESP_OK;
            });

        // POST /api/servo/zero
        RegisterUri("/api/servo/zero", HTTP_POST,
            [this](httpd_req_t* req) {
                dispenser_->Zero();
                cJSON* resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "angle", 0);
                cJSON_AddNumberToObject(resp, "slot", 0);
                char* js = cJSON_PrintUnformatted(resp);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(resp);
                return ESP_OK;
            });

        // POST /api/servo/slot
        RegisterUri("/api/servo/slot", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[64];
                int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';

                cJSON* json = cJSON_Parse(buf);
                if (!json) return ESP_FAIL;

                cJSON* slot_json = cJSON_GetObjectItem(json, "slot");
                if (cJSON_IsNumber(slot_json)) {
                    dispenser_->GoToSlot(slot_json->valueint);
                }
                cJSON_Delete(json);

                cJSON* resp = cJSON_CreateObject();
                cJSON_AddNumberToObject(resp, "angle",
                    dispenser_->GetAngle());
                cJSON_AddNumberToObject(resp, "slot",
                    dispenser_->CurrentSlot());
                char* js = cJSON_PrintUnformatted(resp);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(resp);
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
