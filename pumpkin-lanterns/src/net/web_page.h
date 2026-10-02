#pragma once
#include <pgmspace.h>

// Minimal single-page UI. Fetches /api/state every 2 s, posts /api/cmd.
static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Pumpkin</title><style>
body{font:15px system-ui,sans-serif;background:#120a06;color:#f3e6d0;margin:0;padding:16px;max-width:520px;margin:auto}
h1{font-size:20px;margin:0 0 8px;color:#ff8c1a}.card{background:#1f130b;border-radius:10px;padding:12px;margin:10px 0}
button{background:#ff8c1a;color:#120a06;border:0;border-radius:8px;padding:9px 12px;margin:3px;font-weight:600}
button.sec{background:#5a3a1e;color:#f3e6d0}button.on{outline:2px solid #fff}input[type=range]{width:100%}
input[type=text]{background:#120a06;color:#f3e6d0;border:1px solid #5a3a1e;border-radius:6px;padding:7px;width:60%}
.k{color:#b89a78}.row{display:flex;justify-content:space-between;padding:2px 0}#fx button{min-width:30%}a{color:#ff8c1a}
</style></head><body><h1 id=t>Pumpkin</h1>
<div class=card><div class=row><span class=k>Lights</span><span id=lights></span></div>
<div class=row><span class=k>Effect</span><span id=eff></span></div>
<div class=row><span class=k>Scene</span><span id=scene></span></div>
<div class=row><span class=k>Sun / lux</span><span id=sun></span></div>
<div class=row><span class=k>Distance</span><span id=dist></span></div>
<div class=row><span class=k>Weather</span><span id=wx></span></div>
<div class=row><span class=k>LED current / VBUS</span><span id=ma></span></div>
<div class=row><span class=k>Next</span><span id=next></span></div></div>
<div class=card><b>Mode</b><br><button class=sec onclick="cmd('mode',0)" id=m0>Auto</button><button class=sec onclick="cmd('mode',1)" id=m1>Force on</button><button class=sec onclick="cmd('mode',2)" id=m2>Force off</button>
<button onclick="cmd('scare',128)">Scare!</button><button class=sec onclick="cmd('next',0)">Next</button>
<button class=sec id=rot onclick="cmd('rotate',rotv?0:1)">Rotate</button><button class=sec id=amb onclick="cmd('ambient',ambv?0:1)">Ambient</button></div>
<div class=card><b>Effects</b><div id=fx></div></div>
<div class=card><b>Text</b> <input type=text id=txt maxlength=23> <button class=sec onclick="cmd('text',0,txt.value)">Set</button></div>
<div class=card><b>Brightness</b> <span id=bv></span><input type=range min=1 max=255 id=b onchange="cmd('brightness',this.value)">
<b>Speed</b> <span id=sv></span><input type=range min=1 max=255 id=s onchange="cmd('speed',this.value)">
<b>Hue</b> <span id=hv></span><input type=range min=0 max=255 id=h onchange="cmd('hue',this.value)">
<b>Volume</b> <span id=vv></span><input type=range min=0 max=100 id=v onchange="cmd('volume',this.value)"></div>
<div class=card><b>Sounds</b><div id=snd></div></div>
<div class=card id=info class=k></div>
<div class=card class=k><a href=/setup>WiFi / MQTT setup</a> · <a href=/api/state>JSON</a></div>
<script>
let rotv=1,ambv=0,effs=[],busy=0;
function cmd(t,v,s){fetch('/api/cmd?type='+t+'&value='+(v||0)+'&str='+encodeURIComponent(s||'')).then(()=>setTimeout(poll,300))}
function poll(){if(busy)return;busy=1;fetch('/api/state').then(r=>r.json()).then(j=>{busy=0;
document.getElementById('t').textContent=j.name+' ('+(j.leader?'leader':'follower')+' '+(j.slot+1)+'/'+j.nodes+')';
lights.textContent=(j.lightsOn?'ON':'OFF')+' via '+j.source+(j.idle?' · idle':'')+(j.alert?' · alert':'');
eff.textContent=j.effect+(j.scare?' (scare L'+(j.scareLevel+1)+')':'')+' '+Math.round(j.elapsed/1000)+'s';
scene.textContent=j.scene+(j.scaresEnabled?'':' (scares off)');
sun.textContent=(j.sunElev==null?'--':j.sunElev.toFixed(1)+'°')+' / '+(j.lux<0?'--':j.lux.toFixed(0));
dist.textContent=(j.usOk?j.distance.toFixed(0)+' cm':'sensor n/a')+' r='+j.reactivity.toFixed(2)+(j.mic>0?' mic='+j.mic.toFixed(2):'');
wx.textContent=j.wxOk?('wind '+j.windKmh.toFixed(0)+' km/h, rain '+j.rainMm.toFixed(1)+' mm'):'--';
ma.textContent=j.ledMa+' mA @ '+j.brightness+(j.vbus?' / '+(j.vbus/1000).toFixed(2)+' V':'');next.textContent='on '+j.nextSunset+' / off '+j.nextSunrise;
for(let i=0;i<3;i++)document.getElementById('m'+i).className='sec'+(j.mode==i?' on':'');
rotv=j.rotate?1:0;rot.className='sec'+(rotv?' on':'');ambv=j.ambient?1:0;amb.className='sec'+(ambv?' on':'');
if(effs.join()!=j.effects.join()){effs=j.effects;fx.innerHTML=effs.map(e=>'<button class=sec id="fx_'+e+'" onclick="cmd(\'effect\',0,\''+e+'\')">'+e+'</button>').join('')}
effs.forEach(e=>{const b=document.getElementById('fx_'+e);if(b)b.className='sec'+(e==j.effect?' on':'')});
if(document.activeElement.type!='range'){b.value=j.brightness;s.value=j.speed;h.value=j.hue;v.value=j.volume}
if(document.activeElement.id!='txt')txt.value=j.text||'';
bv.textContent=j.brightness;sv.textContent=j.speed;hv.textContent=j.hue;vv.textContent=j.volume;
snd.innerHTML=(j.sounds||[]).map(p=>'<button class=sec onclick="cmd(\'play\',0,\''+p+'\')">'+p.split('/').pop()+'</button>').join('')||'<span class=k>'+(j.audioOk?'none':'no SD/audio')+'</span>';
info.textContent=j.time+' | ip '+j.ip+' rssi '+j.rssi+' | mqtt '+(j.mqtt?'ok':'off')+' | up '+j.uptime+'s heap '+j.heap+' | '+j.fw;
}).catch(()=>{busy=0})}
poll();setInterval(poll,2000);
</script></body></html>)HTML";

// Captive-portal provisioning page (also reachable at /setup when on the LAN).
static const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Pumpkin setup</title><style>body{font:15px system-ui,sans-serif;background:#120a06;color:#f3e6d0;padding:16px;max-width:420px;margin:auto}
h1{color:#ff8c1a;font-size:20px}label{display:block;margin:10px 0 3px;color:#b89a78}input{width:100%%;padding:8px;border-radius:6px;border:1px solid #5a3a1e;background:#1f130b;color:#f3e6d0}
button{background:#ff8c1a;border:0;border-radius:8px;padding:10px 14px;margin-top:14px;font-weight:600}</style></head><body>
<h1>Pumpkin %s — network setup</h1><form method=post action=/setup>
<label>WiFi SSID</label><input name=ssid value="%s"><label>WiFi password</label><input name=pass type=password placeholder="(unchanged)">
<label>MQTT host (blank = off)</label><input name=mh value="%s"><label>MQTT user</label><input name=mu value="%s"><label>MQTT password</label><input name=mp type=password placeholder="(unchanged)">
<button>Save &amp; reboot</button></form><p style="color:#b89a78">Current: %s</p></body></html>)HTML";
