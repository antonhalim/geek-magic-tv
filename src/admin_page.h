#pragma once
#include <pgmspace.h>

static const char ADMIN_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYBERCLOCK</title>
<style>
*{box-sizing:border-box}
body{margin:0;padding:16px;background:#05080a;color:#cfe;font:14px/1.5 ui-monospace,Menlo,Consolas,monospace;max-width:480px;margin:auto}
h1{color:#00f0ff;font-size:20px;letter-spacing:4px;margin:8px 0 16px;text-shadow:0 0 8px #00f0ff88}
section{border:1px solid #00f0ff;padding:12px;margin-bottom:16px;box-shadow:0 0 6px #00f0ff44}
h2{margin:0 0 8px;font-size:11px;letter-spacing:3px;text-transform:uppercase;color:#ff2bd6}
label,.k{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:#7aa}
.v{color:#fcee0a}
input[type=text]{width:100%;background:#000;color:#00f0ff;border:1px solid #ff2bd6;padding:8px;font:inherit}
button{background:#000;color:#00f0ff;border:1px solid #00f0ff;padding:8px 12px;font:inherit;letter-spacing:1px;cursor:pointer}
button.on{background:#00f0ff;color:#000}
.seg{display:flex;gap:8px;margin:4px 0 12px}.seg button{flex:1}
#res button{display:block;width:100%;text-align:left;margin-top:6px;border-color:#ff2bd6;color:#cfe}
input[type=range]{width:100%;accent-color:#ff2bd6}
a{color:#fcee0a}
p{margin:8px 0 0;font-size:12px;color:#7aa}
</style></head><body>
<h1>CYBERCLOCK</h1>
<section><h2>Status</h2><div id="st">...</div></section>
<section><h2>Location</h2>
<div class="k">Current</div><div class="v" id="city">-</div>
<div class="k" style="margin-top:8px">Search city</div>
<input type="text" id="q" placeholder="Type, press enter" maxlength="64">
<div id="res"></div></section>
<section><h2>Display</h2>
<div class="k">Unit</div><div class="seg" id="unit"><button data-v="C">&deg;C</button><button data-v="F">&deg;F</button></div>
<div class="k">Face</div><div class="seg" id="face"><button data-v="0">NETRUNNER</button><button data-v="1">TERMINAL</button></div>
<div class="k">Brightness <span class="v" id="bv"></span></div>
<input type="range" id="br" min="0" max="100"></section>
<section><h2>Firmware</h2><a href="/update">/update</a>
<p>To revert to stock, upload GeekMagic's official SmallTV-Ultra firmware .bin</p></section>
<script>
const $=id=>document.getElementById(id);let t;
const post=o=>fetch('/api/config',{method:'POST',body:JSON.stringify(o)}).then(r=>{if(!r.ok)throw 0;return r.json()}).then(show).catch(()=>fetch('/api/config').then(r=>r.json()).then(show));
function mark(id,v){for(const b of $(id).children)b.classList.toggle('on',b.dataset.v==v)}
function show(c){$('city').textContent=c.city;mark('unit',c.unit);mark('face',c.face);if(document.activeElement!==$('br')&&!t)$('br').value=c.brightness;$('bv').textContent=c.brightness}
for(const id of['unit','face'])$(id).onclick=e=>{const v=e.target.dataset.v;if(v!=null)post({[id]:id=='face'?+v:v})};
$('br').oninput=e=>{$('bv').textContent=e.target.value;clearTimeout(t);t=setTimeout(()=>{t=0;post({brightness:+e.target.value})},300)};
const ab=s=>Object.fromEntries(s.split(',').map(p=>p.split(':')));
const US=ab('Alabama:AL,Alaska:AK,Arizona:AZ,Arkansas:AR,California:CA,Colorado:CO,Connecticut:CT,Delaware:DE,District of Columbia:DC,Florida:FL,Georgia:GA,Hawaii:HI,Idaho:ID,Illinois:IL,Indiana:IN,Iowa:IA,Kansas:KS,Kentucky:KY,Louisiana:LA,Maine:ME,Maryland:MD,Massachusetts:MA,Michigan:MI,Minnesota:MN,Mississippi:MS,Missouri:MO,Montana:MT,Nebraska:NE,Nevada:NV,New Hampshire:NH,New Jersey:NJ,New Mexico:NM,New York:NY,North Carolina:NC,North Dakota:ND,Ohio:OH,Oklahoma:OK,Oregon:OR,Pennsylvania:PA,Rhode Island:RI,South Carolina:SC,South Dakota:SD,Tennessee:TN,Texas:TX,Utah:UT,Vermont:VT,Virginia:VA,Washington:WA,West Virginia:WV,Wisconsin:WI,Wyoming:WY,Puerto Rico:PR');
const CA=ab('Alberta:AB,British Columbia:BC,Manitoba:MB,New Brunswick:NB,Newfoundland and Labrador:NL,Nova Scotia:NS,Ontario:ON,Prince Edward Island:PE,Quebec:QC,Saskatchewan:SK,Northwest Territories:NT,Nunavut:NU,Yukon:YT');
function region(g){if(g.country=='US')return US[g.admin1]||'US';if(g.country=='CA')return CA[g.admin1]||'CA';return g.country}
$('q').onkeydown=async e=>{
if(e.key!='Enter')return;
const r=$('res');r.textContent='...';
const a=await fetch('/api/geocode?q='+encodeURIComponent(e.target.value)).then(r=>r.json()).catch(()=>[]);
r.textContent=a.length?'':'No results';
for(const g of a){const b=document.createElement('button');
const rg=region(g),name=(g.name+(rg?', '+rg:'')).slice(0,47);
b.textContent=g.name+(g.admin1?', '+g.admin1:'')+' ('+g.country+')';
b.onclick=()=>{post({city:name,lat:g.lat,lon:g.lon});r.textContent='';$('q').value=''};
r.appendChild(b)}};
function poll(){fetch('/api/status').then(r=>r.json()).then(s=>{
const w=s.weather;
$('st').textContent=s.ip+' | '+s.rssi+'dBm | up '+s.uptime+'s | heap '+s.heap+' | '+(w.valid?w.tempC+'C code '+w.code+' age '+w.age+'s':'no weather')
}).catch(()=>{})}
fetch('/api/config').then(r=>r.json()).then(show);
poll();setInterval(poll,10000);
</script></body></html>
)HTML";
