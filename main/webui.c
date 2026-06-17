/* WebUI HTTP server using raw lwIP sockets for V1.3.0 */

#include "webui.h"

#include "app_config.h"
#include "app_state.h"
#include "cellular_ecm.h"
#include "esp_log.h"
#include "diag_system.h"
#include "fault_log.h"
#include "wifi_ap.h"

#include "app_tasks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "webui";
static volatile bool s_running = false;
static volatile bool s_restart_requested = false;
static volatile int s_listen_sock = -1;
static volatile int s_active_sock = -1;
static volatile TickType_t s_last_alive_tick = 0;
static TaskHandle_t s_webui_task = NULL;
static TaskHandle_t s_monitor_task = NULL;

#define WEBUI_TASK_PRIORITY 5
#define WEBUI_MONITOR_PRIORITY 3
#define WEBUI_TASK_STACK_BYTES 8192
#define WEBUI_MONITOR_STACK_BYTES 3072
#define WEBUI_ACCEPT_POLL_MS 100
#define WEBUI_CLIENT_TIMEOUT_MS 3000
#define WEBUI_STALE_TIMEOUT_MS 20000
#define WEBUI_RESTART_DELAY_MS 1000
#define WEBUI_FAULT_LOG_INTERVAL_MS 30000

/* ================================================================
 * Embedded single-page HTML/CSS/JS
 * ================================================================ */

