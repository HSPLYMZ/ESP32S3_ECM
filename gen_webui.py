# -*- coding: utf-8 -*-
import sys, os
sys.stdout.reconfigure(encoding='utf-8')

HTML = r'''<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,viewport-fit=cover">
<title>ECM 路由器</title>
<style>
:root{--bg:#f5f7fb;--card:rgba(255,255,255,.76);--line:rgba(60,60,67,.12);--text:#111827;--muted:#6b7280;--blue:#007aff;--green:#34c759;--orange:#ff9f0a;--red:#ff3b30;--shadow:0 18px 50px rgba(20,33,61,.13)}
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","PingFang SC","Microsoft YaHei",sans-serif;background:radial-gradient(circle at 12% -6%,#dff0ff 0,#f5f7fb 34%,transparent 35%),linear-gradient(180deg,#fbfdff 0,#eef2f7 100%);color:var(--text);max-width:460px;margin:0 auto;padding:calc(18px + env(safe-area-inset-top)) 16px 34px;min-height:100vh}
body:before{content:"";position:fixed;inset:-80px -80px auto auto;width:220px;height:220px;border-radius:50%;background:rgba(0,122,255,.16);filter:blur(18px);z-index:-1}
header{display:flex;justify-content:space-between;align-items:flex-end;padding:12px 2px 16px}
.kicker{font-size:.78em;color:var(--muted);font-weight:700;letter-spacing:.4px}
h1{font-size:2em;line-height:1.05;font-weight:800;letter-spacing:-1px}
.ver{font-size:.76em;color:#375172;background:rgba(255,255,255,.72);border:1px solid var(--line);border-radius:999px;padding:7px 11px;box-shadow:0 8px 24px rgba(31,41,55,.08)}
nav{display:flex;gap:4px;background:rgba(118,118,128,.12);border:1px solid rgba(255,255,255,.7);border-radius:18px;padding:4px;margin-bottom:16px;backdrop-filter:blur(18px)}
nav button{flex:1;padding:11px;border:0;background:transparent;color:var(--muted);font-size:.92em;font-weight:700;cursor:pointer;border-radius:14px;transition:.18s}
nav button.active{background:#fff;color:var(--text);box-shadow:0 5px 18px rgba(31,41,55,.12)}
.card{background:var(--card);backdrop-filter:blur(22px);border:1px solid rgba(255,255,255,.82);border-radius:28px;padding:18px;margin-bottom:14px;box-shadow:var(--shadow)}
.hero{display:flex;justify-content:space-between;gap:16px;overflow:hidden;position:relative}
.hero:after{content:"";position:absolute;right:-34px;bottom:-42px;width:140px;height:140px;border-radius:50%;background:rgba(52,199,89,.12)}
.eyebrow,.card-title{font-size:.74em;color:var(--muted);font-weight:800;text-transform:uppercase;letter-spacing:1.2px;margin-bottom:10px}
.hero-title{font-size:2.2em;font-weight:850;letter-spacing:-1.4px}
.hero-sub{margin-top:6px;color:var(--muted);font-size:.9em;line-height:1.45;max-width:260px}
.orb{width:82px;height:82px;border-radius:24px;background:linear-gradient(145deg,#34c759,#00c7be);box-shadow:0 16px 35px rgba(52,199,89,.35);display:grid;place-items:center;flex:0 0 auto;z-index:1}
.orb span{width:34px;height:34px;border-radius:50%;background:#fff;box-shadow:inset 0 0 0 10px rgba(255,255,255,.45)}
.orb.warn{background:linear-gradient(145deg,#ff9f0a,#ffcc00);box-shadow:0 16px 35px rgba(255,159,10,.32)}
.orb.bad{background:linear-gradient(145deg,#ff3b30,#ff6b6b);box-shadow:0 16px 35px rgba(255,59,48,.28)}
.quickgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px}
.mini{background:rgba(255,255,255,.68);border:1px solid rgba(255,255,255,.86);border-radius:22px;padding:14px;box-shadow:0 12px 30px rgba(31,41,55,.08)}
.mini .mttl{font-size:.76em;color:var(--muted);font-weight:750}.mini .mval{margin-top:6px;font-size:1.2em;font-weight:800;word-break:break-all}
.uplink{display:grid;grid-template-columns:repeat(5,1fr);gap:7px}
.step{display:flex;flex-direction:column;align-items:center;gap:7px;font-size:.72em;padding:10px 5px;border-radius:18px;background:rgba(118,118,128,.09);color:var(--muted);font-weight:800;transition:.25s}
.step .dot{width:11px;height:11px;border-radius:50%;background:rgba(118,118,128,.3)}
.step.on{background:rgba(52,199,89,.14);color:#147a35}.step.on .dot{background:var(--green);box-shadow:0 0 0 5px rgba(52,199,89,.16)}
.step.warn{background:rgba(255,159,10,.15);color:#a15b00}.step.warn .dot{background:var(--orange);box-shadow:0 0 0 5px rgba(255,159,10,.14)}
.arrow{display:none}
.row{display:flex;justify-content:space-between;gap:16px;padding:12px 0;border-bottom:1px solid var(--line);font-size:.92em;line-height:1.45}
.row:last-child{border:none}.lbl{color:var(--muted);font-weight:650}.val{text-align:right;word-break:break-word;max-width:62%;font-weight:700}
.temp{display:inline-flex;align-items:baseline;gap:6px;font-size:2.35em;font-weight:850;letter-spacing:-1.5px;color:var(--green)}
.temp:after{content:"设备温度";font-size:.32em;color:var(--muted);font-weight:700;letter-spacing:0}.temp.warn{color:var(--red)}
.errbox{background:rgba(255,59,48,.1);color:#b42318;padding:13px 15px;border-radius:18px;margin:0 0 14px;font-size:.86em;display:none;word-break:break-all;border:1px solid rgba(255,59,48,.16)}
.sigbars{display:inline-flex;gap:3px;align-items:flex-end;height:16px;vertical-align:-2px;margin-right:7px}
.sigbar{width:4px;background:rgba(118,118,128,.24);border-radius:3px}.sigbar:nth-child(1){height:6px}.sigbar:nth-child(2){height:9px}.sigbar:nth-child(3){height:12px}.sigbar:nth-child(4){height:15px}
.sigbar.a{background:var(--green)}.sigbar.w{background:var(--orange)}
input,select{width:100%;padding:15px 14px;margin-bottom:12px;background:rgba(255,255,255,.72);border:1px solid var(--line);border-radius:17px;color:var(--text);font-size:1em;font-family:inherit;outline:none}
input:focus,select:focus{border-color:rgba(0,122,255,.55);box-shadow:0 0 0 4px rgba(0,122,255,.11)}
.btn{width:100%;padding:15px;border:0;border-radius:18px;font-size:1em;cursor:pointer;margin-bottom:10px;font-family:inherit;font-weight:800;transition:.15s}
.btn:active{transform:scale(.985)}.btn-go{background:var(--blue);color:#fff;box-shadow:0 13px 28px rgba(0,122,255,.24)}.btn-grey{background:rgba(118,118,128,.14);color:var(--text)}
.toast{position:fixed;top:calc(16px + env(safe-area-inset-top));left:50%;transform:translateX(-50%);padding:12px 22px;border-radius:999px;font-size:.9em;z-index:99;display:none;white-space:nowrap;font-weight:800;box-shadow:0 16px 40px rgba(31,41,55,.18)}
.toast.ok{background:#111827;color:#fff}.toast.err{background:var(--red);color:#fff}
.footer{padding:10px 6px 0;text-align:center;color:var(--muted);font-size:.78em;line-height:1.8}
.footer a{color:var(--blue);font-weight:800;text-decoration:none}
@media(max-width:380px){body{padding-left:12px;padding-right:12px}.hero-title{font-size:1.8em}.quickgrid{grid-template-columns:1fr}.uplink{grid-template-columns:repeat(3,1fr)}}
</style>
</head>
<body>
<header>
<div><div class="kicker">ECM Router</div><h1>控制中心</h1></div>
<div class="ver" id="ver">V1.3.0</div>
</header>
<nav>
<button id="btnDash" class="active" onclick="showTab(0)">状态</button>
<button id="btnSet" onclick="showTab(1)">设置</button>
</nav>
<div id="dash">
<div class="card hero">
<div>
<div class="eyebrow">蜂窝热点</div>
<div class="hero-title" id="heroTitle">正在连接</div>
<div class="hero-sub" id="heroSub">等待设备状态同步</div>
</div>
<div class="orb warn" id="heroOrb"><span></span></div>
</div>
<div class="quickgrid">
<div class="mini"><div class="mttl">4G 信号</div><div class="mval" id="miniSig">--</div></div>
<div class="mini"><div class="mttl">客户端</div><div class="mval" id="miniSta">--</div></div>
<div class="mini"><div class="mttl">ECM IP</div><div class="mval" id="miniIp">--</div></div>
<div class="mini"><div class="mttl">温度</div><div class="mval" id="miniTemp">--</div></div>
</div>
<div class="card">
<div class="card-title">链路状态</div>
<div class="uplink" id="uplink"></div>
</div>
<div class="card">
<div class="card-title">4G 网络</div>
<div id="net4g"></div>
</div>
<div class="card">
<div class="card-title">Wi-Fi</div>
<div id="wifi"></div>
</div>
<div class="card">
<div class="card-title">芯片温度</div>
<div id="thermal"></div>
</div>
<div class="errbox" id="errbox"></div>
</div>
<div id="set" hidden>
<div class="card">
<div class="card-title">Wi-Fi 设置</div>
<input id="cfgSSID" placeholder="SSID" maxlength="32">
<input id="cfgPass" type="password" placeholder="密码" maxlength="63">
<select id="cfgCh">
<option value="1">信道 1</option>
<option value="6">信道 6</option>
<option value="11">信道 11</option>
</select>
</div>
<div class="card">
<div class="card-title">APN 设置</div>
<select id="cfgAPN" onchange="apnChg()">
<option value="0">中国移动 (cmnet)</option>
<option value="1">中国联通 (3gnet)</option>
<option value="2">中国电信 (ctnet)</option>
<option value="3">自定义 APN</option>
</select>
<input id="cfgCustom" placeholder="自定义 APN" maxlength="31">
</div>
<button class="btn btn-go" onclick="doSave()">保存并应用</button>
<button class="btn btn-grey" onclick="doReconn()">重连蜂窝网络</button>
</div>
<footer class="footer">作者邮箱 hsp2028.163.com<br>GitHub 地址 <a href="https://github.com/HSPLYMZ/ESP32S3_ECM">HSPLYMZ/ESP32S3_ECM</a></footer>
<div class="toast" id="toast"></div>
<script>
var gPollTimer=0;
function Q(s){return document.getElementById(s);}
function showTab(n){
Q('dash').hidden=n!==0;Q('set').hidden=n===0;
Q('btnDash').className=n===0?'active':'';
Q('btnSet').className=n===1?'active':'';
if(n===0)poll();else lcfg();
}
function apnChg(){Q('cfgCustom').hidden=Q('cfgAPN').value!=='3';}
function toast(msg,ok){
var e=Q('toast');e.textContent=msg;
e.className='toast '+(ok?'ok':'err');e.style.display='block';
setTimeout(function(){e.style.display='none'},2500);
}
function buildUplink(s){
var steps=[
['USB','USB',s.cellular.usb],
['AT','AT',s.cellular.at],
['ECM','ECM',s.cellular.uplink],
['IP','IP',!!(s.cellular.ip&&s.cellular.ip!=='--')],
['NAT','NAT',s.cellular.napt]
];
var h='';
for(var i=0;i<steps.length;i++){
var st=steps[i];
var cls='step';
if(st[2])cls+=' on';
else if(i>0&&steps[i-1][2])cls+=' warn';
h+='<div class="'+cls+'"><span class="dot"></span><span class="lbl">'+st[1]+'</span></div>';
}
return h;
}
function sigbars(csq){
var v=csq?parseInt(csq):0;if(isNaN(v))v=0;
var lv=v<10?0:v<16?1:v<22?2:v<26?3:4;
var s='<span class=sigbars>';
for(var i=0;i<4;i++){
var c=i<lv?'sigbar '+(lv<2?'w':'a'):'sigbar';
s+='<span class="'+c+'"></span>';
}
s+='</span>';return s;
}
function updateHero(s){
var ok=s.cellular.usb&&s.cellular.at&&s.cellular.uplink&&s.cellular.napt;
var ip=s.cellular.ip&&s.cellular.ip!=='--'?s.cellular.ip:'等待 IP';
Q('heroTitle').textContent=ok?'在线':'检查中';
Q('heroSub').textContent=ok?'热点已通过 ECM 转发，IP '+ip:esc(s.cellular.dial||'链路正在建立');
Q('heroOrb').className='orb '+(ok?'':(s.cellular.error&&s.cellular.error!=='--'?'bad':'warn'));
}
function render(s){
Q('ver').textContent=s.ver;
updateHero(s);
Q('uplink').innerHTML=buildUplink(s);
Q('miniSig').innerHTML=sigbars(s.cellular.signal)+esc(s.cellular.signal);
Q('miniSta').textContent=s.softap.clients+' / 2';
Q('miniIp').textContent=s.cellular.ip||'--';
Q('miniTemp').textContent=s.thermal.temp.toFixed(1)+' °C';
var net=sigbars(s.cellular.signal)+'<b>'+esc(s.cellular.signal)+'</b>';
Q('net4g').innerHTML=
'<div class="row"><span class="lbl">信号强度</span><span class="val">'+net+'</span></div>'
+'<div class="row"><span class="lbl">SIM 卡</span><span class="val">'+esc(s.cellular.sim)+'</span></div>'
+'<div class="row"><span class="lbl">网络注册</span><span class="val">'+esc(s.cellular.cereg)+'</span></div>'
+'<div class="row"><span class="lbl">分组附着 CGATT</span><span class="val">'+esc(s.cellular.cgatt)+'</span></div>'
+'<div class="row"><span class="lbl">运营商</span><span class="val">'+esc(s.cellular.network)+'</span></div>'
+'<div class="row"><span class="lbl">拨号状态</span><span class="val">'+esc(s.cellular.dial)+'</span></div>'
+'<div class="row"><span class="lbl">累计重连 / 失败</span><span class="val">'+s.cellular.reconnects+' / '+s.cellular.failures+'</span></div>'
+'<div class="row"><span class="lbl">最近失败原因</span><span class="val">'+esc(s.cellular.last_failure)+'</span></div>';
Q('wifi').innerHTML=
'<div class="row"><span class="lbl">SSID</span><span class="val">'+esc(s.softap.ssid)+'</span></div>'
+'<div class="row"><span class="lbl">信道</span><span class="val">'+s.softap.channel+'</span></div>'
+'<div class="row"><span class="lbl">已连接</span><span class="val">'+s.softap.clients+' / 2</span></div>'
+'<div class="row"><span class="lbl">ECM IP</span><span class="val">'+esc(s.cellular.ip)+'</span></div>'
+'<div class="row"><span class="lbl">DNS</span><span class="val">'+esc(s.cellular.dns)+'</span></div>';
var t=s.thermal;
Q('thermal').innerHTML='<span class="temp'+(t.warn?' warn':'')
+'">'+t.temp.toFixed(1)+' °C</span>';
var eb=Q('errbox');var ce=s.cellular.error;
if(ce&&ce!=='--'){eb.textContent='错误: '+ce;eb.style.display='block';}
else{eb.style.display='none';}
}
function esc(s){
if(!s||s==='--')return'--';
var d=document.createElement('div');d.textContent=s;return d.innerHTML;
}
function poll(){
clearTimeout(gPollTimer);
fetch('/api/status').then(function(r){return r.json();}).then(function(j){
render(j);gPollTimer=setTimeout(poll,4000);
}).catch(function(){gPollTimer=setTimeout(poll,8000);});
}
function lcfg(){
fetch('/api/config').then(function(r){return r.json();}).then(function(j){
Q('cfgSSID').value=j.ssid||'';Q('cfgPass').value=j.password||'';
Q('cfgCh').value=j.channel;Q('cfgAPN').value=j.apn_profile;
Q('cfgCustom').value=j.custom_apn||'';apnChg();
});}
function doSave(){
var b='ssid='+encodeURIComponent(Q('cfgSSID').value)
+'&password='+encodeURIComponent(Q('cfgPass').value)
+'&channel='+Q('cfgCh').value+'&apn_profile='+Q('cfgAPN').value
+'&custom_apn='+encodeURIComponent(Q('cfgCustom').value);
fetch('/api/config',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
.then(function(r){return r.json();}).then(function(j){
toast(j.ok?'已保存，正在重启服务...':'错误: '+j.error,j.ok);
if(j.ok)setTimeout(function(){poll();},3000);
});}
function doReconn(){
fetch('/api/reconnect',{method:'POST'}).then(function(r){return r.json();})
.then(function(j){toast(j.ok?'正在重连...':'错误: '+j.error,j.ok);});
}
poll();
</script>
</body>
</html>'''

