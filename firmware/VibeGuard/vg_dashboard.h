// Dashboard page served at http://192.168.4.1/ . Self-contained (no CDN): the AP has no internet.
// tools/dashboard_mock.py extracts this same string, so the mock and the device serve identical HTML.
#pragma once
#ifndef PROGMEM
#define PROGMEM
#endif

static const char DASHBOARD_HTML[] PROGMEM = R"VGHTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VibeGuard</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#16191d;--mute:#667080;--line:#e2e5ea;--ok:#1f9d6b;--warn:#d98a00;--bad:#d64545;--off:#7a828e;--accent:#2b6cb0;--band:#e8eef7}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1a1e24;--ink:#e8ebef;--mute:#98a1ad;--line:#2a3038;--band:#1f2a38;--accent:#6aa5e8}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;gap:10px;padding:14px 16px;border-bottom:1px solid var(--line);background:var(--card)}
header h1{font-size:18px;margin:0;letter-spacing:.2px}header .sub{color:var(--mute);font-size:13px}
#conn{margin-left:auto;font-size:12px;color:var(--mute)}#conn i{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--off);margin-right:6px}
main{max-width:980px;margin:0 auto;padding:16px;display:grid;gap:14px;grid-template-columns:repeat(12,1fr)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;grid-column:span 12}
@media(min-width:760px){.s4{grid-column:span 4}.s8{grid-column:span 8}.s6{grid-column:span 6}}
h2{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--mute);margin:0 0 10px}
.hero{display:flex;gap:18px;align-items:center}.ring{position:relative;width:120px;height:120px;flex:none}
.ring svg{transform:rotate(-90deg)}.ring b{position:absolute;inset:0;display:grid;place-items:center;font-size:28px}
.badge{display:inline-block;padding:3px 10px;border-radius:999px;font-weight:600;font-size:13px;color:#fff;background:var(--off)}
.fault{font-size:20px;font-weight:600;margin:8px 0 4px}.mute{color:var(--mute);font-size:13px}
.kv{display:grid;grid-template-columns:1fr 1fr;gap:10px}.kv div{border:1px solid var(--line);border-radius:10px;padding:8px 10px}
.kv b{display:block;font-size:20px}.kv span{font-size:12px;color:var(--mute)}
.bar{margin:10px 0}.bar .lbl{display:flex;justify-content:space-between;font-size:13px}
.track{position:relative;height:12px;background:var(--band);border-radius:6px;overflow:hidden;margin-top:4px}
.fill{position:absolute;left:0;top:0;bottom:0;background:var(--ok);border-radius:6px;transition:width .4s}
.tick{position:absolute;top:-2px;bottom:-2px;width:2px}
canvas{width:100%;height:220px;display:block}
.adv{border-left:4px solid var(--off);padding-left:12px}.money{display:flex;gap:16px;flex-wrap:wrap;margin-top:8px;font-size:14px}
button{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:8px;padding:7px 12px;cursor:pointer}
button:hover{border-color:var(--accent)}button.on{background:var(--accent);color:#fff;border-color:var(--accent)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}#sim{display:none}
.prog{height:8px;background:var(--band);border-radius:4px;overflow:hidden;margin-top:8px}.prog i{display:block;height:100%;background:var(--accent);width:0}
</style></head><body>
<header><h1>VibeGuard</h1><span class="sub">Motor vibration monitor</span><span id="conn"><i></i>connecting</span></header>
<main>
<section class="card s8"><h2>Machine health</h2><div class="hero">
<div class="ring"><svg width="120" height="120"><circle cx="60" cy="60" r="52" stroke="var(--band)" stroke-width="12" fill="none"/>
<circle id="arc" cx="60" cy="60" r="52" stroke="var(--ok)" stroke-width="12" fill="none" stroke-linecap="round" stroke-dasharray="0 999"/></svg><b id="health">--</b></div>
<div><span class="badge" id="state">--</span><div class="fault" id="fault">--</div><div class="mute" id="why">Waiting for data</div>
<div id="calbox" style="display:none"><div class="mute" id="caltxt"></div><div class="prog"><i id="calbar"></i></div></div></div></div></section>
<section class="card s4"><h2>Live readings</h2><div class="kv">
<div><b id="rpm">--</b><span>Shaft RPM</span></div><div><b id="f1">--</b><span>1x frequency (Hz)</span></div>
<div><b id="rms">--</b><span>Vibration RMS (mg)</span></div><div><b id="proc">--</b><span>DSP time (us)</span></div></div></section>
<section class="card s6"><h2>Fault bands (energy vs. learned baseline)</h2><div id="bars"></div>
<div class="mute">Orange tick = warning threshold, red tick = alarm threshold (learned per motor).</div></section>
<section class="card s6"><h2>Advice</h2><div class="adv" id="advbox"><div id="action">--</div>
<div class="money"><span>Repair now: <b id="repair">--</b></span><span>If it fails: <b id="failure">--</b></span><span>Saving: <b id="saving">--</b></span></div></div>
<div class="row" style="margin-top:14px"><button id="recal">Re-learn baseline (20 s)</button></div></section>
<section class="card"><h2>Spectrum (last 0.512 s frame)</h2><canvas id="spec"></canvas>
<div class="mute">Shaded: 1x, 2x and 3x lines and the 100-240 Hz bearing band. Resolution 1.95 Hz per bin.</div></section>
<section class="card" id="sim"><h2>Simulation controls</h2><div class="row" id="scen"></div>
<div class="row" style="margin-top:10px"><label class="mute">Severity <input id="sev" type="range" min="0" max="2" step="0.05" value="1"></label><b id="sevv">1.00</b></div></section>
</main>
<script>
const $=id=>document.getElementById(id),C={HEALTHY:'--ok',WARNING:'--warn',ALARM:'--bad',MOTOR_OFF:'--off',CALIBRATING:'--accent'};
const col=v=>getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const NAMES=['Rotor imbalance (1x)','Looseness (2x-3x)','Bearing wear (100-240 Hz)'];
const FN={NONE:'No fault detected',IMBALANCE:'Rotor imbalance',LOOSENESS:'Mechanical looseness',BEARING:'Bearing wear'};
const inr=v=>'₹'+Number(v).toLocaleString('en-IN');
let last=null,spec=null,simBuilt=false;
function lg(r){return Math.max(0,Math.min(1,Math.log(Math.max(r,1))/Math.log(16)))}
function render(s){
 const c=col(C[s.state]||'--off');$('state').textContent=s.state.replace('_',' ');$('state').style.background=c;
 $('health').textContent=s.state=='CALIBRATING'?'--':Math.round(s.health)+'%';
 const L=2*Math.PI*52,h=s.state=='CALIBRATING'?0:s.health/100;$('arc').setAttribute('stroke-dasharray',(L*h)+' 999');$('arc').setAttribute('stroke',c);
 $('fault').textContent=s.state=='CALIBRATING'?'Learning healthy baseline':s.state=='MOTOR_OFF'?'Motor stopped':FN[s.fault];
 const cb=s.state=='CALIBRATING';$('calbox').style.display=cb?'block':'none';
 if(cb){$('caltxt').textContent=`Frame ${s.calib.done} of ${s.calib.total}. Keep the motor running and healthy.`;$('calbar').style.width=(100*s.calib.done/s.calib.total)+'%'}
 let w=0;for(let i=1;i<3;i++)if(s.ratio[i]/s.warn[i]>s.ratio[w]/s.warn[w])w=i;
 $('why').textContent=cb?'':s.state=='MOTOR_OFF'?'Vibration below the running threshold':`Highest band: ${NAMES[w]} at x${s.ratio[w].toFixed(2)} of baseline`;
 $('rpm').textContent=s.rpm||'--';$('f1').textContent=s.f1?s.f1.toFixed(1):'--';$('rms').textContent=s.rmsMg.toFixed(1);$('proc').textContent=s.rt.procUs;
 let html='';for(let i=0;i<3;i++){const r=s.ratio[i],bc=r>=s.alarm[i]?'--bad':r>=s.warn[i]?'--warn':'--ok';
  html+=`<div class="bar"><div class="lbl"><span>${NAMES[i]}</span><b>x${r.toFixed(2)}</b></div><div class="track">
  <div class="fill" style="width:${100*lg(r)}%;background:${col(bc)}"></div>
  <div class="tick" style="left:${100*lg(s.warn[i])}%;background:${col('--warn')}"></div><div class="tick" style="left:${100*lg(s.alarm[i])}%;background:${col('--bad')}"></div></div></div>`}
 $('bars').innerHTML=html;
 $('action').textContent=s.advice.action;$('advbox').style.borderColor=c;
 const f=s.advice.failure>0;$('repair').textContent=f?inr(s.advice.repair):'-';$('failure').textContent=f?inr(s.advice.failure):'-';$('saving').textContent=f?inr(s.advice.saving):'-';
 if(s.sim){$('sim').style.display='block';if(!simBuilt){simBuilt=true;$('scen').innerHTML=['healthy','imbalance','looseness','bearing','motor_off'].map(n=>`<button data-s="${n}">${n.replace('_',' ')}</button>`).join('');
  $('scen').onclick=e=>{const n=e.target.dataset.s;if(n)post('/api/sim?scenario='+n+'&severity='+$('sev').value)}}
  document.querySelectorAll('#scen button').forEach(b=>b.classList.toggle('on',b.dataset.s==s.scenario))}
}
function drawSpec(){const cv=$('spec'),d=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;cv.width=W*d;cv.height=H*d;
 const g=cv.getContext('2d');g.scale(d,d);g.clearRect(0,0,W,H);if(!spec)return;
 const p={l:40,r:8,t:8,b:22},w=W-p.l-p.r,h=H-p.t-p.b,mx=Math.max(5,...spec.mg.slice(2))*1.1,n=spec.mg.length,fmax=n*spec.binHz;
 const X=f=>p.l+w*f/fmax,Y=v=>p.t+h*(1-v/mx);g.fillStyle=col('--band');
 g.fillRect(X(spec.bearing[0]),p.t,X(spec.bearing[1])-X(spec.bearing[0]),h);
 if(spec.f1>0)for(let k=1;k<=3;k++){g.fillStyle=col(k==1?'--ok':'--warn')+'33';g.fillRect(X(k*spec.f1-2.5*spec.binHz),p.t,X(5*spec.binHz)-X(0),h)}
 g.strokeStyle=col('--line');g.fillStyle=col('--mute');g.font='11px system-ui';g.lineWidth=1;
 for(let f=0;f<=250;f+=50){g.beginPath();g.moveTo(X(f),p.t);g.lineTo(X(f),p.t+h);g.stroke();g.fillText(f+' Hz',X(f)-12,H-6)}
 for(let i=0;i<=4;i++){const v=mx*i/4;g.fillText(v.toFixed(v<10?1:0),4,Y(v)+4)}
 g.strokeStyle=col('--accent');g.lineWidth=1.5;g.beginPath();spec.mg.forEach((v,k)=>{k?g.lineTo(X(k*spec.binHz),Y(v)):g.moveTo(X(0),Y(v))});g.stroke();
 g.fillStyle=col('--mute');g.fillText('mg',p.l+4,p.t+12)}
function post(u){fetch(u,{method:'POST'}).then(poll)}
$('recal').onclick=()=>post('/api/recalibrate');$('sev').oninput=e=>$('sevv').textContent=(+e.target.value).toFixed(2);
async function poll(){try{const [s,sp]=await Promise.all([fetch('/api/status').then(r=>r.json()),fetch('/api/spectrum').then(r=>r.json())]);
 last=s;spec=sp;render(s);drawSpec();$('conn').innerHTML='<i style="background:'+col('--ok')+'"></i>live';}
 catch(e){$('conn').innerHTML='<i style="background:'+col('--bad')+'"></i>offline'}}
setInterval(poll,1000);poll();addEventListener('resize',drawSpec);
</script></body></html>)VGHTML";