static const char INDEX_HTML[] =
"<!DOCTYPE html>"
"<html lang=\"zh\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0,viewport-fit=cover\">"
"<title>ECM 路由器</title>"
"<style>"
":root{--bg:#f5f7fb;--card:rgba(255,255,255,.76);--line:rgba(60,60,67,.12);--text:#111827;--muted:#6b7280;--blue:#007aff;--green:#34c759;--orange:#ff9f0a;--red:#ff3b30;--shadow:0 18px 50px rgba(20,33,61,.13)}"
"*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
"body{font-family:-apple-system,BlinkMacSystemFont,\"SF Pro Display\",\"PingFang SC\",\"Microsoft YaHei\",sans-serif;background:radial-gradient(circle at 12% -6%,#dff0ff 0,#f5f7fb 34%,transparent 35%),linear-gradient(180deg,#fbfdff 0,#eef2f7 100%);color:var(--text);max-width:460px;margin:0 auto;padding:calc(18px + env(safe-area-inset-top)) 16px 34px;min-height:100vh}"
"body:before{content:\"\";position:fixed;inset:-80px -80px auto auto;width:220px;height:220px;border-radius:50%;background:rgba(0,122,255,.16);filter:blur(18px);z-index:-1}"
"header{display:flex;justify-content:space-between;align-items:flex-end;padding:12px 2px 16px}"
".kicker{font-size:.78em;color:var(--muted);font-weight:700;letter-spacing:.4px}"
"h1{font-size:2em;line-height:1.05;font-weight:800;letter-spacing:-1px}"
".ver{font-size:.76em;color:#375172;background:rgba(255,255,255,.72);border:1px solid var(--line);border-radius:999px;padding:7px 11px;box-shadow:0 8px 24px rgba(31,41,55,.08)}"
"nav{display:flex;gap:4px;background:rgba(118,118,128,.12);border:1px solid rgba(255,255,255,.7);border-radius:18px;padding:4px;margin-bottom:16px;backdrop-filter:blur(18px)}"
"nav button{flex:1;padding:11px;border:0;background:transparent;color:var(--muted);font-size:.92em;font-weight:700;cursor:pointer;border-radius:14px;transition:.18s}"
"nav button.active{background:#fff;color:var(--text);box-shadow:0 5px 18px rgba(31,41,55,.12)}"
".card{background:var(--card);backdrop-filter:blur(22px);border:1px solid rgba(255,255,255,.82);border-radius:28px;padding:18px;margin-bottom:14px;box-shadow:var(--shadow)}"
".hero{display:flex;justify-content:space-between;gap:16px;overflow:hidden;position:relative}"
".hero:after{content:\"\";position:absolute;right:-34px;bottom:-42px;width:140px;height:140px;border-radius:50%;background:rgba(52,199,89,.12)}"
".eyebrow,.card-title{font-size:.74em;color:var(--muted);font-weight:800;text-transform:uppercase;letter-spacing:1.2px;margin-bottom:10px}"
".hero-title{font-size:2.2em;font-weight:850;letter-spacing:-1.4px}"
".hero-sub{margin-top:6px;color:var(--muted);font-size:.9em;line-height:1.45;max-width:260px}"
".orb{width:82px;height:82px;border-radius:24px;background:linear-gradient(145deg,#34c759,#00c7be);box-shadow:0 16px 35px rgba(52,199,89,.35);display:grid;place-items:center;flex:0 0 auto;z-index:1}"
".orb span{width:34px;height:34px;border-radius:50%;background:#fff;box-shadow:inset 0 0 0 10px rgba(255,255,255,.45)}"
".orb.warn{background:linear-gradient(145deg,#ff9f0a,#ffcc00);box-shadow:0 16px 35px rgba(255,159,10,.32)}"
".orb.bad{background:linear-gradient(145deg,#ff3b30,#ff6b6b);box-shadow:0 16px 35px rgba(255,59,48,.28)}"
".quickgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px}"
".mini{background:rgba(255,255,255,.68);border:1px solid rgba(255,255,255,.86);border-radius:22px;padding:14px;box-shadow:0 12px 30px rgba(31,41,55,.08)}"
".mini .mttl{font-size:.76em;color:var(--muted);font-weight:750}.mini .mval{margin-top:6px;font-size:1.2em;font-weight:800;word-break:break-all}"
".uplink{display:grid;grid-template-columns:repeat(5,1fr);gap:7px}"
".step{display:flex;flex-direction:column;align-items:center;gap:7px;font-size:.72em;padding:10px 5px;border-radius:18px;background:rgba(118,118,128,.09);color:var(--muted);font-weight:800;transition:.25s}"
".step .dot{width:11px;height:11px;border-radius:50%;background:rgba(118,118,128,.3)}"
".step.on{background:rgba(52,199,89,.14);color:#147a35}.step.on .dot{background:var(--green);box-shadow:0 0 0 5px rgba(52,199,89,.16)}"
".step.warn{background:rgba(255,159,10,.15);color:#a15b00}.step.warn .dot{background:var(--orange);box-shadow:0 0 0 5px rgba(255,159,10,.14)}"
".arrow{display:none}"
".row{display:flex;justify-content:space-between;gap:16px;padding:12px 0;border-bottom:1px solid var(--line);font-size:.92em;line-height:1.45}"
".row:last-child{border:none}.lbl{color:var(--muted);font-weight:650}.val{text-align:right;word-break:break-word;max-width:62%;font-weight:700}"
".temp{display:inline-flex;align-items:baseline;gap:6px;font-size:2.35em;font-weight:850;letter-spacing:-1.5px;color:var(--green)}"
".temp:after{content:\"设备温度\";font-size:.32em;color:var(--muted);font-weight:700;letter-spacing:0}.temp.warn{color:var(--red)}"
".errbox{background:rgba(255,59,48,.1);color:#b42318;padding:13px 15px;border-radius:18px;margin:0 0 14px;font-size:.86em;display:none;word-break:break-all;border:1px solid rgba(255,59,48,.16)}"
".sigbars{display:inline-flex;gap:3px;align-items:flex-end;height:16px;vertical-align:-2px;margin-right:7px}"
".sigbar{width:4px;background:rgba(118,118,128,.24);border-radius:3px}.sigbar:nth-child(1){height:6px}.sigbar:nth-child(2){height:9px}.sigbar:nth-child(3){height:12px}.sigbar:nth-child(4){height:15px}"
".sigbar.a{background:var(--green)}.sigbar.w{background:var(--orange)}"
"input,select{width:100%;padding:15px 14px;margin-bottom:12px;background:rgba(255,255,255,.72);border:1px solid var(--line);border-radius:17px;color:var(--text);font-size:1em;font-family:inherit;outline:none}"
"input:focus,select:focus{border-color:rgba(0,122,255,.55);box-shadow:0 0 0 4px rgba(0,122,255,.11)}"
".btn{width:100%;padding:15px;border:0;border-radius:18px;font-size:1em;cursor:pointer;margin-bottom:10px;font-family:inherit;font-weight:800;transition:.15s}"
".btn:active{transform:scale(.985)}.btn-go{background:var(--blue);color:#fff;box-shadow:0 13px 28px rgba(0,122,255,.24)}.btn-grey{background:rgba(118,118,128,.14);color:var(--text)}"
".toast{position:fixed;top:calc(16px + env(safe-area-inset-top));left:50%;transform:translateX(-50%);padding:12px 22px;border-radius:999px;font-size:.9em;z-index:99;display:none;white-space:nowrap;font-weight:800;box-shadow:0 16px 40px rgba(31,41,55,.18)}"
".toast.ok{background:#111827;color:#fff}.toast.err{background:var(--red);color:#fff}"
".footer{padding:10px 6px 0;text-align:center;color:var(--muted);font-size:.78em;line-height:1.8}"
".footer a{color:var(--blue);font-weight:800;text-decoration:none}"
"@media(max-width:380px){body{padding-left:12px;padding-right:12px}.hero-title{font-size:1.8em}.quickgrid{grid-template-columns:1fr}.uplink{grid-template-columns:repeat(3,1fr)}}"
"</style>"
"</head>"
"<body>"
"<header>"
"<div><div class=\"kicker\">ECM Router</div><h1>控制中心</h1></div>"
"<div class=\"ver\" id=\"ver\">V1.3.0</div>"
"</header>"
"<nav>"
"<button id=\"btnDash\" class=\"active\" onclick=\"showTab(0)\">状态</button>"
"<button id=\"btnSet\" onclick=\"showTab(1)\">设置</button>"
"</nav>"
"<div id=\"dash\">"
"<div class=\"card hero\">"
"<div>"
"<div class=\"eyebrow\">蜂窝热点</div>"
"<div class=\"hero-title\" id=\"heroTitle\">正在连接</div>"
"<div class=\"hero-sub\" id=\"heroSub\">等待设备状态同步</div>"
"</div>"
"<div class=\"orb warn\" id=\"heroOrb\"><span></span></div>"
"</div>"
"<div class=\"quickgrid\">"
"<div class=\"mini\"><div class=\"mttl\">4G 信号</div><div class=\"mval\" id=\"miniSig\">--</div></div>"
"<div class=\"mini\"><div class=\"mttl\">客户端</div><div class=\"mval\" id=\"miniSta\">--</div></div>"
"<div class=\"mini\"><div class=\"mttl\">ECM IP</div><div class=\"mval\" id=\"miniIp\">--</div></div>"
"<div class=\"mini\"><div class=\"mttl\">温度</div><div class=\"mval\" id=\"miniTemp\">--</div></div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-title\">链路状态</div>"
"<div class=\"uplink\" id=\"uplink\"></div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-title\">4G 网络</div>"
"<div id=\"net4g\"></div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-title\">Wi-Fi</div>"
"<div id=\"wifi\"></div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-title\">芯片温度</div>"
"<div id=\"thermal\"></div>"
"</div>"
"<div class=\"errbox\" id=\"errbox\"></div>"
"</div>"
"<div id=\"set\" hidden>"
"<div class=\"card\">"
"<div class=\"card-title\">Wi-Fi 设置</div>"
"<input id=\"cfgSSID\" placeholder=\"SSID\" maxlength=\"32\">"
"<input id=\"cfgPass\" type=\"password\" placeholder=\"密码\" maxlength=\"63\">"
"<select id=\"cfgCh\">"
"<option value=\"1\">信道 1</option>"
"<option value=\"6\">信道 6</option>"
"<option value=\"11\">信道 11</option>"
"</select>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-title\">APN 设置</div>"
"<select id=\"cfgAPN\" onchange=\"apnChg()\">"
"<option value=\"0\">中国移动 (cmnet)</option>"
"<option value=\"1\">中国联通 (3gnet)</option>"
"<option value=\"2\">中国电信 (ctnet)</option>"
"<option value=\"3\">自定义 APN</option>"
"</select>"
"<input id=\"cfgCustom\" placeholder=\"自定义 APN\" maxlength=\"31\">"
"</div>"
"<button class=\"btn btn-go\" onclick=\"doSave()\">保存并应用</button>"
"<button class=\"btn btn-grey\" onclick=\"doReconn()\">重连蜂窝网络</button>"
"</div>"
"<footer class=\"footer\">作者邮箱 hsp2028.163.com<br>GitHub 地址 <a href=\"https://github.com/HSPLYMZ/ESP32S3_ECM\">HSPLYMZ/ESP32S3_ECM</a></footer>"
"<div class=\"toast\" id=\"toast\"></div>"
"<script>"
"var gPollTimer=0;"
"function Q(s){return document.getElementById(s);}"
"function showTab(n){"
"Q('dash').hidden=n!==0;Q('set').hidden=n===0;"
"Q('btnDash').className=n===0?'active':'';"
"Q('btnSet').className=n===1?'active':'';"
"if(n===0)poll();else lcfg();"
"}"
"function apnChg(){Q('cfgCustom').hidden=Q('cfgAPN').value!=='3';}"
"function toast(msg,ok){"
"var e=Q('toast');e.textContent=msg;"
"e.className='toast '+(ok?'ok':'err');e.style.display='block';"
"setTimeout(function(){e.style.display='none'},2500);"
"}"
"function buildUplink(s){"
"var steps=["
"['USB','USB',s.cellular.usb],"
"['AT','AT',s.cellular.at],"
"['ECM','ECM',s.cellular.uplink],"
"['IP','IP',!!(s.cellular.ip&&s.cellular.ip!=='--')],"
"['NAT','NAT',s.cellular.napt]"
"];"
"var h='';"
"for(var i=0;i<steps.length;i++){"
"var st=steps[i];"
"var cls='step';"
"if(st[2])cls+=' on';"
"else if(i>0&&steps[i-1][2])cls+=' warn';"
"h+='<div class=\"'+cls+'\"><span class=\"dot\"></span><span class=\"lbl\">'+st[1]+'</span></div>';"
"}"
"return h;"
"}"
"function sigbars(csq){"
"var v=csq?parseInt(csq):0;if(isNaN(v))v=0;"
"var lv=v<10?0:v<16?1:v<22?2:v<26?3:4;"
"var s='<span class=sigbars>';"
"for(var i=0;i<4;i++){"
"var c=i<lv?'sigbar '+(lv<2?'w':'a'):'sigbar';"
"s+='<span class=\"'+c+'\"></span>';"
"}"
"s+='</span>';return s;"
"}"
"function updateHero(s){"
"var ok=s.cellular.usb&&s.cellular.at&&s.cellular.uplink&&s.cellular.napt;"
"var ip=s.cellular.ip&&s.cellular.ip!=='--'?s.cellular.ip:'等待 IP';"
"Q('heroTitle').textContent=ok?'在线':'检查中';"
"Q('heroSub').textContent=ok?'热点已通过 ECM 转发，IP '+ip:esc(s.cellular.dial||'链路正在建立');"
"Q('heroOrb').className='orb '+(ok?'':(s.cellular.error&&s.cellular.error!=='--'?'bad':'warn'));"
"}"
"function render(s){"
"Q('ver').textContent=s.ver;"
"updateHero(s);"
"Q('uplink').innerHTML=buildUplink(s);"
"Q('miniSig').innerHTML=sigbars(s.cellular.signal)+esc(s.cellular.signal);"
"Q('miniSta').textContent=s.softap.clients+' / 2';"
"Q('miniIp').textContent=s.cellular.ip||'--';"
"Q('miniTemp').textContent=s.thermal.temp.toFixed(1)+' °C';"
"var net=sigbars(s.cellular.signal)+'<b>'+esc(s.cellular.signal)+'</b>';"
"Q('net4g').innerHTML="
"'<div class=\"row\"><span class=\"lbl\">信号强度</span><span class=\"val\">'+net+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">SIM 卡</span><span class=\"val\">'+esc(s.cellular.sim)+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">网络注册</span><span class=\"val\">'+esc(s.cellular.cereg)+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">分组附着 CGATT</span><span class=\"val\">'+esc(s.cellular.cgatt)+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">运营商</span><span class=\"val\">'+esc(s.cellular.network)+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">拨号状态</span><span class=\"val\">'+esc(s.cellular.dial)+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">累计重连 / 失败</span><span class=\"val\">'+s.cellular.reconnects+' / '+s.cellular.failures+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">最近失败原因</span><span class=\"val\">'+esc(s.cellular.last_failure)+'</span></div>';"
"Q('wifi').innerHTML="
"'<div class=\"row\"><span class=\"lbl\">SSID</span><span class=\"val\">'+esc(s.softap.ssid)+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">信道</span><span class=\"val\">'+s.softap.channel+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">已连接</span><span class=\"val\">'+s.softap.clients+' / 2</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">ECM IP</span><span class=\"val\">'+esc(s.cellular.ip)+'</span></div>'"
"+'<div class=\"row\"><span class=\"lbl\">DNS</span><span class=\"val\">'+esc(s.cellular.dns)+'</span></div>';"
"var t=s.thermal;"
"Q('thermal').innerHTML='<span class=\"temp'+(t.warn?' warn':'')"
"+'\">'+t.temp.toFixed(1)+' °C</span>';"
"var eb=Q('errbox');var ce=s.cellular.error;"
"if(ce&&ce!=='--'){eb.textContent='错误: '+ce;eb.style.display='block';}"
"else{eb.style.display='none';}"
"}"
"function esc(s){"
"if(!s||s==='--')return'--';"
"var d=document.createElement('div');d.textContent=s;return d.innerHTML;"
"}"
"function poll(){"
"clearTimeout(gPollTimer);"
"fetch('/api/status').then(function(r){return r.json();}).then(function(j){"
"render(j);gPollTimer=setTimeout(poll,4000);"
"}).catch(function(){gPollTimer=setTimeout(poll,8000);});"
"}"
"function lcfg(){"
"fetch('/api/config').then(function(r){return r.json();}).then(function(j){"
"Q('cfgSSID').value=j.ssid||'';Q('cfgPass').value=j.password||'';"
"Q('cfgCh').value=j.channel;Q('cfgAPN').value=j.apn_profile;"
"Q('cfgCustom').value=j.custom_apn||'';apnChg();"
"});}"
"function doSave(){"
"var b='ssid='+encodeURIComponent(Q('cfgSSID').value)"
"+'&password='+encodeURIComponent(Q('cfgPass').value)"
"+'&channel='+Q('cfgCh').value+'&apn_profile='+Q('cfgAPN').value"
"+'&custom_apn='+encodeURIComponent(Q('cfgCustom').value);"
"fetch('/api/config',{method:'POST',"
"headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})"
".then(function(r){return r.json();}).then(function(j){"
"toast(j.ok?'已保存，正在重启服务...':'错误: '+j.error,j.ok);"
"if(j.ok)setTimeout(function(){poll();},3000);"
"});}"
"function doReconn(){"
"fetch('/api/reconnect',{method:'POST'}).then(function(r){return r.json();})"
".then(function(j){toast(j.ok?'正在重连...':'错误: '+j.error,j.ok);});"
"}"
"poll();"
"</script>"
"</body>"
"</html>";
/* ================================================================
 * HTTP Helpers - use \x0D\x0A for CRLF in format strings to avoid
 * physical CRLF bytes breaking C string literals
 * ================================================================ */

