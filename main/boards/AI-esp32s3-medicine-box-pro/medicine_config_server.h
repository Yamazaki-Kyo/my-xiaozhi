#ifndef MEDICINE_CONFIG_SERVER_H
#define MEDICINE_CONFIG_SERVER_H

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <vector>
#include <map>
#include <mutex>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <memory>
#include <algorithm>

#include "pillbox_turntable.h"

#define TAG_MCS "MedConfig"

// ===================== 转盘调试 Web 页面 =====================
static const char TURNTABLE_DEBUG_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>转盘调试</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,system-ui,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:16px}
h2{font-size:1.3rem;margin-bottom:8px;color:#e0e0e0}
.current-slot{display:flex;align-items:center;gap:12px;margin:12px 0 20px}
.slot-badge{width:64px;height:64px;border-radius:50%;background:#16213e;border:3px solid #0f3460;display:flex;align-items:center;justify-content:center;font-size:2rem;font-weight:bold;color:#e94560;transition:all .3s}
.slot-badge.moving{animation:pulse .5s infinite;border-color:#e94560}
@keyframes pulse{0%,100%{box-shadow:0 0 8px #e9456040}50%{box-shadow:0 0 24px #e94560aa}}
.status-tag{font-size:.75rem;padding:3px 10px;border-radius:10px;font-weight:600}
.status-tag.ready{background:#0f3460;color:#53d769}
.status-tag.busy{background:#e9456040;color:#e94560}
.status-tag.nope{background:#333;color:#999}
.slot-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;max-width:360px;width:100%;margin-bottom:16px}
.slot-btn{aspect-ratio:1;border-radius:16px;border:2px solid #0f3460;background:#16213e;color:#ccc;font-size:1.1rem;font-weight:600;cursor:pointer;transition:all .15s;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;-webkit-tap-highlight-color:transparent}
.slot-btn:active{transform:scale(.94);background:#e94560;border-color:#e94560;color:#fff}
.slot-btn.active{border-color:#53d769;background:#53d76920;color:#53d769}
.slot-btn small{font-size:.65rem;opacity:.6}
.home-btn{width:100%;max-width:360px;padding:14px;border-radius:12px;border:none;background:#e94560;color:#fff;font-size:1rem;font-weight:700;cursor:pointer;margin-bottom:20px;transition:all .15s}
.home-btn:active{transform:scale(.96);opacity:.8}
.home-btn:disabled{background:#444;color:#888;cursor:not-allowed}
.info-row{display:flex;justify-content:space-between;max-width:360px;width:100%;font-size:.75rem;color:#888;padding:0 4px}
</style>
</head>
<body>
<h2>药盘转盘调试</h2>
<div class="current-slot">
  <div class="slot-badge" id="slotDisplay">-</div>
  <div>
    <div style="font-size:.85rem;margin-bottom:4px">当前槽位</div>
    <span class="status-tag nope" id="statusTag">未就绪</span>
  </div>
</div>
<div class="slot-grid" id="slotGrid"></div>
<button class="home-btn" id="homeBtn" onclick="goHome()">归零 (槽0)</button>
<div class="info-row"><span id="movingHint"></span><span id="updateTime"></span></div>
<div id="errorMsg" style="display:none;max-width:360px;width:100%;margin-top:8px;padding:10px 14px;border-radius:8px;background:#e9456040;color:#e94560;font-size:.85rem;text-align:center"></div>

<script>
var currentSlot = -1;
var isMoving = false;
var isReady = false;

function showError(msg){
  var e=document.getElementById('errorMsg');
  e.textContent=msg;e.style.display='block';
  clearTimeout(e._t);e._t=setTimeout(function(){e.style.display='none';},3000);
}

function buildGrid(){
  var g=document.getElementById('slotGrid');
  g.innerHTML='';
  for(var i=1;i<=7;i++){
    var b=document.createElement('button');
    b.className='slot-btn';
    b.innerHTML=i+'<small>槽</small>';
    b.onclick=(function(s){return function(){goToSlot(s);};})(i);
    b.id='btn'+i;
    g.appendChild(b);
  }
}

function updateUI(){
  var badge=document.getElementById('slotDisplay');
  badge.textContent=isReady?currentSlot:'-';
  badge.className='slot-badge'+(isMoving?' moving':'');

  var tag=document.getElementById('statusTag');
  if(!isReady){tag.textContent='未就绪';tag.className='status-tag nope';}
  else if(isMoving){tag.textContent='转动中...';tag.className='status-tag busy';}
  else{tag.textContent='就绪';tag.className='status-tag ready';}

  for(var i=1;i<=7;i++){
    var b=document.getElementById('btn'+i);
    if(b)b.classList.toggle('active',isReady&&!isMoving&&currentSlot===i);
  }

  document.getElementById('homeBtn').disabled=!isReady||isMoving||currentSlot===0;
  document.getElementById('movingHint').textContent=isMoving?'转盘正在旋转...':(isReady?'点击按钮旋转到指定槽位':'等待转盘就绪');
  document.getElementById('updateTime').textContent=new Date().toLocaleTimeString();
}

function fetchStatus(){
  fetch('/api/turntable/status')
    .then(function(r){return r.json();})
    .then(function(d){
      currentSlot=d.slot;isReady=d.ready;
      if(isReady && !isNaN(d.slot) && d.slot>=0) isMoving=false;
      updateUI();
    })
    .catch(function(e){showError('无法连接设备');});
}

function goToSlot(n){
  fetch('/api/turntable/go',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:n})})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.ok){currentSlot=-1;isMoving=true;updateUI();}
      else{showError(d.error||'请求失败');}
    })
    .catch(function(e){showError('请求失败,请检查连接');});
}

function goHome(){
  fetch('/api/turntable/home',{method:'POST'})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.ok){currentSlot=-1;isMoving=true;updateUI();}
      else{showError(d.error||'归零失败');}
    })
    .catch(function(e){showError('请求失败,请检查连接');});
}

buildGrid();
fetchStatus();
setInterval(fetchStatus,500);
</script>
</body>
</html>
)rawliteral";

static const char MEDICINE_CONFIG_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>智能药盒 · 管理系统</title>
<style>
/* ===== CSS Reset & Variables ===== */
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --primary:#2b8fd1;--primary-dark:#1a6fa8;--primary-light:#e8f4fd;
  --accent:#6daa7e;--accent-dark:#4a8a5e;--accent-light:#e8f5e9;
  --danger:#e07b7b;--warning:#ff9800;
  --bg:#f8fafb;--surface:#ffffff;--border:#d6e5f0;--line-light:#e8f0f6;
  --text:#2c3e50;--text-light:#6b8299;--text-lighter:#9aafc4;
  --radius:12px;--radius-lg:16px;--radius-sm:10px;
  --shadow:0 2px 12px rgba(0,0,0,.06);--shadow-lg:0 4px 20px rgba(0,0,0,.08);
  --nav-h:64px;--bottom-nav-h:72px;
}
html{font-size:15px;height:100%;-webkit-tap-highlight-color:transparent}
body{
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei","Helvetica Neue",sans-serif;
  background:var(--bg);color:var(--text);line-height:1.5;min-height:100vh;
  display:flex;flex-direction:column;overflow-x:hidden;
}

/* ===== Top Bar ===== */
.topbar{
  height:56px;background:var(--surface);border-bottom:1px solid var(--border);
  display:flex;align-items:center;justify-content:space-between;padding:0 18px;
  flex-shrink:0;z-index:20;
}
.topbar .logo{font-size:1.15rem;font-weight:700;color:var(--primary-dark);letter-spacing:.3px}
.topbar .logo span{margin-right:6px}

/* ===== Main Content Area ===== */
.main{flex:1;overflow-y:auto;overflow-x:hidden;min-height:0;padding:16px;padding-bottom:calc(var(--bottom-nav-h) + 86px);-webkit-overflow-scrolling:touch}

/* ===== View Switching ===== */
.view{display:none;flex-direction:column;gap:16px;animation:fadeIn .25s ease}
.view.active{display:flex}
@keyframes fadeIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:translateY(0)}}

/* ===== Module Card ===== */
.module-header{margin-bottom:10px}
.module-header h2{font-size:1.25rem;font-weight:700;color:var(--text)}
.module-header p{font-size:.82rem;color:var(--text-light);margin-top:3px}
.card{
  background:var(--surface);border:1px solid var(--border);border-radius:var(--radius-lg);
  box-shadow:var(--shadow);overflow:hidden;
}
.card-pad{padding:16px}
.card-title{
  font-size:.95rem;font-weight:600;color:var(--text);padding:14px 16px;
  border-bottom:1px solid var(--line-light);display:flex;align-items:center;gap:8px;
}
.card-title .icon{font-size:1.1rem}

/* ===== Master Switch ===== */
.switch-wrap{display:flex;align-items:center;justify-content:space-between;padding:4px 2px}
.switch-label{font-size:1rem;font-weight:500}
.toggle{position:relative;width:52px;height:30px;cursor:pointer}
.toggle input{opacity:0;width:0;height:0}
.toggle .slider{
  position:absolute;inset:0;background:#ccc;border-radius:30px;transition:.3s;
}
.toggle input:checked+.slider{background:var(--accent)}
.toggle .slider::before{
  content:"";position:absolute;width:24px;height:24px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s;
}
.toggle input:checked+.slider::before{transform:translateX(22px)}