def to_c_string(s):
    lines = s.split('\n')
    result = []
    for line in lines:
        escaped = line.replace('\\', '\\\\').replace('"', '\\"')
        result.append('"' + escaped + '"')
    return '\n'.join(result)

html_c = to_c_string(HTML)

# Read existing webui.c, find and replace HTML section
webui_path = r"D:\HSP\esp_projects\ESP32S3_ECM_V1\main\webui.c"
with open(webui_path, "r", encoding="utf-8") as f:
    content = f.read()

start = content.find('static const char INDEX_HTML[] =')
end = content.find('/* ===', start)

new_code = 'static const char INDEX_HTML[] =\n' + html_c + ';\n'
content = content[:start] + new_code + content[end:]

with open(webui_path, "w", encoding="utf-8", newline="\n") as f:
    f.write(content)

# Verify encoding
with open(webui_path, "rb") as f:
    data = f.read()
    # Check that Chinese chars are proper UTF-8, not ?
    idx = data.find(b'<title>ECM')
    if idx >= 0:
        chunk = data[idx:idx+30]
        # Check if we have proper UTF-8 (bytes > 0x7F) or just ? (0x3F)
        has_mb = any(b > 0x7F for b in chunk)
        has_qm = chunk.count(0x3F)
        print(f"Title bytes: {' '.join(f'{b:02X}' for b in chunk)}")
        print(f"Multi-byte chars: {has_mb}, Question marks: {has_qm}")
        if not has_mb:
            print("ERROR: Chinese characters replaced with ?!")
        else:
            print("OK: Chinese characters properly UTF-8 encoded")