#define HTTP_CRLF "\x0D\x0A"

static int http_send(int sock, const char *status, const char *content_type, const char *body, int body_len)
{
    char header[256];
    int hdr_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s" HTTP_CRLF
        "Content-Type: %s" HTTP_CRLF
        "Content-Length: %d" HTTP_CRLF
        "Connection: close" HTTP_CRLF
        "Access-Control-Allow-Origin: *" HTTP_CRLF
        HTTP_CRLF,
        status, content_type, body_len);

    int sent = lwip_send(sock, header, hdr_len, 0);
    if (sent < 0) return -1;
    sent = lwip_send(sock, body, body_len, 0);
    return sent;
}

static int http_send_ok(int sock, const char *content_type, const char *body, int body_len)
{
    return http_send(sock, "200 OK", content_type, body, body_len);
}

static int http_send_json_ok(int sock, const char *json)
{
    return http_send_ok(sock, "application/json", json, (int)strlen(json));
}

static void json_escape(char *dst, size_t dst_len, const char *src)
{
    size_t used = 0;

    if (dst == NULL || dst_len == 0) {
        return;
    }

    while (src != NULL && *src != '\0' && used + 1 < dst_len) {
        unsigned char ch = (unsigned char)*src++;
        if ((ch == '"' || ch == '\\') && used + 2 < dst_len) {
            dst[used++] = '\\';
            dst[used++] = (char)ch;
        } else if (ch >= 0x20) {
            dst[used++] = (char)ch;
        }
    }
    dst[used] = '\0';
}