/* ===== Slot Grid ===== */
.slot-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
@media(min-width:640px){.slot-grid{grid-template-columns:repeat(4,1fr)}}
@media(min-width:1024px){.slot-grid{grid-template-columns:repeat(7,1fr)}}
.slot-card{
  background:var(--surface);border:1.5px solid var(--border);border-radius:var(--radius);
  padding:14px;cursor:pointer;transition:transform .15s,box-shadow .15s,border-color .15s;position:relative;user-select:none;min-height:110px;display:flex;flex-direction:column;
}
.slot-card:active{transform:scale(.97)}
.slot-card:hover{border-color:var(--primary)}
.slot-card.selected{border-color:var(--accent);box-shadow:0 0 0 4px rgba(109,170,126,.12)}
.slot-num{
  display:inline-flex;align-self:flex-start;background:var(--accent-light);color:var(--accent-dark);
  font-weight:700;font-size:.75rem;padding:3px 10px;border-radius:20px;margin-bottom:8px;
}
.slot-num.empty{background:#f0ece6;color:var(--text-lighter)}
.slot-drug{font-size:.95rem;font-weight:600;margin-bottom:4px;word-break:break-all}
.slot-drug.unset{color:#c5c0b8;font-weight:400}
.slot-info{font-size:.75rem;color:var(--text-light);margin-top:auto}
.slot-badge{position:absolute;top:12px;right:12px;width:8px;height:8px;border-radius:50%;background:var(--accent)}
.slot-badge.off{background:#ddd}

/* ===== Timeline ===== */
.timeline{position:relative;padding-left:22px}
.timeline::before{
  content:"";position:absolute;left:6px;top:6px;bottom:6px;width:2px;background:var(--border);border-radius:2px;
}
.timeline-item{
  position:relative;margin-bottom:14px;padding:12px 14px;background:var(--surface);
  border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);
}
.timeline-item::before{
  content:"";position:absolute;left:-20px;top:18px;width:10px;height:10px;border-radius:50%;
  background:var(--accent);border:2px solid var(--surface);box-shadow:0 0 0 2px var(--accent-light);
}
.timeline-item .tl-time{font-size:.82rem;color:var(--accent-dark);font-weight:700;margin-bottom:2px}
.timeline-item .tl-drug{font-size:.95rem;font-weight:600}
.timeline-item .tl-meta{font-size:.78rem;color:var(--text-light);margin-top:2px}
.empty-state{text-align:center;padding:28px;color:var(--text-lighter);font-size:.88rem}

/* ===== Calendar Styles ===== */
.cal-header{
  display:flex;align-items:center;justify-content:space-between;padding:10px 0;margin-bottom:4px;
}
.cal-header .cal-title{font-size:1.2rem;font-weight:700;color:var(--text)}
.cal-header button{
  width:38px;height:38px;border:1.5px solid var(--border);border-radius:50%;background:var(--surface);
  font-size:1.1rem;cursor:pointer;display:flex;align-items:center;justify-content:center;color:var(--text);transition:all .15s;
}
.cal-header button:active{background:var(--primary-light);border-color:var(--primary)}
.cal-quick-actions{display:flex;gap:8px;margin-bottom:8px;flex-wrap:wrap}
.cal-quick-actions button{
  padding:7px 14px;border:1px solid var(--border);border-radius:20px;background:var(--surface);
  font-size:.78rem;cursor:pointer;color:var(--text-light);font-family:inherit;transition:all .15s;
}
.cal-quick-actions button:active{background:var(--accent-light);border-color:var(--accent);color:var(--accent-dark)}
.cal-quick-actions .btn-fill-month{background:var(--primary-light);border-color:var(--primary);color:var(--primary-dark);font-weight:600}
.cal-quick-actions .btn-fill-week{background:var(--accent-light);border-color:var(--accent);color:var(--accent-dark);font-weight:600}
.calendar-grid{display:grid;grid-template-columns:repeat(7,1fr);gap:3px}
.calendar-grid .day-header{
  text-align:center;font-size:.78rem;font-weight:600;color:var(--text-light);padding:8px 0;
}
.calendar-grid .day-header.weekend{color:var(--danger)}
.cal-cell{
  border:1px solid var(--border);border-radius:var(--radius-sm);
  padding:4px;cursor:pointer;transition:all .15s;display:flex;flex-direction:column;
  align-items:stretch;background:var(--surface);min-width:0;overflow:hidden;min-height:64px;
}
.cal-cell:active{transform:scale(.95);background:var(--primary-light)}
.cal-cell:hover{border-color:var(--primary)}
.cal-cell.today{border:2px solid var(--primary);background:var(--primary-light)}
.cal-cell.selected{border:2px solid var(--accent);box-shadow:0 0 0 3px rgba(109,170,126,.15)}
.cal-cell.other-month{opacity:.35;background:var(--bg)}
.cal-cell .day-num{
  font-size:.85rem;font-weight:600;color:var(--text);line-height:1.2;
}
.cal-cell.today .day-num{color:var(--primary-dark)}
.cal-cell.other-month .day-num{color:var(--text-lighter)}
.cal-cell .day-dots{
  display:flex;flex-wrap:wrap;gap:2px;justify-content:center;margin-top:2px;
}
.cal-cell .day-dot{
  width:7px;height:7px;border-radius:50%;flex-shrink:0;
}
.dot-s1{background:#e07b7b}.dot-s2{background:#6daa7e}.dot-s3{background:#2b8fd1}
.dot-s4{background:#ff9800}.dot-s5{background:#9b59b6}.dot-s6{background:#1abc9c}
.dot-s7{background:#e67e22}

/* Calendar event tags in day cells */
.cal-ev-tag{
  display:flex;align-items:center;gap:2px;padding:1px 3px;margin-top:1px;
  border-radius:2px;background:var(--bg);font-size:.65rem;overflow:hidden;white-space:nowrap;
  border-left:3px solid transparent;line-height:1.3;
}
.cal-ev-tag .cal-ev-time{font-weight:600;flex-shrink:0;color:var(--text);min-width:32px}
.cal-ev-tag .cal-ev-text{overflow:hidden;text-overflow:ellipsis;color:var(--text-light)}
.cal-ev-tag.dot-s1{border-left-color:#e07b7b}.cal-ev-tag.dot-s2{border-left-color:#6daa7e}
.cal-ev-tag.dot-s3{border-left-color:#2b8fd1}.cal-ev-tag.dot-s4{border-left-color:#ff9800}
.cal-ev-tag.dot-s5{border-left-color:#9b59b6}.cal-ev-tag.dot-s6{border-left-color:#1abc9c}
.cal-ev-tag.dot-s7{border-left-color:#e67e22}
.cal-ev-more{font-size:.62rem;color:var(--text-lighter);text-align:center;margin-top:1px;line-height:1.2}

/* Day events list in editor */
.day-event-item{
  display:flex;align-items:center;gap:10px;padding:10px 12px;margin-bottom:6px;
  background:var(--bg);border-radius:var(--radius-sm);border:1px solid var(--line-light);font-size:.85rem;
}
.day-event-item .ev-time{font-weight:700;color:var(--primary-dark);min-width:44px}
.day-event-item .ev-name{flex:1;font-weight:500}
.day-event-item .ev-dose{color:var(--text-light);font-size:.78rem}
.day-event-item .ev-del{
  width:28px;height:28px;border:none;border-radius:50%;background:var(--danger);color:#fff;
  font-size:.85rem;cursor:pointer;display:flex;align-items:center;justify-content:center;flex-shrink:0;
}

/* ===== Bottom Sheet Editor ===== */
.editor-overlay{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,.35);z-index:100;
  justify-content:center;align-items:flex-end;backdrop-filter:blur(2px);
}
.editor-overlay.active{display:flex}
.editor-panel{
  background:var(--surface);border-radius:24px 24px 0 0;width:100%;max-width:560px;
  max-height:85vh;overflow-y:auto;overflow-x:hidden;-webkit-overflow-scrolling:touch;padding:20px 16px 32px;animation:slideUp .25s ease-out;
}
@media(min-width:768px){
  .editor-overlay{align-items:center}
  .editor-panel{border-radius:var(--radius-lg);max-height:90vh;padding:24px}
}
@keyframes slideUp{from{transform:translateY(100%)}to{transform:translateY(0)}}
.editor-panel h3{font-size:1.2rem;margin-bottom:18px;text-align:center;color:var(--text)}
.form-group{margin-bottom:14px}
.form-group label{display:block;font-size:.85rem;font-weight:500;color:var(--text-light);margin-bottom:5px}
.form-group input,.form-group select{
  width:100%;border:1.5px solid var(--border);border-radius:var(--radius-sm);padding:11px 12px;
  font-size:1rem;outline:none;transition:border-color .2s;font-family:inherit;background:var(--surface);
}
.form-group input:focus,.form-group select:focus{border-color:var(--accent)}
.form-group select{-webkit-appearance:none;appearance:none;background-image:url("data:image/svg+xml,%3Csvg width='12' height='8' viewBox='0 0 12 8' xmlns='http://www.w3.org/2000/svg'%3E%3Cpath d='M1 1l5 5 5-5' stroke='%239aafc4' stroke-width='1.5' fill='none'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center;padding-right:32px}
.dose-stepper{display:flex;align-items:center;gap:10px}
.dose-stepper button{
  width:44px;height:44px;border:1.5px solid var(--border);border-radius:50%;background:var(--surface);
  font-size:1.3rem;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:all .15s;color:var(--text);
}
.dose-stepper button:active{background:var(--accent-light);border-color:var(--accent)}
.dose-stepper .dose-val{font-size:1.4rem;font-weight:700;min-width:36px;text-align:center}
.preset-btn{
  padding:6px 12px;border:1px solid var(--border);border-radius:16px;background:var(--surface);
  font-size:.78rem;cursor:pointer;color:var(--text-light);font-family:inherit;transition:all .15s;min-width:40px;text-align:center;
}
.preset-btn:active{background:var(--primary-light);border-color:var(--primary);color:var(--primary-dark)}
.time-min-input{
  flex:1;text-align:center;padding:8px 4px;border:1px solid var(--border);border-radius:var(--radius-sm);
  font-size:.9rem;font-family:inherit;color:var(--text);background:var(--surface);
  -webkit-appearance:none;appearance:none;
  background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%236b8299' d='M6 8L1 3h10z'/%3E%3C/svg%3E");
  background-repeat:no-repeat;background-position:right 6px center;background-size:12px;padding-right:22px;
}
.editor-actions{display:flex;gap:10px;margin-top:20px}
.editor-actions button{flex:1;padding:13px;border:none;border-radius:var(--radius-sm);font-size:1rem;font-weight:600;cursor:pointer;min-height:48px;transition:background .2s;font-family:inherit}
.btn-clear{background:var(--bg);color:var(--text)}
.btn-clear:hover{background:#e0ddd5}
.btn-save{background:var(--accent);color:#fff}
.btn-save:hover{background:var(--accent-dark)}

/* ===== Global Actions Bar ===== */
.global-actions{
  position:fixed;bottom:var(--bottom-nav-h);left:0;right:0;background:var(--surface);
  padding:10px 14px;box-shadow:0 -4px 20px rgba(0,0,0,.08);display:flex;gap:8px;z-index:40;
  border-top:1px solid var(--border);
}
.global-actions button{
  flex:1;padding:12px 6px;border:none;border-radius:var(--radius-sm);font-size:.9rem;font-weight:600;
  cursor:pointer;min-height:44px;transition:all .2s;font-family:inherit;white-space:nowrap;
}
.btn-secondary{background:var(--bg);color:var(--text);border:1px solid var(--border)}
.btn-secondary:hover{background:#e0ddd5}
.btn-primary{background:var(--accent);color:#fff}
.btn-primary:hover{background:var(--accent-dark)}
@media(min-width:768px){
  .global-actions{max-width:900px;left:50%;transform:translateX(-50%);border-radius:var(--radius) var(--radius) 0 0}
}

/* ===== Status View Cards ===== */
.status-card{
  background:var(--surface);border:1px solid var(--border);border-radius:var(--radius-lg);
  padding:18px;box-shadow:var(--shadow);
}
.status-card h3{
  font-size:.95rem;font-weight:600;margin-bottom:12px;padding-bottom:10px;border-bottom:1px solid var(--line-light);display:flex;align-items:center;gap:6px;
}
.status-row{display:flex;align-items:center;justify-content:space-between;padding:7px 0;font-size:.85rem}
.status-row .label{color:var(--text-light)}
.status-row .value{font-weight:500}
.status-row .value.green{color:var(--accent-dark)}
.status-row .value.blue{color:var(--primary)}
.current-time{font-size:2.2rem;font-weight:700;color:var(--primary);text-align:center;padding:10px 0;letter-spacing:1px}
.current-date{font-size:.85rem;color:var(--text-light);text-align:center;margin-top:-6px;margin-bottom:4px}

/* Reminder list in status */
.reminder-list{list-style:none}
.reminder-item{
  display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid var(--line-light);font-size:.85rem;
}
.reminder-item:last-child{border-bottom:none}
.reminder-item .r-time{font-weight:700;color:var(--primary);min-width:48px;font-size:.9rem}
.reminder-item .r-name{flex:1}
.reminder-item .r-badge{
  font-size:.7rem;padding:3px 10px;border-radius:12px;background:var(--primary-light);color:var(--primary);font-weight:500;white-space:nowrap;
}
.reminder-item .r-badge.upcoming{background:#fff3e0;color:var(--warning)}
.reminder-item .r-badge.done{background:var(--accent-light);color:var(--accent-dark)}

/* ===== Bottom Nav ===== */
.bottom-nav{
  position:fixed;bottom:0;left:0;right:0;height:var(--bottom-nav-h);background:var(--surface);
  border-top:1px solid var(--border);display:flex;z-index:50;box-shadow:0 -2px 10px rgba(0,0,0,.04);
}
.nav-btn{
  flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;
  background:none;border:none;cursor:pointer;color:var(--text-light);font-family:inherit;
  transition:all .2s;font-size:.72rem;position:relative;
}
.nav-btn .nav-icon{font-size:1.4rem;line-height:1}
.nav-btn.active{color:var(--primary);font-weight:600}
.nav-btn.active::after{
  content:"";position:absolute;top:0;left:20%;right:20%;height:3px;background:var(--primary);border-radius:0 0 4px 4px;
}
.nav-btn:hover{background:var(--primary-light)}

/* ===== Toast ===== */
.toast{
  position:fixed;top:16px;left:50%;transform:translateX(-50%) translateY(-20px);
  background:var(--text);color:#fff;padding:10px 22px;border-radius:24px;font-size:.88rem;z-index:300;
  opacity:0;transition:all .3s;pointer-events:none;font-weight:500;box-shadow:var(--shadow-lg);white-space:nowrap;
}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
.toast.success{background:var(--accent-dark)}
.toast.error{background:var(--danger)}

/* ===== Scrollbar ===== */
::-webkit-scrollbar{width:6px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
</style>
</head>
<body>

<!-- Top Bar -->
<div class="topbar">
  <div class="logo"><span>💊</span>智能药盒管理系统</div>
</div>

<!-- Main Content -->
<main class="main" id="mainContainer">

  <!-- ===== VIEW 1: 用药计划 (Monthly Calendar) ===== -->
  <div class="view active" id="viewCalendar">
    <div class="module-header">
      <h2>🗓 用药计划</h2>
      <p>点击日期添加用药提醒 · 支持整月/整周快速填充</p>
    </div>

    <div class="card">
      <div class="card-pad">
        <div class="switch-wrap">
          <span class="switch-label">用药提醒总开关</span>
          <label class="toggle">
            <input type="checkbox" id="masterEnabled" onchange="onMasterToggle()">
            <span class="slider"></span>
          </label>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-pad">
        <div class="cal-header">
          <button onclick="changeMonth(-1)" title="上月">&lt;</button>
          <span class="cal-title" id="calTitle">2026年 5月</span>
          <button onclick="changeMonth(1)" title="下月">&gt;</button>
        </div>
        <div class="cal-quick-actions" id="calQuickActions">
          <button class="btn-fill-month" onclick="quickFill('month')">📅 整月填充</button>
          <button class="btn-fill-week" onclick="quickFill('week')">📆 本周填充</button>
          <button onclick="clearMonth()" style="color:var(--danger)">🗑 清空本月</button>
        </div>
        <div class="calendar-grid" id="calendarGrid"></div>
      </div>
    </div>

    <div class="card">
      <div class="card-title"><span class="icon">📋</span>今日提醒时间轴</div>
      <div class="card-pad">
        <div id="timeline" class="empty-state">暂无提醒计划，请点击日历日期添加</div>
      </div>
    </div>
  </div>

  <!-- ===== VIEW 2: 药槽配置 ===== -->
  <div class="view" id="viewConfig">
    <div class="module-header">
      <h2>💊 药槽配置</h2>
      <p>配置7个药槽的药品名称与单次用量</p>
    </div>

    <div class="card">
      <div class="card-title"><span class="icon">💊</span>药槽列表 (点击编辑)</div>
      <div class="card-pad">
        <div class="slot-grid" id="slotGrid"></div>
      </div>
    </div>
  </div>

  <!-- ===== VIEW 3: 系统状态 ===== -->
  <div class="view" id="viewStatus">
    <div class="module-header">
      <h2>系统状态</h2>
      <p>今日用药提醒状态与设备信息</p>
    </div>

    <div class="status-card">
      <h3><span>⏰</span>当前时间</h3>
      <div class="current-time" id="currentTime">--:--:--</div>
      <div class="current-date" id="currentDate">----年--月--日 星期-</div>
    </div>

    <div class="status-card">
      <h3><span>🔔</span>今日提醒</h3>
      <ul class="reminder-list" id="reminderList">
        <li class="empty-state" style="padding:16px 0">请先在「用药计划」中设定用药时间</li>
      </ul>
    </div>
  </div>

</main>

<datalist id="minList">
  <option value="00"><option value="05"><option value="10"><option value="15">
  <option value="20"><option value="25"><option value="30"><option value="35">
  <option value="40"><option value="45"><option value="50"><option value="55">
</datalist>

<!-- Global Actions -->
<div class="global-actions" id="globalActions">
  <button class="btn-secondary" onclick="loadFromDevice()">从设备读取</button>
  <button class="btn-secondary" onclick="resetAll()">清空</button>
  <button class="btn-primary" onclick="saveToDevice()">保存并下发</button>
</div>

<!-- Bottom Navigation -->
<nav class="bottom-nav">
  <button class="nav-btn active" onclick="switchView('calendar',this)">
    <span class="nav-icon">🗓</span>
    <span>用药计划</span>
  </button>
  <button class="nav-btn" onclick="switchView('config',this)">
    <span class="nav-icon">💊</span>
    <span>药槽配置</span>
  </button>
  <button class="nav-btn" onclick="switchView('status',this)">
    <span class="nav-icon">📊</span>
    <span>系统状态</span>
  </button>
</nav>

<!-- Slot Editor Overlay -->
<div class="editor-overlay" id="slotEditorOverlay">
  <div class="editor-panel" id="slotEditorPanel"></div>
</div>

<!-- Day Event Editor Overlay -->
<div class="editor-overlay" id="dayEditorOverlay">
  <div class="editor-panel" id="dayEditorPanel"></div>
</div>

<!-- Toast -->
<div class="toast" id="toast"></div>

<script>
// ===== Data Model =====
var DEFAULT_PLAN = {
  master_enabled: true,
  slots: (function(){
    var arr = [];
    for(var i=0;i<7;i++){ arr.push({slot:i+1, drug_name:"", shape_label:"", dose_count:1, daily_frequency:0, times:[], remaining:0, expiry_date:""}); }
    return arr;
  })()
};

var SHAPE_OPTIONS = ["","圆形药片","胶囊","oval","三角形","心形","白色","黄色","红色","蓝色","绿色"];
var STORE_PLAN = 'med_plan';
var STORE_CALENDAR = 'med_calendar';

var currentPlan = JSON.parse(JSON.stringify(DEFAULT_PLAN));
var calendarData = {};  // { "2026-5": [ {day:18,slot:2,hour:8,minute:0,dose:1}, ... ] }
var calYear = 2026, calMonth = 5;
var editingSlot = -1;
var selectedDay = -1;

// Dot colors per slot
var SLOT_COLORS = ['dot-s1','dot-s2','dot-s3','dot-s4','dot-s5','dot-s6','dot-s7'];

// ===== Init =====
function init(){
  var now = new Date();
  calYear = now.getFullYear();
  calMonth = now.getMonth() + 1;
  loadCache();
  loadCalendarCache();
  renderAll();
  updateClock();
  checkMedicationTime();
  setInterval(function(){ updateClock(); checkMedicationTime(); }, 30000);

  document.getElementById('slotEditorOverlay').addEventListener('click', function(e){
    if(e.target===e.currentTarget) closeSlotEditor();
  });
  document.getElementById('dayEditorOverlay').addEventListener('click', function(e){
    if(e.target===e.currentTarget) closeDayEditor();
  });
  document.addEventListener('keydown', function(e){ if(e.key==='Escape'){ closeSlotEditor(); closeDayEditor(); } });
}

function loadCache(){
  try{
    var raw = localStorage.getItem(STORE_PLAN);
    if(raw){
      var p = JSON.parse(raw);
      if(p.slots && p.slots.length===7){
        // 迁移旧数据：补全缺失的 expiry_date 字段
        p.slots.forEach(function(s){ if(s.expiry_date===undefined) s.expiry_date=''; });
        currentPlan = p;
      }
    }
  }catch(e){}
}
function saveCache(){ localStorage.setItem(STORE_PLAN, JSON.stringify(currentPlan)); }

function loadCalendarCache(){
  try{
    var raw = localStorage.getItem(STORE_CALENDAR);
    if(raw) calendarData = JSON.parse(raw);
  }catch(e){}
  if(!calendarData || typeof calendarData !== 'object') calendarData = {};
}
function saveCalendarCache(){ localStorage.setItem(STORE_CALENDAR, JSON.stringify(calendarData)); }

// ===== Navigation =====
function switchView(name, btn){
  document.querySelectorAll('.view').forEach(function(v){ v.classList.remove('active'); });
  var viewId = 'view' + name.charAt(0).toUpperCase() + name.slice(1);
  document.getElementById(viewId).classList.add('active');
  document.querySelectorAll('.nav-btn').forEach(function(b){ b.classList.remove('active'); });
  btn.classList.add('active');

  var ga = document.getElementById('globalActions');
  if(name==='calendar'){ ga.style.display='flex'; renderCalendar(); renderTimeline(); }
  else if(name==='config'){ ga.style.display='flex'; }
  else { ga.style.display='none'; updateStatusView(); }
}

// ===== Rendering =====
function renderAll(){
  document.getElementById('masterEnabled').checked = currentPlan.master_enabled;
  renderSlots();
  renderCalendar();
  renderTimeline();
  saveCache();
}

// ===== Slot Config (View 2 - Simplified) =====
function turntableGoSlot(n){
  fetch('/api/turntable/go',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:n})})
    .then(function(r){return r.json();})
    .then(function(d){if(!d.ok) console.warn('goToSlot failed:',d.error);})
    .catch(function(e){console.warn('goToSlot error:',e);});
}

function renderSlots(){
  var grid = document.getElementById('slotGrid');
  if(!grid) return;
  grid.innerHTML = '';
  currentPlan.slots.forEach(function(s,i){
    var card = document.createElement('div');
    card.className = 'slot-card';
    card.onclick = function(){
      if(s.drug_name && s.drug_name.length > 0) turntableGoSlot(s.slot);
      openSlotEditor(i);
    };
    var configured = s.drug_name && s.drug_name.length > 0;
    var info = configured ? s.dose_count+'颗/次 · 剩余 '+(s.remaining||0)+'颗' : '点击配置';
    if(configured && s.expiry_date){
      var daysLeft = calcDaysLeft(s.expiry_date);
      if(daysLeft <= 30) info += ' · <span style="color:'+(daysLeft<0?'#e07b7b':daysLeft<7?'#e07b7b':'#ff9800')+'">';
      info += ' 保质至 '+s.expiry_date;
      if(daysLeft <= 30) info += ' ('+(daysLeft<0?'已过期':daysLeft+'天')+')</span>';
    }
    card.innerHTML =
      '<span class="slot-num '+(configured?'':'empty')+'">槽 '+s.slot+'</span>'+
      '<span class="slot-badge '+(configured?'':'off')+'"></span>'+
      '<div class="slot-drug '+(configured?'':'unset')+'">'+(configured ? escapeHtml(s.drug_name) : '未配置')+'</div>'+
      '<div class="slot-info">'+info+'</div>';
    grid.appendChild(card);
  });
}

function calcDaysLeft(dateStr){
  if(!dateStr) return 999;
  var parts = dateStr.split('-');
  var d = new Date(parseInt(parts[0]), parseInt(parts[1])-1, parseInt(parts[2]));
  var now = new Date(); now.setHours(0,0,0,0);
  return Math.ceil((d.getTime() - now.getTime()) / 86400000);
}

// ===== Slot Editor (simplified: no reminder times) =====
function openSlotEditor(idx){
  editingSlot = idx;
  var s = currentPlan.slots[idx];
  var overlay = document.getElementById('slotEditorOverlay');
  var panel = document.getElementById('slotEditorPanel');

  var shapeOpts = SHAPE_OPTIONS.map(function(o){
    return '<option value="'+escapeAttr(o)+'" '+(o===s.shape_label?'selected':'')+'>'+(o||'无标签')+'</option>';
  }).join('');

  panel.innerHTML =
    '<h3>编辑药槽 '+s.slot+'</h3>'+
    '<div class="form-group">'+
      '<label>药品名称</label>'+
      '<input type="text" id="editDrugName" value="'+escapeAttr(s.drug_name)+'" placeholder="例如：降压药">'+
    '</div>'+
    '<div class="form-group">'+
      '<label>形状/颜色标签</label>'+
      '<select id="editShape">'+shapeOpts+'</select>'+
    '</div>'+
    '<div class="form-group">'+
      '<label>单次服用颗数</label>'+
      '<div class="dose-stepper">'+
        '<button onclick="slotStepDose(-1)">-</button>'+
        '<span class="dose-val" id="doseDisplay">'+s.dose_count+'</span>'+
        '<button onclick="slotStepDose(1)">+</button>'+
      '</div>'+
    '</div>'+
    '<div class="form-group">'+
      '<label>剩余数量（颗）</label>'+
      '<input type="number" id="editRemaining" value="'+(s.remaining||0)+'" min="0" max="999" step="1" inputmode="numeric" style="font-size:1.2rem;text-align:center;font-weight:600">'+
      '<div style="display:flex;flex-wrap:wrap;gap:6px;margin-top:8px">'+
        ['0','5','10','15','20','30','50','100'].map(function(n){ return '<button class="preset-btn" onclick="setRemaining('+n+')">'+n+'</button>'; }).join('')+
      '</div>'+
    '</div>'+
    '<div class="form-group">'+
      '<label>保质期截止日期</label>'+
      '<input type="date" id="editExpiry" value="'+(s.expiry_date||'')+'" style="font-size:1.1rem;text-align:center;padding:10px">'+
    '</div>'+
    '<div class="editor-actions">'+
      '<button class="btn-clear" onclick="clearSlot()">清空此槽</button>'+
      '<button class="btn-save" onclick="closeSlotEditor()">确定</button>'+
    '</div>';
  overlay.classList.add('active');
  document.querySelectorAll('.slot-card').forEach(function(c,i){ c.classList.toggle('selected', i===idx); });
}

function closeSlotEditor(){
  if(editingSlot<0) return;
  var s = currentPlan.slots[editingSlot];
  s.drug_name = document.getElementById('editDrugName').value.trim();
  s.shape_label = document.getElementById('editShape').value;
  s.remaining = parseInt(document.getElementById('editRemaining').value) || 0;
  s.expiry_date = document.getElementById('editExpiry').value || '';
  document.getElementById('slotEditorOverlay').classList.remove('active');
  document.querySelectorAll('.slot-card').forEach(function(c){ c.classList.remove('selected'); });
  editingSlot = -1;
  renderAll();
}

function clearSlot(){
  var s = currentPlan.slots[editingSlot];
  s.drug_name=''; s.shape_label=''; s.dose_count=1; s.daily_frequency=0; s.times=[]; s.remaining=0; s.expiry_date='';
  closeSlotEditor();
}

function slotStepDose(delta){
  var s = currentPlan.slots[editingSlot];
  s.dose_count = Math.max(1, Math.min(9, s.dose_count + delta));
  document.getElementById('doseDisplay').textContent = s.dose_count;
  saveCache();
}

function setRemaining(val){
  document.getElementById('editRemaining').value = val;
}

// ===== Master Toggle =====
function onMasterToggle(){
  currentPlan.master_enabled = document.getElementById('masterEnabled').checked;
  saveCache(); renderTimeline(); updateStatusView();
}

// ===== Calendar (View 1) =====
function getMonthKey(y,m){ return y+'-'+m; }

function getMonthEvents(y,m){
  var key = getMonthKey(y,m);
  return calendarData[key] || [];
}

function setMonthEvents(y,m, events){
  var key = getMonthKey(y,m);
  if(events.length===0){ delete calendarData[key]; }
  else { calendarData[key] = events; }
  saveCalendarCache();
}

function getDayEvents(y,m,d){
  return getMonthEvents(y,m).filter(function(e){ return e.day===d; });
}

function renderCalendar(){
  var grid = document.getElementById('calendarGrid');
  var title = document.getElementById('calTitle');
  if(!grid) return;

  title.textContent = calYear+'年 '+calMonth+'月';

  var headers = ['日','一','二','三','四','五','六'];
  var html = '';
  headers.forEach(function(h,i){
    html += '<div class="day-header'+(i===0||i===6?' weekend':'')+'">'+h+'</div>';
  });

  var firstDay = new Date(calYear, calMonth-1, 1).getDay();
  var daysInMonth = new Date(calYear, calMonth, 0).getDate();
  var daysInPrevMonth = new Date(calYear, calMonth-1, 0).getDate();
  var today = new Date();
  var todayYear = today.getFullYear(), todayMonth = today.getMonth()+1, todayDate = today.getDate();

  var events = getMonthEvents(calYear, calMonth);

  // Previous month tail
  for(var i=firstDay-1; i>=0; i--){
    var d = daysInPrevMonth - i;
    html += renderDayCell(d, true, false, todayYear, todayMonth, todayDate);
  }

  // Current month
  for(var d=1; d<=daysInMonth; d++){
    var isToday = (calYear===todayYear && calMonth===todayMonth && d===todayDate);
    var isSel = (calYear===todayYear && calMonth===todayMonth && d===selectedDay);
    html += renderDayCell(d, false, isToday, todayYear, todayMonth, todayDate, isSel);
  }

  // Next month head (fill remaining cells to complete last row)
  var remaining = 7 - ((firstDay + daysInMonth) % 7);
  if(remaining < 7){
    for(var d=1; d<=remaining; d++){
      html += renderDayCell(d, true, false, todayYear, todayMonth, todayDate);
    }
  }

  // Build day events map
  var dayEvtMap = {};
  events.forEach(function(e){
    if(!dayEvtMap[e.day]) dayEvtMap[e.day] = [];
    dayEvtMap[e.day].push(e);
  });

  grid.innerHTML = html;

  // Add event tags to cells
  for(var d=1; d<=daysInMonth; d++){
    var cell = document.getElementById('calCell'+d);
    if(!cell) continue;
    var evts = dayEvtMap[d] || [];
    if(evts.length > 0){
      evts.sort(function(a,b){ return (a.hour*60+a.minute) - (b.hour*60+b.minute); });
      var maxShow = 3;
      for(var i=0; i<evts.length && i<maxShow; i++){
        var e = evts[i];
        var hh = String(e.hour).padStart(2,'0');
        var mm = String(e.minute).padStart(2,'0');
        var slotName = '槽'+e.slot;
        var drugName = '';
        if(currentPlan.slots[e.slot-1] && currentPlan.slots[e.slot-1].drug_name){
          drugName = currentPlan.slots[e.slot-1].drug_name;
        }
        var tag = document.createElement('div');
        tag.className = 'cal-ev-tag '+SLOT_COLORS[e.slot-1];
        tag.innerHTML = '<span class="cal-ev-time">'+hh+':'+mm+'</span><span class="cal-ev-text">'+escapeHtml(slotName+(drugName?' '+drugName:''))+'</span>';
        cell.appendChild(tag);
      }
      if(evts.length > maxShow){
        var more = document.createElement('div');
        more.className = 'cal-ev-more';
        more.textContent = '+'+(evts.length-maxShow)+'项';
        cell.appendChild(more);
      }
    }
  }
}

function renderDayCell(d, isOther, isToday, ty, tm, td, isSel){
  var cls = 'cal-cell';
  if(isOther) cls += ' other-month';
  if(isToday) cls += ' today';
  if(isSel) cls += ' selected';
  return '<div class="'+cls+'" id="calCell'+d+'" onclick="openDayEditor('+d+')"><span class="day-num">'+d+'</span></div>';
}

function changeMonth(delta){
  calMonth += delta;
  if(calMonth > 12){ calMonth = 1; calYear++; }
  if(calMonth < 1){ calMonth = 12; calYear--; }
  selectedDay = -1;
  renderCalendar();
  renderTimeline();
}

function openDayEditor(day){
  selectedDay = day;
  renderCalendar();

  var overlay = document.getElementById('dayEditorOverlay');
  var panel = document.getElementById('dayEditorPanel');
  var events = getDayEvents(calYear, calMonth, day);
  events.sort(function(a,b){ return (a.hour*60+a.minute) - (b.hour*60+b.minute); });

  var dateStr = calYear+'年'+calMonth+'月'+day+'日';
  var weekday = ['日','一','二','三','四','五','六'][new Date(calYear, calMonth-1, day).getDay()];
  dateStr += ' 星期'+weekday;

  // Build existing events list
  var evtHtml = '';
  if(events.length===0){
    evtHtml = '<div class="empty-state" style="padding:12px 0">该日暂无用药安排</div>';
  } else {
    events.forEach(function(e){
      var slotName = '槽'+e.slot;
      var drugName = '';
      if(currentPlan.slots[e.slot-1] && currentPlan.slots[e.slot-1].drug_name){
        drugName = ' '+escapeHtml(currentPlan.slots[e.slot-1].drug_name);
      }
      var hh = String(e.hour).padStart(2,'0');
      var mm = String(e.minute).padStart(2,'0');
      evtHtml +=
        '<div class="day-event-item">'+
          '<span class="ev-time">'+hh+':'+mm+'</span>'+
          '<span class="ev-name">'+slotName+drugName+'</span>'+
          '<span class="ev-dose">'+e.dose+'颗</span>'+
          '<button class="ev-del" onclick="deleteDayEvent('+e.day+','+e.slot+','+e.hour+','+e.minute+')" title="删除">x</button>'+
        '</div>';
    });
  }

  // Build slot options for add form
  var slotOpts = '';
  for(var i=0;i<7;i++){
    var s = currentPlan.slots[i];
    var label = '槽'+(i+1);
    if(s.drug_name) label += ' - '+s.drug_name;
    slotOpts += '<option value="'+(i+1)+'">'+label+'</option>';
  }

  panel.innerHTML =
    '<h3>'+dateStr+'</h3>'+
    '<div style="margin-bottom:10px;font-size:.85rem;color:var(--text-light)">已安排用药</div>'+
    '<div id="dayEventList">'+evtHtml+'</div>'+
    '<hr style="border:none;border-top:1px solid var(--line-light);margin:16px 0">'+
    '<div style="font-size:.85rem;font-weight:600;color:var(--text);margin-bottom:10px">+ 添加用药</div>'+
    '<div class="form-group">'+
      '<label>药槽</label>'+
      '<select id="dayEvSlot">'+slotOpts+'</select>'+
    '</div>'+
    '<div class="form-group">'+
      '<label>时间</label>'+
      '<div style="display:flex;gap:8px;align-items:center">'+
        '<select id="dayEvHour" style="flex:1;text-align:center">'+hourOptions(8)+'</select>'+
        '<span>:</span>'+
        '<input type="text" id="dayEvMin" list="minList" class="time-min-input" value="00" pattern="[0-5]?[0-9]" autocomplete="off">'+
      '</div>'+
    '</div>'+
    '<div class="form-group">'+
      '<label>颗数</label>'+
      '<div class="dose-stepper">'+
        '<button onclick="dayEvStepDose(-1)">-</button>'+
        '<span class="dose-val" id="dayEvDoseDisplay">1</span>'+
        '<button onclick="dayEvStepDose(1)">+</button>'+
      '</div>'+
    '</div>'+
    '<button class="btn-save" style="width:100%;margin-top:8px" onclick="addDayEvent()">添加提醒</button>'+
    '<div class="cal-quick-actions" style="margin-top:16px;justify-content:center">'+
      '<button class="btn-fill-month" onclick="quickFillFromDay('+day+',\'month\')">📅 复制到整月</button>'+
      '<button class="btn-fill-week" onclick="quickFillFromDay('+day+',\'week\')">📆 复制到本周</button>'+
      '<button onclick="clearDay('+day+')" style="color:var(--danger);border-color:var(--danger)">🗑 清空当日</button>'+
    '</div>'+
    '<div class="editor-actions" style="margin-top:8px">'+
      '<button class="btn-clear" onclick="closeDayEditor()">关闭</button>'+
    '</div>';

  overlay.classList.add('active');
}

function closeDayEditor(){
  document.getElementById('dayEditorOverlay').classList.remove('active');
  selectedDay = -1;
  renderCalendar();
  renderTimeline();
}

function addDayEvent(){
  var slot = parseInt(document.getElementById('dayEvSlot').value);
  var hour = parseInt(document.getElementById('dayEvHour').value);
  var minute = parseInt(document.getElementById('dayEvMin').value);
  var dose = parseInt(document.getElementById('dayEvDoseDisplay').textContent);

  if(isNaN(hour)||hour<0||hour>23){ showToast('小时无效 (0-23)','error'); return; }
  if(isNaN(minute)||minute<0||minute>59){ showToast('分钟无效 (0-59)','error'); return; }

  // Check duplicate
  var events = getMonthEvents(calYear, calMonth);
  var dup = events.filter(function(e){ return e.day===selectedDay && e.slot===slot && e.hour===hour && e.minute===minute; });
  if(dup.length>0){
    showToast('该时间已存在相同药槽的提醒','error'); return;
  }

  events.push({day:selectedDay, slot:slot, hour:hour, minute:minute, dose:dose});
  setMonthEvents(calYear, calMonth, events);

  // Refresh the editor
  openDayEditor(selectedDay);
}

function deleteDayEvent(day, slot, hour, minute){
  var events = getMonthEvents(calYear, calMonth);
  events = events.filter(function(e){ return !(e.day===day && e.slot===slot && e.hour===hour && e.minute===minute); });
  setMonthEvents(calYear, calMonth, events);
  openDayEditor(selectedDay);
}

function clearDay(day){
  if(!confirm('确定清空 '+calYear+'/'+calMonth+'/'+day+' 的所有用药安排？')) return;
  var events = getMonthEvents(calYear, calMonth);
  events = events.filter(function(e){ return e.day!==day; });
  setMonthEvents(calYear, calMonth, events);
  openDayEditor(selectedDay);
}

function dayEvStepDose(delta){
  var el = document.getElementById('dayEvDoseDisplay');
  if(!el) return;
  var v = parseInt(el.textContent) + delta;
  el.textContent = Math.max(1, Math.min(9, v));
}

// ===== Quick Fill =====
function quickFillFromDay(day, mode){
  var srcEvents = getDayEvents(calYear, calMonth, day);
  if(srcEvents.length===0){ showToast('源日期无用药安排，请先添加','error'); return; }
  if(!confirm('将 '+calYear+'/'+calMonth+'/'+day+' 的用药安排复制到'+(mode==='month'?'整月':'本周')+'所有日期？')) return;

  var events = getMonthEvents(calYear, calMonth);

  if(mode==='month'){
    var daysInMonth = new Date(calYear, calMonth, 0).getDate();
    // Remove existing events for all days except source
    events = events.filter(function(e){ return e.day===day; });
    // Copy to all days
    for(var d=1; d<=daysInMonth; d++){
      if(d===day) continue;
      srcEvents.forEach(function(e){
        events.push({day:d, slot:e.slot, hour:e.hour, minute:e.minute, dose:e.dose});
      });
    }
  } else {
    // Fill week (Mon-Sun containing the source day)
    var srcDate = new Date(calYear, calMonth-1, day);
    var srcDayOfWeek = srcDate.getDay(); // 0=Sun
    var weekStart = new Date(srcDate);
    weekStart.setDate(srcDate.getDate() - (srcDayOfWeek===0?6:srcDayOfWeek-1)); // Monday
    var weekEnd = new Date(weekStart);
    weekEnd.setDate(weekStart.getDate() + 6); // Sunday

    // Remove existing events for all days in the week except source
    events = events.filter(function(e){
      if(e.day===day) return true;
      var ed = new Date(calYear, calMonth-1, e.day);
      return ed < weekStart || ed > weekEnd;
    });
    // Copy to week days
    for(var d = new Date(weekStart); d <= weekEnd; d.setDate(d.getDate()+1)){
      var wd = d.getDate();
      if(wd === day) continue;
      // Only fill days in current month
      if(d.getMonth()+1 !== calMonth) continue;
      srcEvents.forEach(function(e){
        events.push({day:wd, slot:e.slot, hour:e.hour, minute:e.minute, dose:e.dose});
      });
    }
  }

  setMonthEvents(calYear, calMonth, events);
  closeDayEditor();
  renderCalendar();
  renderTimeline();
  showToast('已复制用药安排到'+(mode==='month'?'整月':'本周'),'success');
}

function quickFill(mode){
  var today = new Date();
  var day = today.getDate();
  // Only fill if current view month matches today
  if(calYear !== today.getFullYear() || calMonth !== today.getMonth()+1){
    showToast('请先切换到当前月份','error'); return;
  }
  quickFillFromDay(day, mode);
}

function clearMonth(){
  if(!confirm('确定清空 '+calYear+'年'+calMonth+'月 的所有用药安排？此操作不可撤销。')) return;
  setMonthEvents(calYear, calMonth, []);
  renderCalendar();
  renderTimeline();
  showToast('已清空本月用药计划');
}

// ===== Timeline (Today's calendar events + slot-based events) =====
function renderTimeline(){
  var tl = document.getElementById('timeline');
  if(!tl) return;
  if(!currentPlan.master_enabled){
    tl.innerHTML = '<div class="empty-state">提醒总开关已关闭</div>'; return;
  }

  var today = new Date();
  var ty = today.getFullYear(), tm = today.getMonth()+1, td = today.getDate();

  // Get calendar events for today
  var calEvents = getDayEvents(ty, tm, td);
  var entries = [];
  calEvents.forEach(function(e){
    var slotName = '槽'+e.slot;
    var drugName = '';
    if(currentPlan.slots[e.slot-1] && currentPlan.slots[e.slot-1].drug_name){
      drugName = currentPlan.slots[e.slot-1].drug_name;
    }
    entries.push({
      hour:e.hour, minute:e.minute, minutes:e.hour*60+e.minute,
      drug:drugName||'未配置药品', slot:e.slot, dose:e.dose
    });
  });

  // Also include slot-based daily repeat events
  currentPlan.slots.forEach(function(s){
    if(!s.drug_name || s.daily_frequency===0) return;
    s.times.forEach(function(t){
      entries.push({
        hour:t.hour||t.h, minute:t.minute||t.m, minutes:(t.hour||t.h)*60+(t.minute||t.m),
        drug:s.drug_name, slot:s.slot, dose:s.dose_count
      });
    });
  });

  if(entries.length===0){
    tl.innerHTML = '<div class="empty-state">今日无用药计划，请点击日历日期添加</div>'; return;
  }
  entries.sort(function(a,b){ return a.minutes-b.minutes; });
  tl.innerHTML = '<div class="timeline">'+entries.map(function(e){
    var hh = String(e.hour).padStart(2,'0'), mm = String(e.minute).padStart(2,'0');
    return '<div class="timeline-item">'+
      '<div class="tl-time">'+hh+':'+mm+'</div>'+
      '<div class="tl-drug">'+escapeHtml(e.drug)+' <span class="tl-meta">[槽'+e.slot+']</span></div>'+
      '<div class="tl-meta">每次 '+e.dose+' 颗</div>'+
    '</div>';
  }).join('')+'</div>';
}

// ===== Status View (View 3) =====
function updateStatusView(){
  updateClock();
  renderReminderList();
  checkMedicationTime();
}

function updateClock(){
  var now = new Date();
  var timeStr = now.toLocaleTimeString('zh-CN',{hour12:false});
  var dayNames = ['日','一','二','三','四','五','六'];
  var dateStr = now.getFullYear()+'年'+String(now.getMonth()+1).padStart(2,'0')+'月'+String(now.getDate()).padStart(2,'0')+'日 星期'+dayNames[now.getDay()];
  var tEl=document.getElementById('currentTime'), dEl=document.getElementById('currentDate');
  if(tEl) tEl.textContent = timeStr;
  if(dEl) dEl.textContent = dateStr;
}

function renderReminderList(){
  var container = document.getElementById('reminderList');
  if(!container) return;
  if(!currentPlan.master_enabled){
    container.innerHTML = '<li class="empty-state" style="padding:16px 0">提醒总开关已关闭</li>'; return;
  }
  var items=[];
  var now = new Date();
  var curMin = now.getHours()*60 + now.getMinutes();
  var ty = now.getFullYear(), tm = now.getMonth()+1, td = now.getDate();

  // Calendar events for today
  var calEvents = getDayEvents(ty, tm, td);
  calEvents.forEach(function(e){
    var mins = e.hour*60 + e.minute;
    var badgeClass='', badgeText='';
    if(curMin >= mins+30){badgeClass='done'; badgeText='已完成';}
    else if(curMin >= mins){badgeClass='upcoming'; badgeText='进行中';}
    else {badgeClass=''; badgeText='待提醒';}
    var drugName = '';
    if(currentPlan.slots[e.slot-1] && currentPlan.slots[e.slot-1].drug_name){
      drugName = currentPlan.slots[e.slot-1].drug_name;
    }
    items.push({
      timeStr: String(e.hour).padStart(2,'0')+':'+String(e.minute).padStart(2,'0'),
      name: drugName||'未配置药品', dose: e.dose, slot: e.slot,
      badgeClass: badgeClass, badgeText: badgeText, minutes: mins
    });
  });

  // Slot-based daily repeat events
  currentPlan.slots.forEach(function(s){
    if(!s.drug_name || s.daily_frequency===0) return;
    s.times.forEach(function(t){
      var mins = (t.hour||t.h)*60 + (t.minute||t.m);
      var badgeClass='', badgeText='';
      if(curMin >= mins+30){badgeClass='done'; badgeText='已完成';}
      else if(curMin >= mins){badgeClass='upcoming'; badgeText='进行中';}
      else {badgeClass=''; badgeText='待提醒';}
      items.push({
        timeStr: String(t.hour||t.h).padStart(2,'0')+':'+String(t.minute||t.m).padStart(2,'0'),
        name: s.drug_name, dose: s.dose_count, slot: s.slot,
        badgeClass: badgeClass, badgeText: badgeText, minutes: mins
      });
    });
  });

  if(items.length===0){
    container.innerHTML = '<li class="empty-state" style="padding:16px 0">今日无用药计划，请在「用药计划」中添加</li>'; return;
  }
  items.sort(function(a,b){ return a.minutes-b.minutes; });
  container.innerHTML = items.map(function(it){
    return '<li class="reminder-item">'+
      '<span class="r-time">'+it.timeStr+'</span>'+
      '<span class="r-name">'+escapeHtml(it.name)+' · 槽'+it.slot+' · '+it.dose+'颗</span>'+
      '<span class="r-badge '+it.badgeClass+'">'+it.badgeText+'</span>'+
    '</li>';
  }).join('');
}

// ===== LED / Medication Time Check =====
function checkMedicationTime(){
  var now = new Date();
  var curMin = now.getHours()*60 + now.getMinutes();
  var ty = now.getFullYear(), tm = now.getMonth()+1, td = now.getDate();
  var isOn = false;

  if(currentPlan.master_enabled){
    // Check calendar events for today
    var calEvents = getDayEvents(ty, tm, td);
    calEvents.forEach(function(e){
      var target = e.hour*60 + e.minute;
      if(curMin >= target && curMin <= target+30) isOn = true;
    });

    // Check slot-based daily repeat events
    currentPlan.slots.forEach(function(s){
      if(!s.drug_name || s.daily_frequency===0) return;
      s.times.forEach(function(t){
        var target = (t.hour||t.h)*60 + (t.minute||t.m);
        if(curMin >= target && curMin <= target+30) isOn = true;
      });
    });
  }

}

// ===== Device Communication =====
function fetchWithTimeout(url, opts, timeoutMs){
  timeoutMs = timeoutMs || 5000;
  if(typeof AbortSignal !== 'undefined' && AbortSignal.timeout){
    opts.signal = AbortSignal.timeout(timeoutMs);
    return fetch(url, opts);
  }
  var ctrl = new AbortController();
  opts.signal = ctrl.signal;
  return new Promise(function(resolve, reject){
    var timer = setTimeout(function(){ ctrl.abort(); reject(new Error('timeout')); }, timeoutMs);
    fetch(url, opts).then(function(r){ clearTimeout(timer); resolve(r); }, function(e){ clearTimeout(timer); reject(e); });
  });
}

function buildPlanJson(){
  var plan = JSON.parse(JSON.stringify(currentPlan));
  plan.slots.forEach(function(s){
    s.times = s.times.map(function(t){ return {hour:t.hour||t.h||8, minute:t.minute||t.m||0}; });
  });
  return JSON.stringify(plan);
}

async function saveToDevice(){
  var planJson = buildPlanJson();
  var calJson = JSON.stringify(calendarData);

  // Save plan
  try{
    var resp = await fetchWithTimeout('/api/medicine/plan', {
      method:'POST', headers:{'Content-Type':'application/json'}, body:planJson
    }, 8000);
    if(!resp.ok){ showToast('保存药槽失败: '+await resp.text(),'error'); return; }
  }catch(e){ showToast('保存药槽失败，请检查连接','error'); return; }

  // Save calendar
  try{
    var resp = await fetchWithTimeout('/api/medicine/calendar', {
      method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({action:'set_all', data:calendarData})
    }, 8000);
    if(resp.ok){
      showToast('全部计划已保存并下发','success');
      // 保存后自动归零转盘
      fetch('/api/turntable/home',{method:'POST'}).catch(function(){});
    } else {
      showToast('保存日历失败: '+await resp.text(),'error');
    }
  }catch(e){ showToast('保存日历失败，请检查连接','error'); }
}

async function loadFromDevice(){
  // Load plan
  try{
    var resp = await fetchWithTimeout('/api/medicine/plan', {}, 8000);
    if(resp.ok){
      var plan = await resp.json();
      if(plan.slots){
        plan.slots.forEach(function(s){
          s.daily_frequency = s.daily_frequency || 0;
          s.times = (s.times||[]).map(function(t){ return {h:t.hour||t.h||8, m:t.minute||t.m||0}; });
          if(!s.dose_count) s.dose_count = 1;
        });
        plan.master_enabled = !!plan.master_enabled;
        currentPlan = plan;
        saveCache();
      }
    }
  }catch(e){}

  // Load calendar
  try{
    var resp = await fetchWithTimeout('/api/medicine/calendar?year='+calYear+'&month='+calMonth, {}, 8000);
    if(resp.ok){
      var data = await resp.json();
      if(data.events){
        calendarData = {};
        data.events.forEach(function(e){
          var key = data.year+'-'+data.month;
          if(!calendarData[key]) calendarData[key] = [];
          calendarData[key].push({day:e.day, slot:e.slot, hour:e.hour, minute:e.minute, dose:e.dose});
        });
        saveCalendarCache();
      }
    }
  }catch(e){}

  renderAll(); updateStatusView();
  showToast('已从设备读取全部计划','success');
}

function resetAll(){
  if(!confirm('确定要清空全部用药计划吗？药槽设置（药品名称、颗数、剩余数量等）将保留。此操作不可撤销。')) return;
  calendarData = {};
  currentPlan.slots.forEach(function(s){
    s.daily_frequency = 0;
    s.times = [];
  });
  saveCache();
  saveCalendarCache();
  renderAll(); updateStatusView();
  showToast('已清空全部用药计划，药槽设置已保留');
}

// ===== Toast =====
var toastTimer;
function showToast(msg, type){
  var el = document.getElementById('toast');
  el.textContent = msg;
  el.className = 'toast ' + (type||'') + ' show';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(function(){ el.classList.remove('show'); }, 2500);
}

// ===== Helpers =====
function escapeHtml(str){
  var div = document.createElement('div');
  div.textContent = String(str||'');
  return div.innerHTML;
}
function escapeAttr(str){
  return String(str||'').replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}
function hourOptions(sel){
  var s='';
  for(var h=0;h<24;h++){ var hh=String(h).padStart(2,'0'); s+='<option value="'+h+'" '+(h===sel?'selected':'')+'>'+hh+'</option>'; }
  return s;
}
function minOptions(sel, step){
  step = step || 1;
  var s='';
  for(var m=0;m<60;m+=step){ var mm=String(m).padStart(2,'0'); s+='<option value="'+m+'" '+(m===sel?'selected':'')+'>'+mm+'</option>'; }
  return s;
}

// ===== Start =====
document.addEventListener('DOMContentLoaded', init);
</script>
</body>
</html>

)rawliteral";

struct MedTime {
    int hour;
    int minute;
};

struct MedSlot {
    int slot;
    std::string drug_name;
    std::string shape_label;
    int dose_count;
    int daily_frequency;
    MedTime times[5];
    int remaining;
    std::string expiry_date;  // 保质期 "YYYY-MM-DD"
};

struct MedPlan {
    bool master_enabled;
    MedSlot slots[7];
};

struct CalendarEvent {
    int day;       // 1-31
    int slot;      // 1-7
    int hour;      // 0-23
    int minute;    // 0-59
    int dose;      // 1-9
};

class MedicineConfigServer {
public:
    MedicineConfigServer()
        : server_(nullptr) {
        LoadFromNVS();
    }

    void RegisterHttpHandlers(httpd_handle_t server) {
        server_ = server;
        RegisterHandlers();
    }

    void SetEventCallback(std::function<void(int slot, int dose, const char* msg)> cb) {
        on_event_fired_ = std::move(cb);
    }

    void SetTurntable(PillBoxTurntable* t) { turntable_ = t; }

    /// @brief 检查当前时间是否匹配用药计划，匹配时触发回调。返回提醒消息（空串=无提醒）。
    std::string CheckAndNotify() {
        if (!plan_.master_enabled) return {};

        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        if (tm_now.tm_year + 1900 < 2025) return {};

        int now_min = tm_now.tm_hour * 60 + tm_now.tm_min;

        // 检查日历事件（按月存储的每日事件）
        std::string month_key = std::to_string(tm_now.tm_year + 1900) + "-" + std::to_string(tm_now.tm_mon + 1);
        auto it = calendar_.find(month_key);
        if (it != calendar_.end()) {
            for (auto& e : it->second) {
                if (e.day != tm_now.tm_mday) continue;
                int t_min = e.hour * 60 + e.minute;
                if (t_min == now_min) {
                    int key = tm_now.tm_yday * 100000 + e.slot * 10000 + t_min;
                    if (last_fired_ == key) continue;
                    last_fired_ = key;

                    char msg[128];
                    const char* drug = "";
                    if (e.slot >= 1 && e.slot <= 7 && !plan_.slots[e.slot-1].drug_name.empty())
                        drug = plan_.slots[e.slot-1].drug_name.c_str();
                    snprintf(msg, sizeof(msg),
                             "用药提醒: %s, 槽位%d, 每次%d颗",
                             drug[0] ? drug : "(未命名)", e.slot, e.dose);
                    ESP_LOGI(TAG_MCS, "%s", msg);

                    if (on_event_fired_) on_event_fired_(e.slot, e.dose, msg);

                    char screen_msg[128];
                    snprintf(screen_msg, sizeof(screen_msg),
                             "%s 槽%d x%d颗",
                             drug[0] ? drug : "(未命名)", e.slot, e.dose);
                    return std::string(screen_msg);
                }
            }
        }

        // 检查槽位每日重复事件（向后兼容）
        for (auto& s : plan_.slots) {
            if (s.drug_name.empty() || s.daily_frequency == 0) continue;
            for (int i = 0; i < s.daily_frequency; i++) {
                int t_min = s.times[i].hour * 60 + s.times[i].minute;
                if (t_min == now_min) {
                    int key = tm_now.tm_yday * 100000 + s.slot * 10000 + t_min;
                    if (last_fired_ == key) continue;
                    last_fired_ = key;

                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "用药提醒: %s, 槽位%d, 每次%d颗",
                             s.drug_name.c_str(), s.slot, s.dose_count);
                    ESP_LOGI(TAG_MCS, "%s", msg);

                    if (on_event_fired_) on_event_fired_(s.slot, s.dose_count, msg);

                    char screen_msg[128];
                    snprintf(screen_msg, sizeof(screen_msg),
                             "%s 槽%d x%d颗",
                             s.drug_name.c_str(), s.slot, s.dose_count);
                    return std::string(screen_msg);
                }
            }
        }
        return {};
    }

    MedPlan& GetPlan() { return plan_; }

    /// @brief 生成今日用药计划摘要（供 LCD 侧边栏显示）
    std::string GetPlanSummary() {
        if (!plan_.master_enabled) return "总开关已关闭";

        struct Entry {
            int minutes;
            std::string line;
        };
        std::vector<Entry> entries;

        // 日历事件
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        std::string month_key = std::to_string(tm_now.tm_year + 1900) + "-" + std::to_string(tm_now.tm_mon + 1);
        auto it = calendar_.find(month_key);
        if (it != calendar_.end()) {
            for (auto& e : it->second) {
                if (e.day != tm_now.tm_mday) continue;
                const char* drug = "";
                if (e.slot >= 1 && e.slot <= 7 && !plan_.slots[e.slot-1].drug_name.empty())
                    drug = plan_.slots[e.slot-1].drug_name.c_str();
                char line[64];
                snprintf(line, sizeof(line), "%02d:%02d %s x%d",
                         e.hour, e.minute,
                         drug[0] ? drug : "(未命名)", e.dose);
                entries.push_back({e.hour * 60 + e.minute, std::string(line)});
            }
        }

        // 槽位每日重复事件
        for (auto& s : plan_.slots) {
            if (s.drug_name.empty() || s.daily_frequency == 0) continue;
            for (int i = 0; i < s.daily_frequency; i++) {
                char line[64];
                snprintf(line, sizeof(line), "%02d:%02d %s x%d",
                         s.times[i].hour, s.times[i].minute,
                         s.drug_name.c_str(), s.dose_count);
                entries.push_back({s.times[i].hour * 60 + s.times[i].minute, std::string(line)});
            }
        }

        if (entries.empty()) return "暂无计划";

        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.minutes < b.minutes; });

        std::string result;
        for (auto& e : entries) {
            if (!result.empty()) result += "\n";
            result += e.line;
        }
        return result;
    }

private:
    httpd_handle_t server_;
    MedPlan plan_;
    int last_fired_ = -1;
    std::map<std::string, std::vector<CalendarEvent>> calendar_;
    std::function<void(int slot, int dose, const char* msg)> on_event_fired_;
    PillBoxTurntable* turntable_ = nullptr;

    static constexpr const char* NVS_NS = "medplan";
    static constexpr const char* NVS_KEY = "plan";
    static constexpr const char* NVS_KEY_CAL = "calendar";

    void SaveToNVS() {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "master", plan_.master_enabled);

        cJSON* arr = cJSON_AddArrayToObject(root, "slots");
        for (auto& s : plan_.slots) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "slot", s.slot);
            cJSON_AddStringToObject(item, "drug", s.drug_name.c_str());
            cJSON_AddStringToObject(item, "shape", s.shape_label.c_str());
            cJSON_AddNumberToObject(item, "dose", s.dose_count);
            cJSON_AddNumberToObject(item, "freq", s.daily_frequency);
            cJSON_AddNumberToObject(item, "remain", s.remaining);
            if (!s.expiry_date.empty())
                cJSON_AddStringToObject(item, "expiry", s.expiry_date.c_str());

            cJSON* times = cJSON_AddArrayToObject(item, "times");
            for (int i = 0; i < s.daily_frequency; i++) {
                cJSON* t = cJSON_CreateObject();
                cJSON_AddNumberToObject(t, "h", s.times[i].hour);
                cJSON_AddNumberToObject(t, "m", s.times[i].minute);
                cJSON_AddItemToArray(times, t);
            }
            cJSON_AddItemToArray(arr, item);
        }

        char* js = cJSON_PrintUnformatted(root);
        if (js) {
            nvs_set_str(h, NVS_KEY, js);
            nvs_commit(h);
            cJSON_free(js);
        }
        cJSON_Delete(root);

        // 保存日历数据
        SaveCalendarToNVS(h);

        nvs_close(h);
    }

    void SaveCalendarToNVS(nvs_handle_t h) {
        cJSON* root = cJSON_CreateObject();
        for (auto& kv : calendar_) {
            cJSON* month_arr = cJSON_AddArrayToObject(root, kv.first.c_str());
            for (auto& e : kv.second) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddNumberToObject(item, "day", e.day);
                cJSON_AddNumberToObject(item, "slot", e.slot);
                cJSON_AddNumberToObject(item, "hour", e.hour);
                cJSON_AddNumberToObject(item, "minute", e.minute);
                cJSON_AddNumberToObject(item, "dose", e.dose);
                cJSON_AddItemToArray(month_arr, item);
            }
        }
        char* js = cJSON_PrintUnformatted(root);
        if (js) {
            nvs_set_str(h, NVS_KEY_CAL, js);
            nvs_commit(h);
            cJSON_free(js);
        }
        cJSON_Delete(root);
    }

    void LoadFromNVS() {
        // 初始化默认计划
        plan_.master_enabled = true;
        for (int i = 0; i < 7; i++) {
            plan_.slots[i].slot = i + 1;
            plan_.slots[i].dose_count = 1;
            plan_.slots[i].daily_frequency = 0;
            plan_.slots[i].remaining = 0;
            plan_.slots[i].expiry_date.clear();
        }

        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

        size_t len = 0;
        if (nvs_get_str(h, NVS_KEY, nullptr, &len) == ESP_OK) {
            char* js = new char[len];
            if (nvs_get_str(h, NVS_KEY, js, &len) == ESP_OK) {
                ParsePlanJson(js);
            }
            delete[] js;
        }

        // 加载日历数据
        len = 0;
        if (nvs_get_str(h, NVS_KEY_CAL, nullptr, &len) == ESP_OK) {
            char* js = new char[len];
            if (nvs_get_str(h, NVS_KEY_CAL, js, &len) == ESP_OK) {
                ParseCalendarJson(js);
            }
            delete[] js;
        }

        nvs_close(h);
        ESP_LOGI(TAG_MCS, "从NVS加载用药计划: 总开关=%d, 日历月份数=%d",
                 plan_.master_enabled, (int)calendar_.size());
    }

    void ParsePlanJson(const char* js) {
        cJSON* root = cJSON_Parse(js);
        if (!root) return;

        cJSON* master = cJSON_GetObjectItem(root, "master");
        if (cJSON_IsBool(master)) plan_.master_enabled = cJSON_IsTrue(master);

        cJSON* arr = cJSON_GetObjectItem(root, "slots");
        if (cJSON_IsArray(arr)) {
            cJSON* item = nullptr;
            cJSON_ArrayForEach(item, arr) {
                cJSON* slot_j = cJSON_GetObjectItem(item, "slot");
                if (!cJSON_IsNumber(slot_j)) continue;
                int idx = slot_j->valueint - 1;
                if (idx < 0 || idx >= 7) continue;

                auto& s = plan_.slots[idx];
                cJSON* d = cJSON_GetObjectItem(item, "drug");
                if (cJSON_IsString(d)) s.drug_name = d->valuestring;
                cJSON* sh = cJSON_GetObjectItem(item, "shape");
                if (cJSON_IsString(sh)) s.shape_label = sh->valuestring;
                cJSON* dose = cJSON_GetObjectItem(item, "dose");
                if (cJSON_IsNumber(dose)) s.dose_count = dose->valueint;
                cJSON* freq = cJSON_GetObjectItem(item, "freq");
                if (cJSON_IsNumber(freq)) s.daily_frequency = freq->valueint;
                cJSON* remain = cJSON_GetObjectItem(item, "remain");
                if (cJSON_IsNumber(remain)) s.remaining = remain->valueint;
                cJSON* expiry = cJSON_GetObjectItem(item, "expiry");
                if (cJSON_IsString(expiry)) s.expiry_date = expiry->valuestring;

                cJSON* times = cJSON_GetObjectItem(item, "times");
                if (cJSON_IsArray(times)) {
                    int ti = 0;
                    cJSON* t = nullptr;
                    cJSON_ArrayForEach(t, times) {
                        if (ti >= 5) break;
                        cJSON* hh = cJSON_GetObjectItem(t, "h");
                        cJSON* mm = cJSON_GetObjectItem(t, "m");
                        if (cJSON_IsNumber(hh)) s.times[ti].hour = hh->valueint;
                        if (cJSON_IsNumber(mm)) s.times[ti].minute = mm->valueint;
                        ti++;
                    }
                }
            }
        }
        cJSON_Delete(root);
    }

    void ParseCalendarJson(const char* js) {
        cJSON* root = cJSON_Parse(js);
        if (!root) return;

        cJSON* month_item = root->child;
        while (month_item) {
            if (cJSON_IsArray(month_item) && month_item->string) {
                std::string key = month_item->string;
                std::vector<CalendarEvent> events;
                cJSON* ev = nullptr;
                cJSON_ArrayForEach(ev, month_item) {
                    CalendarEvent e;
                    cJSON* day = cJSON_GetObjectItem(ev, "day");
                    cJSON* slot = cJSON_GetObjectItem(ev, "slot");
                    cJSON* hour = cJSON_GetObjectItem(ev, "hour");
                    cJSON* minute = cJSON_GetObjectItem(ev, "minute");
                    cJSON* dose = cJSON_GetObjectItem(ev, "dose");
                    if (cJSON_IsNumber(day)) e.day = day->valueint;
                    if (cJSON_IsNumber(slot)) e.slot = slot->valueint;
                    if (cJSON_IsNumber(hour)) e.hour = hour->valueint;
                    if (cJSON_IsNumber(minute)) e.minute = minute->valueint;
                    if (cJSON_IsNumber(dose)) e.dose = dose->valueint;
                    events.push_back(e);
                }
                calendar_[key] = std::move(events);
            }
            month_item = month_item->next;
        }
        cJSON_Delete(root);
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
        esp_err_t err = httpd_register_uri_handler(server_, &uri_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_MCS, "注册 %s %s 失败 (err=%d)", method == HTTP_GET ? "GET" : "POST", uri, err);
            delete ctx;
        }
    }

    void RegisterHandlers() {
        // GET /medicine-config → HTML 页面
        RegisterUri("/medicine-config", HTTP_GET,
            [](httpd_req_t* req) {
                httpd_resp_set_type(req, "text/html; charset=utf-8");
                httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
                httpd_resp_send(req, MEDICINE_CONFIG_HTML, HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            });

        // GET /favicon.ico → 空响应 (避免浏览器 404)
        RegisterUri("/favicon.ico", HTTP_GET,
            [](httpd_req_t* req) {
                httpd_resp_set_type(req, "image/x-icon");
                httpd_resp_send(req, nullptr, 0);
                return ESP_OK;
            });

        // GET /api/health → 健康检查
        RegisterUri("/api/health", HTTP_GET,
            [](httpd_req_t* req) {
                cJSON* r = cJSON_CreateObject();
                cJSON_AddBoolToObject(r, "ok", true);
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        // GET /api/medicine/plan → 获取用药计划
        RegisterUri("/api/medicine/plan", HTTP_GET,
            [this](httpd_req_t* req) {
                cJSON* root = cJSON_CreateObject();
                cJSON_AddBoolToObject(root, "master_enabled", plan_.master_enabled);

                cJSON* arr = cJSON_AddArrayToObject(root, "slots");
                for (auto& s : plan_.slots) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddNumberToObject(item, "slot", s.slot);
                    cJSON_AddStringToObject(item, "drug_name", s.drug_name.c_str());
                    cJSON_AddStringToObject(item, "shape_label", s.shape_label.c_str());
                    cJSON_AddNumberToObject(item, "dose_count", s.dose_count);
                    cJSON_AddNumberToObject(item, "daily_frequency", s.daily_frequency);
                    cJSON_AddNumberToObject(item, "remaining", s.remaining);
                    if (!s.expiry_date.empty())
                        cJSON_AddStringToObject(item, "expiry_date", s.expiry_date.c_str());

                    cJSON* times = cJSON_AddArrayToObject(item, "times");
                    for (int i = 0; i < s.daily_frequency; i++) {
                        cJSON* t = cJSON_CreateObject();
                        cJSON_AddNumberToObject(t, "hour", s.times[i].hour);
                        cJSON_AddNumberToObject(t, "minute", s.times[i].minute);
                        cJSON_AddItemToArray(times, t);
                    }
                    cJSON_AddItemToArray(arr, item);
                }

                char* js = cJSON_PrintUnformatted(root);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(root);
                return ESP_OK;
            });

        // POST /api/medicine/plan → 保存用药计划
        RegisterUri("/api/medicine/plan", HTTP_POST,
            [this](httpd_req_t* req) {
                constexpr size_t kBufSize = 4096;
                auto buf = std::make_unique<char[]>(kBufSize);
                int ret = httpd_req_recv(req, buf.get(), kBufSize - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';

                cJSON* resp = cJSON_CreateObject();
                cJSON* root = cJSON_Parse(buf.get());
                if (!root) {
                    cJSON_AddBoolToObject(resp, "ok", false);
                    cJSON_AddStringToObject(resp, "error", "JSON解析失败");
                    goto send_plan;
                }

                {
                    cJSON* master = cJSON_GetObjectItem(root, "master_enabled");
                    if (cJSON_IsBool(master))
                        plan_.master_enabled = cJSON_IsTrue(master);

                    cJSON* arr = cJSON_GetObjectItem(root, "slots");
                    if (cJSON_IsArray(arr)) {
                        cJSON* item = nullptr;
                        cJSON_ArrayForEach(item, arr) {
                            cJSON* slot_j = cJSON_GetObjectItem(item, "slot");
                            if (!cJSON_IsNumber(slot_j)) continue;
                            int idx = slot_j->valueint - 1;
                            if (idx < 0 || idx >= 7) continue;

                            auto& s = plan_.slots[idx];
                            cJSON* d = cJSON_GetObjectItem(item, "drug_name");
                            if (cJSON_IsString(d)) s.drug_name = d->valuestring;
                            cJSON* sh = cJSON_GetObjectItem(item, "shape_label");
                            if (cJSON_IsString(sh)) s.shape_label = sh->valuestring;
                            cJSON* dose = cJSON_GetObjectItem(item, "dose_count");
                            if (cJSON_IsNumber(dose)) s.dose_count = dose->valueint;
                            cJSON* freq = cJSON_GetObjectItem(item, "daily_frequency");
                            if (cJSON_IsNumber(freq)) s.daily_frequency = freq->valueint;
                            cJSON* remain = cJSON_GetObjectItem(item, "remaining");
                            if (cJSON_IsNumber(remain)) s.remaining = remain->valueint;
                            cJSON* expiry = cJSON_GetObjectItem(item, "expiry_date");
                            if (cJSON_IsString(expiry)) s.expiry_date = expiry->valuestring;
                            // nullptr / "" / "null" 均视为清除
                            if (s.expiry_date == "null") s.expiry_date.clear();

                            cJSON* times = cJSON_GetObjectItem(item, "times");
                            if (cJSON_IsArray(times)) {
                                int ti = 0;
                                cJSON* t = nullptr;
                                cJSON_ArrayForEach(t, times) {
                                    if (ti >= 5) break;
                                    cJSON* hh = cJSON_GetObjectItem(t, "hour");
                                    cJSON* mm = cJSON_GetObjectItem(t, "minute");
                                    if (cJSON_IsNumber(hh)) s.times[ti].hour = hh->valueint;
                                    if (cJSON_IsNumber(mm)) s.times[ti].minute = mm->valueint;
                                    ti++;
                                }
                            }
                        }
                    }

                    SaveToNVS();
                    cJSON_AddBoolToObject(resp, "ok", true);
                    ESP_LOGI(TAG_MCS, "用药计划已保存 (总开关=%d)", plan_.master_enabled);
                }
                cJSON_Delete(root);

            send_plan:
                char* js = cJSON_PrintUnformatted(resp);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(resp);
                return ESP_OK;
            });

        // GET /api/medicine/calendar?year=YYYY&month=MM → 获取日历事件
        RegisterUri("/api/medicine/calendar", HTTP_GET,
            [this](httpd_req_t* req) {
                char buf[128] = {};
                if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
                    char year_str[8] = {}, month_str[8] = {};
                    httpd_query_key_value(buf, "year", year_str, sizeof(year_str));
                    httpd_query_key_value(buf, "month", month_str, sizeof(month_str));
                    int year = atoi(year_str);
                    int month = atoi(month_str);

                    if (year >= 2025 && month >= 1 && month <= 12) {
                        auto month_key = std::to_string(year) + "-" + std::to_string(month);
                        auto it = calendar_.find(month_key);

                        cJSON* root = cJSON_CreateObject();
                        cJSON_AddNumberToObject(root, "year", year);
                        cJSON_AddNumberToObject(root, "month", month);
                        cJSON* arr = cJSON_AddArrayToObject(root, "events");

                        if (it != calendar_.end()) {
                            for (auto& e : it->second) {
                                cJSON* item = cJSON_CreateObject();
                                cJSON_AddNumberToObject(item, "day", e.day);
                                cJSON_AddNumberToObject(item, "slot", e.slot);
                                cJSON_AddNumberToObject(item, "hour", e.hour);
                                cJSON_AddNumberToObject(item, "minute", e.minute);
                                cJSON_AddNumberToObject(item, "dose", e.dose);
                                cJSON_AddItemToArray(arr, item);
                            }
                        }

                        char* js = cJSON_PrintUnformatted(root);
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                        cJSON_free(js);
                        cJSON_Delete(root);
                        return ESP_OK;
                    }
                }
                httpd_resp_send_500(req);
                return ESP_FAIL;
            });

        // POST /api/medicine/calendar → 修改日历事件
        // action: "set" (add single), "clear_day" (clear day),
        //         "fill_month", "fill_week", "set_all" (replace all)
        RegisterUri("/api/medicine/calendar", HTTP_POST,
            [this](httpd_req_t* req) {
                constexpr size_t kBufSize = 8192;
                auto buf = std::make_unique<char[]>(kBufSize);
                int ret = httpd_req_recv(req, buf.get(), kBufSize - 1);
                if (ret <= 0) return ESP_FAIL;
                buf[ret] = '\0';

                cJSON* resp = cJSON_CreateObject();
                cJSON* root = cJSON_Parse(buf.get());
                if (!root) {
                    cJSON_AddBoolToObject(resp, "ok", false);
                    cJSON_AddStringToObject(resp, "error", "JSON解析失败");
                    goto send_cal;
                }

                {
                    cJSON* action_j = cJSON_GetObjectItem(root, "action");
                    const char* action = cJSON_IsString(action_j) ? action_j->valuestring : "";

                    if (strcmp(action, "set") == 0) {
                        cJSON* year_j = cJSON_GetObjectItem(root, "year");
                        cJSON* month_j = cJSON_GetObjectItem(root, "month");
                        cJSON* day_j = cJSON_GetObjectItem(root, "day");
                        cJSON* slot_j = cJSON_GetObjectItem(root, "slot");
                        cJSON* hour_j = cJSON_GetObjectItem(root, "hour");
                        cJSON* minute_j = cJSON_GetObjectItem(root, "minute");
                        cJSON* dose_j = cJSON_GetObjectItem(root, "dose");

                        if (cJSON_IsNumber(year_j) && cJSON_IsNumber(month_j) &&
                            cJSON_IsNumber(day_j) && cJSON_IsNumber(slot_j) &&
                            cJSON_IsNumber(hour_j) && cJSON_IsNumber(minute_j)) {
                            CalendarEvent e;
                            e.day = day_j->valueint;
                            e.slot = slot_j->valueint;
                            e.hour = hour_j->valueint;
                            e.minute = minute_j->valueint;
                            e.dose = cJSON_IsNumber(dose_j) ? dose_j->valueint : 1;
                            auto month_key = std::to_string(year_j->valueint) + "-" + std::to_string(month_j->valueint);
                            calendar_[month_key].push_back(e);
                            SaveToNVS();
                            cJSON_AddBoolToObject(resp, "ok", true);
                        } else {
                            cJSON_AddBoolToObject(resp, "ok", false);
                            cJSON_AddStringToObject(resp, "error", "参数不完整");
                        }
                    } else if (strcmp(action, "clear_day") == 0) {
                        cJSON* year_j = cJSON_GetObjectItem(root, "year");
                        cJSON* month_j = cJSON_GetObjectItem(root, "month");
                        cJSON* day_j = cJSON_GetObjectItem(root, "day");
                        if (cJSON_IsNumber(year_j) && cJSON_IsNumber(month_j) && cJSON_IsNumber(day_j)) {
                            auto month_key = std::to_string(year_j->valueint) + "-" + std::to_string(month_j->valueint);
                            auto& vec = calendar_[month_key];
                            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                [d=day_j->valueint](const CalendarEvent& e) { return e.day == d; }),
                                vec.end());
                            if (vec.empty()) calendar_.erase(month_key);
                            SaveToNVS();
                            cJSON_AddBoolToObject(resp, "ok", true);
                        }
                    } else if (strcmp(action, "set_all") == 0) {
                        cJSON* data = cJSON_GetObjectItem(root, "data");
                        if (cJSON_IsObject(data)) {
                            calendar_.clear();
                            char* js = cJSON_PrintUnformatted(data);
                            if (js) {
                                ParseCalendarJson(js);
                                cJSON_free(js);
                            }
                            SaveToNVS();
                            cJSON_AddBoolToObject(resp, "ok", true);
                            ESP_LOGI(TAG_MCS, "日历数据已保存 (%d个月份)", (int)calendar_.size());
                        } else {
                            cJSON_AddBoolToObject(resp, "ok", false);
                            cJSON_AddStringToObject(resp, "error", "data字段无效");
                        }
                    } else {
                        cJSON_AddBoolToObject(resp, "ok", false);
                        cJSON_AddStringToObject(resp, "error", "未知action");
                    }
                }
                cJSON_Delete(root);

            send_cal:
                char* js = cJSON_PrintUnformatted(resp);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(resp);
                return ESP_OK;
            });

        // ===== 转盘调试 API =====

        // GET /turntable-debug → 调试页面
        RegisterUri("/turntable-debug", HTTP_GET,
            [](httpd_req_t* req) {
                httpd_resp_set_type(req, "text/html; charset=utf-8");
                httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
                httpd_resp_send(req, TURNTABLE_DEBUG_HTML, HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            });

        // GET /api/turntable/status → 当前状态
        RegisterUri("/api/turntable/status", HTTP_GET,
            [this](httpd_req_t* req) {
                cJSON* r = cJSON_CreateObject();
                if (turntable_) {
                    cJSON_AddNumberToObject(r, "slot", turntable_->getCurrentSlot());
                    cJSON_AddBoolToObject(r, "ready", turntable_->isReady());
                } else {
                    cJSON_AddNumberToObject(r, "slot", -1);
                    cJSON_AddBoolToObject(r, "ready", false);
                }
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        // POST /api/turntable/go → 转到指定槽 {slot:N}
        RegisterUri("/api/turntable/go", HTTP_POST,
            [this](httpd_req_t* req) {
                char buf[128] = {};
                httpd_req_recv(req, buf, sizeof(buf) - 1);
                cJSON* root = cJSON_Parse(buf);
                cJSON* r = cJSON_CreateObject();
                if (root) {
                    cJSON* slot_j = cJSON_GetObjectItem(root, "slot");
                    if (cJSON_IsNumber(slot_j)) {
                        int slot = slot_j->valueint;
                        if (turntable_ && turntable_->isReady()) {
                            turntable_->goToSlot(slot);
                            cJSON_AddBoolToObject(r, "ok", true);
                            ESP_LOGI(TAG_MCS, "调试: 转至槽%d", slot);
                        } else {
                            cJSON_AddBoolToObject(r, "ok", false);
                            cJSON_AddStringToObject(r, "error", "转盘未就绪");
                        }
                    }
                    cJSON_Delete(root);
                }
                if (!cJSON_HasObjectItem(r, "ok")) {
                    cJSON_AddBoolToObject(r, "ok", false);
                    cJSON_AddStringToObject(r, "error", "无效请求");
                }
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });

        // POST /api/turntable/home → 归零
        RegisterUri("/api/turntable/home", HTTP_POST,
            [this](httpd_req_t* req) {
                cJSON* r = cJSON_CreateObject();
                if (turntable_ && turntable_->isReady()) {
                    turntable_->goHome();
                    cJSON_AddBoolToObject(r, "ok", true);
                    ESP_LOGI(TAG_MCS, "调试: 归零");
                } else {
                    cJSON_AddBoolToObject(r, "ok", false);
                    cJSON_AddStringToObject(r, "error", "转盘未就绪");
                }
                char* js = cJSON_PrintUnformatted(r);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, js, HTTPD_RESP_USE_STRLEN);
                cJSON_free(js);
                cJSON_Delete(r);
                return ESP_OK;
            });
    }
};

#endif