static void url_decode(char *dst, const char *src, size_t dst_len)
{
    char *out = dst;
    const char *in = src;
    while (*in && (size_t)(out - dst) < dst_len - 1) {
        if (*in == '%' && in[1] && in[2]) {
            char hex[3] = { in[1], in[2], '\0' };
            *out++ = (char)strtol(hex, NULL, 16);
            in += 3;
        } else if (*in == '+') {
            *out++ = ' ';
            in++;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
}

static bool form_get(const char *body, const char *key, char *value, size_t value_len)
{
    if (body == NULL || key == NULL || value == NULL || value_len == 0) {
        return false;
    }
    size_t key_len = strlen(key);
    const char *pos = body;
    while (*pos) {
        if (strncmp(pos, key, key_len) == 0 && pos[key_len] == '=') {
            const char *val_start = pos + key_len + 1;
            const char *val_end = strchr(val_start, '&');
            size_t len = val_end ? (size_t)(val_end - val_start) : strlen(val_start);
            if (len >= value_len) len = value_len - 1;
            memcpy(value, val_start, len);
            value[len] = '\0';
            url_decode(value, value, value_len);
            return true;
        }
        pos = strchr(pos, '&');
        if (!pos) break;
        pos++;
    }
    return false;
}

/* ================================================================
 * Route handlers
 * ================================================================ */

static void handle_root(int sock)
{
    http_send_ok(sock, "text/html", INDEX_HTML, (int)strlen(INDEX_HTML));
}

static void handle_status(int sock)
{
    app_state_snapshot_t state;
    cellular_status_t cell;
    char json[2048];
    char ssid[70];
    char dial[100];
    char error[200];
    char sim[70];
    char signal[70];
    char cereg[130];
    char cgatt[70];
    char last_failure[200];
    char network[200];

    app_state_get_snapshot(&state);
    cellular_ecm_get_status(&cell);
    json_escape(ssid, sizeof(ssid), state.config.ssid);
    json_escape(dial, sizeof(dial), cell.dial_status);
    json_escape(error, sizeof(error), cell.last_error);
    json_escape(sim, sizeof(sim), cell.sim_status);
    json_escape(signal, sizeof(signal), cell.signal_csq);
    json_escape(cereg, sizeof(cereg), cell.cereg_status);
    json_escape(cgatt, sizeof(cgatt), cell.cgatt_status);
    json_escape(last_failure, sizeof(last_failure), cell.last_failure);
    json_escape(network, sizeof(network), cell.network_info);

    snprintf(json, sizeof(json),
        "{"
        "\"ver\":\"%s\","
        "\"softap\":{"
            "\"ssid\":\"%s\","
            "\"started\":%s,"
            "\"channel\":%u,"
            "\"clients\":%u"
        "},"
        "\"cellular\":{"
            "\"usb\":%s,"
            "\"at\":%s,"
            "\"uplink\":%s,"
            "\"napt\":%s,"
            "\"ip\":\"%s\","
            "\"dns\":\"%s\","
            "\"dial\":\"%s\","
            "\"error\":\"%s\","
            "\"sim\":\"%s\","
            "\"signal\":\"%s\","
            "\"cereg\":\"%s\","
            "\"cgatt\":\"%s\","
            "\"reconnects\":%lu,"
            "\"failures\":%lu,"
            "\"last_failure\":\"%s\","
            "\"network\":\"%s\""
        "},"
        "\"thermal\":{"
            "\"temp\":%.1f,"
            "\"warn\":%s"
        "}"
        "}",
        "V1.3.0",
        ssid,
        state.softap_started ? "true" : "false",
        state.runtime_channel,
        state.connected_sta_count,
        cell.usb_connected ? "true" : "false",
        cell.at_ready ? "true" : "false",
        cell.uplink_connected ? "true" : "false",
        cell.napt_enabled ? "true" : "false",
        cell.uplink_ip[0] ? cell.uplink_ip : "--",
        cell.dns[0] ? cell.dns : "--",
        dial,
        error,
        sim,
        signal,
        cereg,
        cgatt,
        (unsigned long)cell.reconnect_count,
        (unsigned long)cell.failure_count,
        last_failure,
        network,
        (double)state.internal_temp_celsius,
        state.thermal_protect_active ? "true" : "false"
    );

    http_send_json_ok(sock, json);
}

static void handle_config_get(int sock)
{
    app_config_t config;
    char json[512];
    char ssid[70];
    char password[140];
    char custom_apn[70];

    app_state_get_config(&config);
    json_escape(ssid, sizeof(ssid), config.ssid);
    json_escape(password, sizeof(password), config.password);
    json_escape(custom_apn, sizeof(custom_apn), config.custom_apn);

    snprintf(json, sizeof(json),
        "{"
        "\"ssid\":\"%s\","
        "\"password\":\"%s\","
        "\"channel\":%u,"
        "\"apn_profile\":%d,"
        "\"custom_apn\":\"%s\""
        "}",
        ssid,
        password,
        config.channel,
        (int)config.apn_profile,
        custom_apn
    );

    http_send_json_ok(sock, json);
}

static void handle_config_post(int sock, const char *body)
{
    app_config_t config;
    app_state_get_config(&config);

    char val[128];

    if (form_get(body, "ssid", val, sizeof(val))) {
        strlcpy(config.ssid, val, sizeof(config.ssid));
    }
    if (form_get(body, "password", val, sizeof(val))) {
        strlcpy(config.password, val, sizeof(config.password));
    }
    if (form_get(body, "channel", val, sizeof(val))) {
        int ch = atoi(val);
        if (ch == 1 || ch == 6 || ch == 11) {
            config.channel = (uint8_t)ch;
        }
    }
    if (form_get(body, "apn_profile", val, sizeof(val))) {
        int apn = atoi(val);
        if (apn >= 0 && apn <= 3) {
            config.apn_profile = (app_apn_profile_t)apn;
        }
    }
    if (form_get(body, "custom_apn", val, sizeof(val))) {
        strlcpy(config.custom_apn, val, sizeof(config.custom_apn));
    }

    const char *err_msg = NULL;
    esp_err_t err = app_config_validate(&config, &err_msg);
    if (err != ESP_OK) {
        char resp[256];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}",
                 err_msg ? err_msg : "Invalid config");
        http_send_json_ok(sock, resp);
        return;
    }

    err = app_config_save(&config);
    if (err != ESP_OK) {
        http_send_json_ok(sock, "{\"ok\":false,\"error\":\"Failed to save config\"}");
        return;
    }

    app_state_set_config(&config);

    ESP_LOGI(TAG, "Config saved. Restarting services.");
    wifi_ap_apply_config(&config);
    cellular_ecm_apply_config(&config);
    cellular_ecm_request_reconnect();

    http_send_json_ok(sock, "{\"ok\":true}");
}

static void handle_reconnect(int sock)
{
    esp_err_t err = cellular_ecm_request_reconnect();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ECM reconnect triggered via WebUI");
        http_send_json_ok(sock, "{\"ok\":true}");
    } else {
        http_send_json_ok(sock, "{\"ok\":false,\"error\":\"ECM not ready\"}");
    }
}

static void handle_404(int sock)
{
    const char *body = "{\"error\":\"Not Found\"}";
    http_send(sock, "404 Not Found", "application/json", body, (int)strlen(body));
}

/* ================================================================
 * Request parser and dispatcher
 * ================================================================ */

static void handle_diag(int sock);
static void handle_faults(int sock);

static void process_request(int sock, char *buf, int len)
{
    /* Parse first line: METHOD /path HTTP/1.1 */
    char *method = buf;
    char *path = strchr(method, ' ');
    if (!path) goto bad;
    *path++ = '\0';
    char *proto = strchr(path, ' ');
    if (proto) *proto = '\0';

    /* Find body for POST */
    char *body = strstr(buf, HTTP_CRLF HTTP_CRLF);
    if (body) body += 4;

    ESP_LOGI(TAG, "%s %s", method, path);

    if (strcmp(path, "/api/diag") == 0 && strcmp(method, "GET") == 0) {
        handle_diag(sock);
    } else if (strcmp(path, "/api/faults") == 0 && strcmp(method, "GET") == 0) {
        handle_faults(sock);
    } else if (strcmp(path, "/") == 0 && strcmp(method, "GET") == 0) {
        handle_root(sock);
    } else if (strcmp(path, "/api/status") == 0 && strcmp(method, "GET") == 0) {
        handle_status(sock);
    } else if (strcmp(path, "/api/config") == 0 && strcmp(method, "GET") == 0) {
        handle_config_get(sock);
    } else if (strcmp(path, "/api/config") == 0 && strcmp(method, "POST") == 0) {
        handle_config_post(sock, body);
    } else if (strcmp(path, "/api/reconnect") == 0 && strcmp(method, "POST") == 0) {
        handle_reconnect(sock);
    } else {
        handle_404(sock);
    }
    return;

bad:
    http_send(sock, "400 Bad Request", "text/plain", "Bad Request", 11);
}

static void handle_diag(int sock)
{
    char json[4096];
    int len = diag_system_get_json(json, sizeof(json));
    if (len > 0) {
        http_send_json_ok(sock, json);
    } else {
        http_send(sock, "500 Server Error", "text/plain", "Diag failed", 11);
    }
}

static void handle_faults(int sock)
{
    const size_t json_size = 8192;
    char *json = (char *)malloc(json_size);
    if (json == NULL) {
        http_send(sock, "500 Server Error", "text/plain", "No memory", 9);
        return;
    }

    int len = fault_log_get_json(json, json_size);
    if (len > 0) {
        http_send_json_ok(sock, json);
    } else {
        http_send(sock, "500 Server Error", "text/plain", "Fault log failed", 16);
    }
    free(json);
}

/* ================================================================
 * HTTP Server Task
 * ================================================================ */

#define HTTP_RECV_BUF_SIZE 2048

static void webui_mark_alive(void)
{
    s_last_alive_tick = xTaskGetTickCount();
}

static void webui_close_listen_socket(void)
{
    int sock = s_listen_sock;
    if (sock >= 0) {
        s_listen_sock = -1;
        (void)lwip_shutdown(sock, SHUT_RDWR);
        (void)lwip_close(sock);
    }
}

static void webui_close_active_socket(void)
{
    int sock = s_active_sock;
    if (sock >= 0) {
        s_active_sock = -1;
        (void)lwip_shutdown(sock, SHUT_RDWR);
        (void)lwip_close(sock);
    }
}

static void webui_log_fault_limited(fault_log_level_t level, const char *event, const char *detail, int err)
{
    static TickType_t s_last_log_tick = 0;
    static uint32_t s_suppressed = 0;
    TickType_t now = xTaskGetTickCount();

    if (s_last_log_tick == 0 ||
        now - s_last_log_tick >= pdMS_TO_TICKS(WEBUI_FAULT_LOG_INTERVAL_MS) ||
        level == FAULT_LOG_LEVEL_ERROR) {
        fault_log_record(level,
                         "webui",
                         event,
                         "%s err=%d suppressed=%lu",
                         detail != NULL ? detail : "-",
                         err,
                         (unsigned long)s_suppressed);
        s_last_log_tick = now;
        s_suppressed = 0;
    } else {
        s_suppressed++;
    }
}

static void webui_set_socket_timeouts(int sock)
{
    struct timeval tv = {
        .tv_sec = WEBUI_CLIENT_TIMEOUT_MS / 1000,
        .tv_usec = (WEBUI_CLIENT_TIMEOUT_MS % 1000) * 1000,
    };
    (void)lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static int webui_read_request(int sock, char *buf, size_t buf_size)
{
    size_t used = 0;

    if (buf == NULL || buf_size == 0) {
        return -1;
    }

    while (used + 1 < buf_size) {
        int got = lwip_recv(sock, buf + used, (int)(buf_size - used - 1), 0);
        if (got > 0) {
            used += (size_t)got;
            buf[used] = '\0';
            if (strstr(buf, HTTP_CRLF HTTP_CRLF) != NULL) {
                return (int)used;
            }
            continue;
        }

        if (got == 0) {
            return used > 0 ? (int)used : 0;
        }

        if (used > 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return (int)used;
        }
        return -1;
    }

    buf[used] = '\0';
    return (int)used;
}

static void webui_handle_client(int client_sock)
{
    char *recv_buf = malloc(HTTP_RECV_BUF_SIZE);
    if (!recv_buf) {
        webui_log_fault_limited(FAULT_LOG_LEVEL_WARN, "no_mem", "request buffer", 0);
        return;
    }

    webui_set_socket_timeouts(client_sock);

    int recv_len = webui_read_request(client_sock, recv_buf, HTTP_RECV_BUF_SIZE);
    if (recv_len > 0) {
        process_request(client_sock, recv_buf, recv_len);
    } else if (recv_len == 0) {
        ESP_LOGW(TAG, "Client closed before sending request");
        webui_log_fault_limited(FAULT_LOG_LEVEL_WARN, "empty_request", "client closed", 0);
    } else {
        int err = errno;
        ESP_LOGW(TAG, "Failed to receive HTTP request: errno=%d", err);
        webui_log_fault_limited(FAULT_LOG_LEVEL_WARN, "recv_fail", "request receive failed", err);
    }

    free(recv_buf);
}

static esp_err_t webui_run_server_once(void)
{
    int listen_sock = -1;
    struct sockaddr_in server_addr = { 0 };

    listen_sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", listen_sock);
        webui_log_fault_limited(FAULT_LOG_LEVEL_ERROR, "socket_fail", "create listen socket", errno);
        return ESP_FAIL;
    }
    s_listen_sock = listen_sock;

    int opt = 1;
    lwip_setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int flags = lwip_fcntl(listen_sock, F_GETFL, 0);
    if (flags >= 0) {
        (void)lwip_fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(80);

    if (lwip_bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        int err = errno;
        ESP_LOGE(TAG, "Failed to bind port 80");
        webui_log_fault_limited(FAULT_LOG_LEVEL_ERROR, "bind_fail", "port 80", err);
        webui_close_listen_socket();
        return ESP_FAIL;
    }

    if (lwip_listen(listen_sock, 5) < 0) {
        int err = errno;
        ESP_LOGE(TAG, "Failed to listen");
        webui_log_fault_limited(FAULT_LOG_LEVEL_ERROR, "listen_fail", "port 80", err);
        webui_close_listen_socket();
        return ESP_FAIL;
    }

    s_restart_requested = false;
    ESP_LOGI(TAG, "WebUI listening on http://192.168.4.1:80");
    fault_log_record(FAULT_LOG_LEVEL_INFO, "webui", "listening", "port=80");

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (s_running && !s_restart_requested) {
        webui_mark_alive();
        int client_sock = lwip_accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK && err != EINTR && !s_restart_requested) {
                ESP_LOGW(TAG, "HTTP accept failed: errno=%d", err);
                webui_log_fault_limited(FAULT_LOG_LEVEL_WARN, "accept_fail", "accept", err);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        s_active_sock = client_sock;
        webui_mark_alive();
        webui_handle_client(client_sock);
        s_active_sock = -1;
        lwip_close(client_sock);
    }

    webui_close_active_socket();
    webui_close_listen_socket();
    return s_running ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static void webui_task(void *arg)
{
    (void)arg;
    s_running = true;

    while (s_running) {
        webui_mark_alive();
        esp_err_t err = webui_run_server_once();
        if (!s_running) {
            break;
        }

        ESP_LOGW(TAG, "WebUI server restarting: %s", esp_err_to_name(err));
        fault_log_record(FAULT_LOG_LEVEL_WARN, "webui", "restart", "%s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(WEBUI_RESTART_DELAY_MS));
    }

    s_webui_task = NULL;
    vTaskDelete(NULL);
}

static BaseType_t webui_spawn_task(void)
{
    if (s_webui_task != NULL) {
        return pdPASS;
    }

    return xTaskCreatePinnedToCore(webui_task,
                                   "webui",
                                   WEBUI_TASK_STACK_BYTES,
                                   NULL,
                                   WEBUI_TASK_PRIORITY,
                                   &s_webui_task,
                                   APP_CORE_NETWORK);
}

static void webui_monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (!s_running) {
            continue;
        }

        if (s_webui_task == NULL) {
            ESP_LOGW(TAG, "WebUI task missing, respawning");
            fault_log_record(FAULT_LOG_LEVEL_ERROR, "webui", "respawn", "task_missing");
            (void)webui_spawn_task();
            continue;
        }

        TickType_t last_alive = s_last_alive_tick;
        TickType_t now = xTaskGetTickCount();
        if (last_alive != 0 && now - last_alive > pdMS_TO_TICKS(WEBUI_STALE_TIMEOUT_MS)) {
            uint32_t stale_ms = (uint32_t)((now - last_alive) * portTICK_PERIOD_MS);
            ESP_LOGW(TAG, "WebUI task stale for %" PRIu32 " ms, closing sockets", stale_ms);
            fault_log_record(FAULT_LOG_LEVEL_ERROR, "webui", "stale", "ms=%" PRIu32, stale_ms);
            s_restart_requested = true;
            webui_close_active_socket();
            webui_close_listen_socket();
            webui_mark_alive();
        }
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

esp_err_t webui_start(void)
{
    /* Wait for SoftAP to be ready */
    ESP_LOGI(TAG, "Waiting for SoftAP...");
    app_state_snapshot_t state;
    int retries = 80; /* 8 second timeout */
    while (retries-- > 0) {
        app_state_get_snapshot(&state);
        if (state.softap_started) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!state.softap_started) {
        ESP_LOGE(TAG, "SoftAP failed to start!");
        return ESP_FAIL;
    }

    BaseType_t ok = webui_spawn_task();
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create webui task");
        return ESP_FAIL;
    }

    if (s_monitor_task == NULL) {
        ok = xTaskCreatePinnedToCore(webui_monitor_task,
                                     "webui_mon",
                                     WEBUI_MONITOR_STACK_BYTES,
                                     NULL,
                                     WEBUI_MONITOR_PRIORITY,
                                     &s_monitor_task,
                                     APP_CORE_BACKGROUND);
        if (ok != pdPASS) {
            ESP_LOGW(TAG, "Failed to create webui monitor task");
            fault_log_record(FAULT_LOG_LEVEL_WARN, "webui", "monitor_fail", "create_task");
        }
    }

    return ESP_OK;
}
