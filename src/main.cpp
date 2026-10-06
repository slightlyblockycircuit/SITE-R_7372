/*
 * ============================================================
 *  SITE-R 7372 - Regolith Station Life Support Controller
 *  ESP32 Dev Module (38-pin, Robocraze)
 *  Display: I2C 16x2 LiquidCrystal LCD
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ============== PIN MAP ==============
#define PIN_DHT         4
#define PIN_MQ6         34
#define PIN_SOIL        35
#define PIN_LDR         32

#define PIN_LED_IN1     25
#define PIN_LED_PWM     26
#define PIN_MIST        23

#define PIN_BTN_EARTH   13
#define PIN_BTN_MARS    16
#define PIN_BTN_MOON    17

#define PIN_BUZZER      33
#define PIN_PELTIER     19

// ============== PWM CHANNELS ==============
#define PWM_MIST        2
#define PWM_LED         3
#define PWM_BUZZER      4

// ============== CONSTANTS ==============
#define DHT_TYPE        DHT11

#define TEMP_HEAT_ON    20.0
#define TEMP_HEAT_OFF   23.0
#define TEMP_EMERGENCY  35.0

#define MIST_HUM_HIGH   70.0
#define MIST_HUM_LOW    50.0

// Grow-light in EARTH mode: brightness ramps up as ambient light drops.
// ldr >= LED_LDR_OFF (bright) -> LED off; ldr <= LED_LDR_FULL (dark) -> max;
// dimly-lit points in between scale proportionally (progressive increase).
// LDR range: 0 = dark, 4096 = full bright, ~1000-2000 = room bulb ("medium").
// LED_LDR_OFF = 2000 so the LED starts fading in as light drops below the
// room-bulb "medium" level; LED_LDR_FULL = 100 = max power near-dark.
#define LED_LDR_OFF      2000
#define LED_LDR_FULL     100

#define GAS_VERY_BAD    2500

#define SOIL_DRY        2000
#define SOIL_WET        1200

#define MIST_OFF_VAL    0
#define MIST_ON_VAL     255

#define LED_MARS        153
#define LED_MOON        60

#define SENSOR_INTERVAL_MS  2000
#define DISPLAY_INTERVAL_MS 1000
#define HEATER_COOLDOWN_MS  15000
#define BUTTON_DEBOUNCE_MS  300

const char* AP_SSID = "YOUR_SSID";  // set your own
const char* AP_PASS = "YOUR_PASSWORD";  // set your own

// ============== GLOBALS ==============
DHT dht(PIN_DHT, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
AsyncWebServer server(80);

float temp = 25.0;
float hum = 50.0;
int   gas = 0;
int   soil = 0;
int   ldr = 0;

int  mode = 0;
bool heaterState = false;
bool ledState = false;
int  ledBright = 0;   // current PWM written to the grow light (fading position)
int  ledTarget = 0;   // target PWM the LED is fading toward
int  mistState = 0;
bool autoControl = false;   // master AUTO-GUIDANCE switch (OFF by default)
bool ledAuto = true;       // grow-light follows the LDR (per-actuator auto)
bool heaterAuto = true;    // heater follows temperature
bool mistAuto = true;      // mister follows humidity
bool emergency = false;
bool gasEmergency = false;
bool lcdOK = false;
// LCD run-time screen rotator state
#define LCD_SCREENS 6
char lcdLine0[17];
char lcdLine1[17];
int  lcdIdx = 0;
int  lcdReveal = 16;
bool lcdRevealing = false;
bool lcdErasePhase = false;
unsigned long lcdAnimT = 0;

unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastHeaterToggle = 0;
unsigned long lastModeChange = 0;

// ============== BUZZER STATE ==============
bool alarmActive = false;
int  buzzStep = 0;
unsigned long lastBuzzToggle = 0;

// ============== FORWARD DECLARATIONS ==============
void readSensors();
void autoControlLogic();
void checkButtons();
void updateDisplay();
void buildLCDScreen(int idx);
void switchLCDScreen();
void lcdAnimStep();
void bootAnimation();
void centerText(char* out, int limit, const char* text);
const char* gasLabel(int v);
const char* soilLabel(int v);
const char* lightLabel(int v);
void buzzerTone(int freq);
void startupSound();
void updateBuzzer();
void pumpLCD(unsigned long ms);
void warmupDisplay();
void turnLEDOn();
void turnLEDOff();
void turnHeaterOn();
void turnHeaterOff();
void setMist(int m);
void updateLED();
void fadeLED();
void initPWM();
void initLCD();
void initWiFi();
void initWebServer();
String getStatusJSON();
void lcdSetCursor(int row, int col);
void lcdClearRow(int row);
void lcdPrintAt(int row, int col, const char* text);

// ============== HTML DASHBOARD ==============
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SITE-R 7372 // MISSION CONTROL</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#05070f;--panel:#0b1220;--edge:#1c2a44;--txt:#d7e6ff;
  --dim:#6b7fa3;--green:#22e39a;--amber:#ffb020;--red:#ff4157;
  --cyan:#34d8f4;
}
html,body{height:100%}
body{
  font-family:'Consolas','Courier New',monospace;
  background:radial-gradient(circle at 50% 0%,#0d1630 0%,var(--bg) 55%);
  background-attachment:fixed;color:var(--txt);padding:16px;padding-bottom:30px;
}
.wrap{max-width:680px;margin:0 auto}
/* ---- top banner ---- */
.banner{
  display:flex;align-items:center;justify-content:space-between;gap:10px;
  border:1px solid var(--edge);border-radius:10px;background:linear-gradient(180deg,#0f1830,#0a1122);
  padding:12px 16px;margin-bottom:14px;
}
.banner h1{font-size:18px;letter-spacing:3px;color:var(--green);
  text-shadow:0 0 12px rgba(34,227,154,.55)}
.banner .sub{font-size:10px;color:var(--dim);letter-spacing:2px;margin-top:3px}
.statusbox{text-align:right;white-space:nowrap}
.sys{font-size:11px;letter-spacing:2px;margin-bottom:4px}
.sys b{font-size:12px}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:5px;
  background:var(--amber);box-shadow:0 0 8px var(--amber);animation:blink 1s infinite}
.dot.good{background:var(--green);box-shadow:0 0 8px var(--green)}
.dot.bad{background:var(--red);box-shadow:0 0 8px var(--red);animation:none}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.25}}
.link{font-size:10px;color:var(--cyan);letter-spacing:1px}
/* ---- emergency ---- */
.emg{display:none;text-align:center;font-weight:bold;letter-spacing:2px;color:#fff;
  background:linear-gradient(90deg,#7a0f1d,#c11b30,#7a0f1d);border:1px solid #ff4157;
  padding:10px;border-radius:8px;margin-bottom:14px;font-size:13px;
  box-shadow:0 0 18px rgba(255,65,87,.5);animation:blink .6s infinite}
/* alarm light: a red glow falls from above while any sensor is in danger */
.alarmglow{position:fixed;top:0;left:0;right:0;height:45vh;pointer-events:none;
  display:none;z-index:99;
  background:radial-gradient(ellipse 70% 100% at 50% 0%,rgba(255,65,87,.55) 0%,
    rgba(255,65,87,.2) 40%,rgba(255,65,87,0) 72%)}
.alarmglow.on{display:block;animation:glowpulse 1.2s ease-in-out infinite}
@keyframes glowpulse{0%,100%{opacity:.6}50%{opacity:1}}
/* ---- section shell ---- */
.sec{background:var(--panel);border:1px solid var(--edge);border-radius:10px;
  padding:12px;margin-bottom:14px;box-shadow:0 4px 16px rgba(0,0,0,.4)}
.sec h2{font-size:11px;color:var(--dim);letter-spacing:3px;margin-bottom:10px;
  border-bottom:1px solid var(--edge);padding-bottom:7px}
.sec h2 .tag{color:var(--green)}
/* ---- telemetry grid ---- */
.grid{display:flex;flex-wrap:wrap;gap:8px}
.tile{flex:1 1 145px;min-width:105px;background:#0a1322;border:1px solid var(--edge);border-radius:8px;
  padding:9px 8px;text-align:center;position:relative;overflow:hidden}
.tile .l{font-size:9px;color:var(--dim);letter-spacing:1px}
.tile .c{font-size:14px;font-weight:bold;letter-spacing:1px;margin:4px 0 2px;
  font-variant-numeric:tabular-nums;color:var(--green);
  text-shadow:0 0 10px rgba(34,227,154,.45)}
.tile .v{font-size:13px;font-weight:bold;color:var(--cyan);margin:0 0 2px;
  font-variant-numeric:tabular-nums;opacity:.9}
.tile .u{font-size:9px;color:var(--dim)}
.tile.s .c{color:var(--cyan);text-shadow:0 0 10px rgba(52,216,244,.4)}
.tile.w .c{color:var(--amber);text-shadow:0 0 10px rgba(255,176,32,.4)}
.tile.d .c{color:var(--red);text-shadow:0 0 10px rgba(255,65,87,.45)}
.tile.g .v{color:var(--green);text-shadow:0 0 10px rgba(34,227,154,.4)}
.tile.a .v{color:var(--amber);text-shadow:0 0 10px rgba(255,176,32,.4)}
.tile.r .v{color:var(--red);text-shadow:0 0 10px rgba(255,65,87,.45)}
.tile .bar{position:absolute;left:0;bottom:0;height:3px;width:0;background:var(--green);transition:width .6s}
.tile.s .bar{background:var(--cyan)}.tile.w .bar{background:var(--amber)}
.tile.d .bar{background:var(--red)}.tile.a .bar{background:var(--amber)}
/* ---- buttons ---- */
.row{display:flex;gap:8px;flex-wrap:wrap}
.btn{flex:1;min-width:86px;background:#0a1322;color:var(--txt);border:1px solid var(--edge);
  padding:11px 8px;font-family:inherit;font-size:11px;letter-spacing:1px;cursor:pointer;
  border-radius:7px;text-align:center;transition:all .12s;position:relative}
.btn .b{display:block;font-size:11px}
.btn .on{display:block;font-size:8px;color:var(--dim);letter-spacing:2px;margin-top:3px;
  font-variant-numeric:tabular-nums}
.btn:hover{color:#fff;background:#101d33;outline:none}
.btn:focus,.btn:focus-visible{outline:none;box-shadow:none}
.btn:active{transform:scale(.97)}
.btn.on-btn{border-color:var(--green);background:rgba(34,227,154,.12);
  box-shadow:0 0 12px rgba(34,227,154,.25)}
.btn.on-btn .on{color:var(--green)}
.btn.on-btn .ledst{background:var(--green);box-shadow:0 0 8px var(--green)}
.ledst{position:absolute;top:8px;right:8px;width:8px;height:8px;border-radius:50%;
  background:#26324a;transition:all .15s}
/* mode buttons distinct color */
.mode.on-btn{border-color:var(--cyan);background:rgba(52,216,244,.12);box-shadow:0 0 12px rgba(52,216,244,.25)}
/* footer */
.stat{text-align:center;color:var(--dim);font-size:10px;letter-spacing:1px;margin-top:4px}
.stat .l{color:var(--green)}
.conn{color:var(--green)}.conn.off{color:var(--red)}
</style>
</head>
<body>
<div class="alarmglow" id="alarmglow"></div>
<div class="wrap">
  <!-- BANNER -->
  <div class="banner">
    <div>
      <h1>SITE-R 7372</h1>
      <div class="sub">REGO-MARS LIFE SUPPORT BASE | MISSION CONTROL</div>
    </div>
    <div class="statusbox">
      <div class="sys"><span class="dot" id="sysdy"></span><b id="syshw">SYSTEM NOMINAL</b></div>
      <div class="link">TELEMETRY: <span id="trk">LIVE</span> &middot; 2s</div>
      <div class="link">IP <!-- your AP IP --> &middot; <a href="http://yourhost.local" style="color:var(--cyan)">yourhost.local</a></div>
    </div>
  </div>

  <!-- EMERGENCY -->
  <div class="emg" id="emg">! EMERGENCY ! THERMAL LIMIT EXCEEDED &middot; HEATER OFF</div>
  <div class="emg" id="gasemg" style="background:linear-gradient(90deg,#7a3a0f,#c15b10,#7a3a0f);border-color:#ff8820;box-shadow:0 0 18px rgba(255,136,32,.5)">! GAS EMERGENCY ! DEADLY LEVEL DETECTED &middot; VENTILATION PROTOCOL</div>

  <!-- TELEMETRY -->
  <div class="sec">
    <h2><span class="tag">&#9654;</span> HABITAT TELEMETRY</h2>
    <div class="grid">
      <div class="tile" id="ttEMP"><div class="l">TEMP</div><div class="c" id="tc">--</div><div class="v" id="t">--.-</div><div class="u">&deg;C</div><div class="bar"></div></div>
      <div class="tile" id="tHUM"><div class="l">HUMIDITY</div><div class="c" id="hc">--</div><div class="v" id="h">--</div><div class="u">%RH</div><div class="bar"></div></div>
      <div class="tile" id="tGAS"><div class="l">GAS</div><div class="c" id="gc">--</div><div class="v" id="g">--</div><div class="u">/4095</div><div class="bar"></div></div>
      <div class="tile" id="tSOIL"><div class="l">SOIL MOIST</div><div class="c" id="sc">--</div><div class="v" id="s">--</div><div class="u">/4095</div><div class="bar"></div></div>
      <div class="tile" id="tLDR"><div class="l">LIGHT</div><div class="c" id="lc">--</div><div class="v" id="l">--</div><div class="u">/4095</div><div class="bar"></div></div>
    </div>
  </div>

  <!-- MISSION MODE -->
  <div class="sec">
    <h2><span class="tag">&#9654;</span> MISSION MODE</h2>
    <div class="row">
      <button class="btn mode" id="m0" onclick="setMode(0)"><span class="b">EARTH</span><span class="on" id="m0s">OPERATIONAL</span><span class="ledst"></span></button>
      <button class="btn mode" id="m1" onclick="setMode(1)"><span class="b">MARS</span><span class="on" id="m1s">OPERATIONAL</span><span class="ledst"></span></button>
      <button class="btn mode" id="m2" onclick="setMode(2)"><span class="b">MOON</span><span class="on" id="m2s">OPERATIONAL</span><span class="ledst"></span></button>
    </div>
  </div>

  <!-- LIFE SUPPORT -->
  <div class="sec">
    <h2><span class="tag">&#9654;</span> LIFE SUPPORT ACTUATORS</h2>
    <div class="row" style="margin-bottom:8px">
      <button class="btn" id="auto" onclick="toggleAuto()"><span class="b">AUTO-GUIDANCE</span><span class="on" id="autos">OFF</span><span class="ledst"></span></button>
      <button class="btn" id="peltier" onclick="togglePeltier()"><span class="b">PELTIER HEATER</span><span class="on" id="peltiers">STANDBY</span><span class="ledst"></span></button>
      <button class="btn" id="led" onclick="toggle('led')"><span class="b">GROW LIGHT</span><span class="on" id="leds">OFF</span><span class="ledst"></span></button>
    </div>
    <div class="row">
      <button class="btn" id="mist" onclick="toggle('mist')"><span class="b">MIST</span><span class="on" id="mists">OFF</span><span class="ledst"></span></button>
    </div>

    <!-- Rocket throttle: LED brightness control -->
    <div style="margin-top:14px;border-top:1px solid var(--edge);padding-top:12px">
      <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:6px">
        <span style="font-size:10px;color:var(--dim);letter-spacing:2px">ROCKET THROTTLE &middot; LED BRIGHTNESS</span>
        <span style="font-size:12px;color:var(--cyan);font-weight:bold" id="ledlvl">0</span>
      </div>
      <div style="display:flex;align-items:center;gap:10px">
        <span style="font-size:10px;color:var(--amber);letter-spacing:1px">0%</span>
        <input type="range" id="ledslider" min="0" max="255" value="0"
               style="flex:1;height:8px;accent-color:var(--green)">
        <span style="font-size:10px;color:var(--green);letter-spacing:1px">100%</span>
      </div>
      <div style="margin-top:4px;text-align:center;font-size:9px;color:var(--dim);letter-spacing:1px">
        <span style="color:var(--amber)">&#9650;</span> THROTTLE UP <span style="color:var(--green)">&#9654;</span>
      </div>
    </div>
  </div>

  <!-- CHART -->
  <div class="sec" id="chartsec" style="display:none">
    <h2><span class="tag">&#9654;</span> LIVE TELEMETRY CHART</h2>
    <div style="position:relative;width:100%;height:220px;background:#0a1322;border:1px solid var(--edge);border-radius:8px;overflow:hidden">
      <svg id="chart" viewBox="0 0 600 200" preserveAspectRatio="none" style="width:100%;height:100%">
        <!-- grid -->
        <line x1="70" y1="20" x2="70" y2="180" stroke="#1c2a44" stroke-width="1"/>
        <line x1="70" y1="180" x2="590" y2="180" stroke="#1c2a44" stroke-width="1"/>
        <line x1="70" y1="100" x2="590" y2="100" stroke="#1c2a44" stroke-width="0.5" stroke-dasharray="4"/>
        <line x1="70" y1="60" x2="590" y2="60" stroke="#1c2a44" stroke-width="0.5" stroke-dasharray="4"/>
        <line x1="70" y1="140" x2="590" y2="140" stroke="#1c2a44" stroke-width="0.5" stroke-dasharray="4"/>
        <!-- Y labels left (temp) -->
        <text x="6" y="24" fill="#ff6b6b" font-size="10" font-family="monospace">50Â°</text>
        <text x="6" y="104" fill="#ff6b6b" font-size="10" font-family="monospace">25Â°</text>
        <text x="6" y="184" fill="#ff6b6b" font-size="10" font-family="monospace">0Â°</text>
        <!-- Y labels left (light) -->
        <text x="44" y="23" fill="#22e39a" font-size="9" font-family="monospace">4096</text>
        <text x="44" y="103" fill="#22e39a" font-size="9" font-family="monospace">2048</text>
        <text x="44" y="183" fill="#22e39a" font-size="9" font-family="monospace">0</text>
        <!-- Y labels right (hum) -->
        <text x="595" y="24" fill="#34d8f4" font-size="10" font-family="monospace" text-anchor="end">100%</text>
        <text x="595" y="104" fill="#34d8f4" font-size="10" font-family="monospace" text-anchor="end">50%</text>
        <text x="595" y="184" fill="#34d8f4" font-size="10" font-family="monospace" text-anchor="end">0%</text>
        <!-- lines drawn by JS -->
        <polyline id="tempLine" fill="none" stroke="#ff6b6b" stroke-width="2" points=""/>
        <polyline id="humLine" fill="none" stroke="#34d8f4" stroke-width="2" points=""/>
        <polyline id="gasLine" fill="none" stroke="#ffb020" stroke-width="2" points=""/>
        <polyline id="ldrLine" fill="none" stroke="#22e39a" stroke-width="1.5" points=""/>
      </svg>
      <div style="position:absolute;top:6px;left:54px;display:flex;gap:14px;font-size:9px;color:var(--dim);letter-spacing:1px">
        <span style="color:#ff6b6b">&#9644; TEMP</span>
        <span style="color:#34d8f4">&#9644; HUM</span>
        <span style="color:#ffb020">&#9644; GAS</span>
        <span style="color:#22e39a">&#9644; LDR</span>
      </div>
    </div>
  </div>

  <div style="text-align:center;margin-bottom:14px">
    <button class="btn" onclick="toggleChart()" style="min-width:200px;display:inline-block"><span class="b" id="chartBtnText">&#9654; VIEW CHART</span></button>
    <button class="btn" onclick="toggleSound()" style="min-width:200px;display:inline-block"><span class="b" id="sndBtnText">&#9654; SOUND OFF</span></button>
  </div>

  <div class="stat">SITE-R 7372 CONTROL &middot; <span class="l" id="conn">CONNECTED</span> &middot; LAST UPDATE <span id="lastu">--:--</span> &middot; NEXT <span id="cnt">2</span>s</div>
</div>
<script>
var REFRESH_MS = 2000;               // auto-refresh every 2 seconds
var MODES = ['EARTH','MARS','MOON'];
var AUTO = 1;                        // current AUTO-GUIDANCE state (from /data)
function fmtTime(ms){var d=new Date(ms);function p(n){return (n<10?'0':'')+n;}
  return p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());}
function el(id){return document.getElementById(id);}

// ---- web alarm sound ----
// Mirrors the model buzzer: polite 1600Hz triple-beep for normal alarms,
// fast urgent 2200Hz bursts for gas. Audio is opt-in (browser autoplay rule).
var ALARM_ON=false, ALARM_GAS=false, SND_ON=false, actx=null, sirenTimer=null;
function toggleSound(){
  SND_ON=!SND_ON;
  el('sndBtnText').innerHTML=SND_ON?'&#9650; SOUND ON':'&#9654; SOUND OFF';
  if(SND_ON){if(!actx){try{actx=new (window.AudioContext||window.webkitAudioContext)();}catch(e){}}}
  if(actx && actx.state==='suspended'){actx.resume();}
  syncSiren();
}
function beep(freq,dur,vol){
  if(!actx||freq<=0) return;
  var o=actx.createOscillator(), g=actx.createGain();
  o.type='square'; o.frequency.value=freq;
  g.gain.value=vol||0.22;
  o.connect(g); g.connect(actx.destination);
  o.start(); o.stop(actx.currentTime+dur/1000);
}
// Start/stop the siren based on alert state + sound enabled. Uses short
// async delays to roughly match the ESP32 buzzer cadence.
function sirenStep(gasMode){
  if(!SND_ON||!ALARM_ON) return;
  if(gasMode){
    beep(900,200);
    sirenTimer=setTimeout(function(){sirenStep(gasMode);},650);
  } else {
    beep(700,160);
    setTimeout(function(){beep(700,160);},350);
    setTimeout(function(){
      beep(700,160);
      setTimeout(function(){sirenStep(gasMode);},350);
    },700);
  }
}
function syncSiren(){
  if(sirenTimer){clearTimeout(sirenTimer);sirenTimer=null;}
  if(!SND_ON||!ALARM_ON) return;
  sirenStep(ALARM_GAS);
}

// ---- chart state ----
var tempHist=[], humHist=[], gasHist=[], ldrHist=[], MAX_PTS=60, chartVisible=false;
function toggleChart(){
  chartVisible=!chartVisible;
  el('chartsec').style.display=chartVisible?'block':'none';
  el('chartBtnText').innerHTML=chartVisible?'&#9650; HIDE CHART':'&#9654; VIEW CHART';
  try{localStorage.setItem('mtv_chart',chartVisible?'1':'0');}catch(e){}
}
// History lives in localStorage so the scrolling trace survives a browser
// reload instead of restarting from empty (and the chart stays open/closed).
function saveHist(){
  try{localStorage.setItem('mtv_hist',JSON.stringify(
    {t:tempHist,h:humHist,g:gasHist,l:ldrHist}));}catch(e){}
}
function loadHist(){
  try{
    var j=localStorage.getItem('mtv_hist');
    if(j){var o=JSON.parse(j);
      tempHist=o.t||[]; humHist=o.h||[]; gasHist=o.g||[]; ldrHist=o.l||[];
    }
    chartVisible=localStorage.getItem('mtv_chart')==='1';
  }catch(e){}
  el('chartsec').style.display=chartVisible?'block':'none';
  el('chartBtnText').innerHTML=chartVisible?'&#9650; HIDE CHART':'&#9654; VIEW CHART';
}
function drawChart(){
  // Heartbeat/monitor style scrolling trace: newest sample is anchored at the
  // RIGHT edge of the plot area, older samples trail to the LEFT. Every new
  // sample shifts the whole trace one slot left ("moves backwards"), and when
  // values spike high the line jumps up like an ECG peak. Plots TEMP, HUMIDITY,
  // GAS (MQ6) and LIGHT (LDR) on their own scales, updated with live data.
  if(!chartVisible||tempHist.length<2) return;
  var H=160, oy=20;
  var left=70, rightEdge=590;
  var spacing=(rightEdge-left)/(MAX_PTS-1);   // fixed spacing -> constant scroll
  var n=tempHist.length;
  var tp='', hp='', gp='', lp='';
  var yt=oy+H, yh=oy+H, yg=oy+H, maxG=0;
  for(var i=0;i<n;i++) if(gasHist[i]>maxG) maxG=gasHist[i];   // gas scale up to max seen
  if(maxG<500) maxG=500;
  for(var i=0;i<n;i++){
    var x=rightEdge-(n-1-i)*spacing;   // newest at right, older shift left
    if(x<left) break;                   // don't draw past the left axis
    var ty=oy+H-(tempHist[i]/50)*H;     // temp: 0-50Â° scale, higher -> higher
    var hy=oy+H-(humHist[i]/100)*H;     // hum: 0-100% scale
    var gy=oy+H-(gasHist[i]/maxG)*H;    // gas: 0-max scale, higher -> higher
    var ly=oy+H-(ldrHist[i]/4096)*H;    // light: 0-4096 scale, bright -> higher
    tp+=x.toFixed(1)+','+ty.toFixed(1)+' ';
    hp+=x.toFixed(1)+','+hy.toFixed(1)+' ';
    gp+=x.toFixed(1)+','+gy.toFixed(1)+' ';
    lp+=x.toFixed(1)+','+ly.toFixed(1)+' ';
  }
  el('tempLine').setAttribute('points',tp);
  el('humLine').setAttribute('points',hp);
  el('gasLine').setAttribute('points',gp);
  el('ldrLine').setAttribute('points',lp);
}

// ---- sensor category labels & colour (thresholds in /4095 ADC) ----
// Light: LDR read high = bright in this wiring. In EARTH mode the grow light
// turns ON when dim (ldr < LDR_ON) and OFF only when bright (ldr > LDR_OFF),
// with hysteresis to avoid flicker.
function lightCat(v){return v>=3000?['BRIGHT','g']:v>=1500?['MEDIUM','s']:v>=500?['DIM','w']:['DARK','d'];}
// Soil (2-pin analog): lower = wetter, higher = drier.
function soilCat(v){return v>=2000?['DRY','d']:v>=1200?['MEDIUM','w']:['WET','s'];}
// Gas (MQ6): higher = worse air.
function gasCat(v){return v>=2500?['DEADLY','d']:v>=1500?['BAD','w']:v>=800?['MEDIUM','a']:v>=300?['GOOD','s']:['BEST','g'];}
function tempCat(v){return v>=35?['CRITICAL','d']:v>=30?['HIGH','w']:v>=20?['NOMINAL','g']:['COOL','s'];}
function humCat(v){return v>70?['HIGH','w']:v<40?['LOW','w']:['OK','g'];}

async function fetch1(){
  try{
    var r=await fetch('/data');
    var d=await r.json();
    // raw values (smaller, below the label)
    el('t').innerText=d.temp.toFixed(1);
    el('h').innerText=d.hum.toFixed(1);
    el('g').innerText=d.gas;
    el('s').innerText=d.soil;
    el('l').innerText=d.ldr;

    // category labels
    var tC=tempCat(d.temp), hC=humCat(d.hum), gC=gasCat(d.gas),
        sC=soilCat(d.soil), lC=lightCat(d.ldr);
    el('tc').innerText=tC[0]; el('hc').innerText=hC[0]; el('gc').innerText=gC[0];
    el('sc').innerText=sC[0]; el('lc').innerText=lC[0];

    // tile colouring + bar fill
    var ti=[['ttEMP',tC[1]],['tHUM',hC[1]],['tGAS',gC[1]],
            ['tSOIL',sC[1]],['tLDR',lC[1]]];
    for(var i=0;i<ti.length;i++) el(ti[i][0]).className='tile '+ti[i][1];
    var bars=[d.temp/40*100,d.hum,d.gas/4095*100,d.soil/4095*100,d.ldr/4095*100];
    var rows=document.querySelectorAll('.tile .bar');
    for(var k=0;k<rows.length&&k<bars.length;k++) rows[k].style.width=bars[k]+'%';

    // actuator states + status lights
    AUTO=d.auto;
    el('auto').classList.toggle('on-btn',d.auto===1);
    el('autos').innerText=d.auto===1?'AUTO':'MANUAL';
    el('led').classList.toggle('on-btn',d.led===1);
    el('leds').innerText=d.led===1?'ON':'OFF';
    el('mist').classList.toggle('on-btn',d.mist===1);
    el('mists').innerText=d.mist===1?'ON':'OFF';
    el('peltier').classList.toggle('on-btn',d.peltier===1);
    el('peltiers').innerText=d.peltier===1?'FIRING':'STANDBY';

    // rocket throttle reflects live LED level
    el('ledslider').value=d.ledlevel;
    el('ledlvl').textContent=d.ledlevel;

    // highlight the active mission mode
    el('m0').classList.toggle('on-btn',d.mode===0);
    el('m1').classList.toggle('on-btn',d.mode===1);
    el('m2').classList.toggle('on-btn',d.mode===2);

    el('emg').style.display=d.emergency===1?'block':'none';
    el('gasemg').style.display=d.gasEmergency===1?'block':'none';
    // red 'alarm light' glow from above whenever sensor data is in danger range
    ALARM_ON = d.emergency===1||d.gasEmergency===1||d.temp>=35||d.hum>90||d.soil>=3500;
    ALARM_GAS = d.gasEmergency===1;
    el('alarmglow').classList.toggle('on',ALARM_ON);
    syncSiren();
    var dot=el('sysdy'), hw=el('syshw');
    dot.className='dot'+(d.emergency===1?' bad':(d.gasEmergency===1?' bad':(d.temp>=35?'':' good')));
    hw.innerText=d.emergency===1?'SYSTEM CRITICAL':(d.gasEmergency===1?'GAS CRITICAL':(d.temp>=35?'TEMPERATURE HIGH':'SYSTEM NOMINAL'));
    el('trk').innerText='LIVE';
    el('conn').innerText='CONNECTED';
    el('conn').className='conn';
    el('lastu').innerText=fmtTime(Date.now());

    // push data to chart history
    tempHist.push(d.temp); humHist.push(d.hum); gasHist.push(d.gas); ldrHist.push(d.ldr);
    if(tempHist.length>MAX_PTS){tempHist.shift();humHist.shift();gasHist.shift();ldrHist.shift();}
    saveHist();
    drawChart();
  }catch(e){
    el('trk').innerText='LINK LOST';
    el('conn').innerText='RECONNECTING';
    el('conn').className='conn off';
    var dot=el('sysdy');dot.className='dot bad';
    el('syshw').innerText='COMMS LOST';
  }
}
// countdown to next refresh (1s ticks, round-robin with the 2s fetch)
var sec=0;
setInterval(function(){sec=(sec+1)%2;el('cnt').innerText=sec===0?'2':'1';},1000);

async function setMode(m){await fetch('/set?mode='+m);fetch1();}
async function toggleAuto(){await fetch('/set?auto=1');fetch1();}
// Changing any control while AUTO-GUIDANCE is on automatically switches the
// system to MANUAL first, then applies the change.  (AUTO is set to 0 locally
// before the request so a second control tap can't race and re-enable auto.)
async function ensureManual(){if(AUTO===1){AUTO=0;await fetch('/set?auto=1');}}
async function togglePeltier(){await ensureManual();await fetch('/set?toggle=peltier');fetch1();}
async function toggle(n){
  await ensureManual();
  await fetch('/set?toggle='+n);fetch1();
}
// Rocket throttle: set LED brightness 0-255 (auto disables when in use)
async function setLedLevel(v){
  await ensureManual();
  el('ledlvl').textContent=v;
  await fetch('/set?ledlevel='+v);fetch1();
}
el('ledslider').addEventListener('input',function(){setLedLevel(+ledslider.value);});

setInterval(fetch1,REFRESH_MS);   // AUTO-RELOAD every 2s
loadHist();                        // restore chart history + visibility
fetch1();
</script>
</body>
</html>
)rawliteral";

// ============== ACTUATOR HELPERS ==============
void turnLEDOn() {
  ledTarget = 255;
}

void turnLEDOff() {
  ledTarget = 0;
}

void turnHeaterOn() {
  digitalWrite(PIN_PELTIER, HIGH);
  heaterState = true;
}

void turnHeaterOff() {
  digitalWrite(PIN_PELTIER, LOW);
  heaterState = false;
}

void setMist(int m) {
  if (m < 0) m = 0;
  if (m > 1) m = 1;
  ledcWrite(PWM_MIST, m ? MIST_ON_VAL : MIST_OFF_VAL);
  mistState = m;
}

void updateLED() {
  if (!autoControl || !ledAuto) return;   // manual LED: driven only by web button

  // Low-pass filtered LDR for LED control (smooths noise, prevents flicker).
  static int ldrFiltered = 0;
  if (ldrFiltered == 0) ldrFiltered = ldr;
  ldrFiltered = (ldrFiltered * 7 + ldr) / 8;  // ~87% weight on history, ~13% new

  // All modes dim with ambient light; MARS/MOON cap the max brightness.
  int maxB = (mode == 0) ? 255 : (mode == 1 ? LED_MARS : LED_MOON);
  int b;
  if (ldrFiltered >= LED_LDR_OFF)       b = 0;
  else if (ldrFiltered <= LED_LDR_FULL) b = maxB;
  else                                  b = map(ldrFiltered, LED_LDR_FULL, LED_LDR_OFF, maxB, 0);

  ledTarget = b;
}

// Smooth fade â€” runs every loop regardless of auto/manual. Times the ramp
// so a full 0<->255 transition completes in ~0.8s (under 1s). The actual
// step each call is computed from elapsed time, so the fade is smooth and
// consistent no matter how fast the loop runs.
void fadeLED() {
  static unsigned long lastFade = 0;

  // Gas emergency â€” blink LED rapidly, override normal fade
  if (gasEmergency) {
    static bool blinkState = false;
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink >= 200) {
      blinkState = !blinkState;
      lastBlink = millis();
      digitalWrite(PIN_LED_IN1, blinkState ? HIGH : LOW);
      ledcWrite(PWM_LED, blinkState ? 180 : 0);
      ledState = blinkState;
    }
    lastFade = 0;   // reset fade so it resumes smoothly after emergency clears
    return;
  }

  if (ledBright == ledTarget) { lastFade = 0; return; }

  unsigned long now = millis();
  if (lastFade == 0) lastFade = now;
  unsigned long dt = now - lastFade;
  lastFade = now;
  if (dt == 0) return;

  // ramp speed: delta per millisecond (255 in ~800ms)
  int step = (int)((unsigned long)(max(abs(ledTarget - ledBright), 1)) * dt / 800);
  if (step < 1) step = 1;

  if (ledBright < ledTarget)       ledBright += min(ledTarget - ledBright, step);
  else if (ledBright > ledTarget)  ledBright -= min(ledBright - ledTarget, step);

  if (ledBright > 0) {
    digitalWrite(PIN_LED_IN1, HIGH);
    ledcWrite(PWM_LED, ledBright);
  } else {
    digitalWrite(PIN_LED_IN1, LOW);
    ledcWrite(PWM_LED, 0);
  }
  ledState = (ledBright > 0);
}

// ============== INIT FUNCTIONS ==============
void initPWM() {
  pinMode(PIN_LED_IN1, OUTPUT);
  pinMode(PIN_MIST, OUTPUT);
  pinMode(PIN_PELTIER, OUTPUT);
  pinMode(PIN_BTN_EARTH, INPUT_PULLUP);
  pinMode(PIN_BTN_MARS, INPUT_PULLUP);
  pinMode(PIN_BTN_MOON, INPUT_PULLUP);

  digitalWrite(PIN_LED_IN1, LOW);
  digitalWrite(PIN_PELTIER, LOW);

  ledcSetup(PWM_MIST, 5000, 8); ledcAttachPin(PIN_MIST,     PWM_MIST);
  ledcSetup(PWM_LED,  5000, 8); ledcAttachPin(PIN_LED_PWM,  PWM_LED);
  ledcSetup(PWM_BUZZER, 5000, 8); ledcAttachPin(PIN_BUZZER, PWM_BUZZER);
  ledcWrite(PWM_MIST, 0);
  ledcWrite(PWM_LED,  0);
  ledcWrite(PWM_BUZZER, 0);
}

// ============== BUZZER ==============
void buzzerTone(int freq) {
  if (freq <= 0) ledcWrite(PWM_BUZZER, 0);
  else           ledcWriteTone(PWM_BUZZER, (uint32_t)freq);
}

// Cool power-on startup sound (blocking, called from bootAnimation).
void startupSound() {
  int notes[]  = {554,440,207,830,261,130,277,349,698,440,58,103,92,1661,830,415,
                  164,554,293,184,174,130,698,146,261,146,207,277,130,440,415,207,261,277};
  int durs[]   = { 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90,
                   90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90,271};
  int rests[]  = { 10, 10, 10, 20, 10, 10, 10, 10, 10, 20, 10, 10, 10, 10, 10, 20,
                   10, 10, 10, 10,110, 10, 10, 10, 10, 20, 10, 10, 10, 20, 10, 20, 10, 30};
  for (int i = 0; i < 34; i++) {
    buzzerTone(notes[i]);
    delay(durs[i]);
    buzzerTone(0);
    delay(rests[i]);
  }
  buzzerTone(0);
}

// Alarm beeper driven non-blocking in loop(). Two patterns:
//  - Gas emergency: fast urgent triple-beep (200ms on/off)
//  - Other alarms: polite triple-beep cadence
void updateBuzzer() {
  unsigned long now = millis();
  bool alarm = (temp >= TEMP_EMERGENCY) || (hum > 90.0f) || gasEmergency;

  if (alarm) {
    if (!alarmActive) {
      alarmActive = true;
      buzzStep = 0;
      lastBuzzToggle = now;
      buzzerTone(1600);
    }

    if (gasEmergency) {
      // Gas: fast urgent beeps
      static const int gDur[]  = {150, 100, 150, 100, 150, 400};
      static const int gTone[] = {1, 0, 1, 0, 1, 0};
      if (now - lastBuzzToggle >= gDur[buzzStep]) {
        lastBuzzToggle = now;
        buzzStep = (buzzStep + 1) % 6;
        buzzerTone(gTone[buzzStep] ? 2200 : 0);
      }
    } else {
      // Normal: polite triple-beep
      static const int stepDur[] = {120, 90, 120, 90, 120, 430};
      static const int stepTone[] = {1, 0, 1, 0, 1, 0};
      if (now - lastBuzzToggle >= stepDur[buzzStep]) {
        lastBuzzToggle = now;
        buzzStep = (buzzStep + 1) % 6;
        buzzerTone(stepTone[buzzStep] ? 1600 : 0);
      }
    }
  } else if (alarmActive) {
    alarmActive = false;
    buzzStep = 0;
    buzzerTone(0);
  }
}

// Pump the LCD animation for `ms` milliseconds (blocking but animated).
void pumpLCD(unsigned long ms) {
  unsigned long endt = millis() + ms;
  while (millis() < endt) {
    lcdAnimStep();
    delay(8);
  }
}

// Keep the display animating through the 5s sensor warmup (no stuck screen).
void warmupDisplay() {
  for (int i = 0; i < 5; i++) {
    updateDisplay();
    pumpLCD(1000);
  }
}

void initLCD() {
  Wire.begin(21, 22);
  Wire.setClock(100000);
  Wire.beginTransmission(0x27);
  lcdOK = (Wire.endTransmission() == 0);
  lcd.init();
  if (lcdOK) {
    lcd.backlight();
    lcd.clear();
    lcdPrintAt(0, 0, "SITE-R 7372");
    lcdPrintAt(1, 0, "BOOTING...");
  }
}

void initDefaults() {
  mode = 0;           // Earth preset on every boot
  autoControl = false; // AUTO-GUIDANCE OFF by default (manual control first)
}

void initWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);                   // keep radio always on -> faster response
  WiFi.setTxPower(WIFI_POWER_19_5dBm);    // maximum TX power for a stronger signal
  // AP address configurable; also reachable via mDNS
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0)); // change IP as needed
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 4);  // channel 6, not hidden, up to 4 clients
  if (!ok) {
    delay(1000);
    WiFi.mode(WIFI_AP);
    ok = WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 4);
  }

  // mDNS
  if (!MDNS.begin("yourhost")) {
    // try once more with lowercase
    MDNS.begin("yourhost");
  }
  MDNS.addService("http", "tcp", 80);
}

void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "application/json", getStatusJSON());
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *req){
    if (req->hasParam("mode")) {
      int m = req->getParam("mode")->value().toInt();
      if (m >= 0 && m <= 2) {
        mode = m;
        updateLED();
      }
    }
    if (req->hasParam("mist")) {
      int mm = req->getParam("mist")->value().toInt();
      if (mm >= 0 && mm <= 1 && !autoControl) {
        mistAuto = false;
        setMist(mm);
      }
    }
    if (req->hasParam("auto")) {
      autoControl = !autoControl;
      if (autoControl) {
        // Re-enabling AUTO-GUIDANCE restores auto behaviour for all actuators
        // and immediately re-evaluates every actuator from current sensor data.
        ledAuto = heaterAuto = mistAuto = true;
        autoControlLogic();
      }
      updateLED();
    }
    if (req->hasParam("ledlevel")) {
      int lv = req->getParam("ledlevel")->value().toInt();
      if (lv >= 0 && lv <= 255 && !autoControl) {
        ledAuto = false;
        ledTarget = lv;          // set target; fadeLED() ramps smoothly to it
      }
    }
    if (req->hasParam("toggle")) {
      String t = req->getParam("toggle")->value();
      // Manual control drives the actuator directly and pins it manually.
      // While AUTO-GUIDANCE is ON, manual toggles are ignored so auto stays
      // in full control.
      if (autoControl) {
        req->send(200, "text/plain", "AUTO");
        return;
      }
      if (t == "heater" || t == "peltier") {
        heaterAuto = false;
        if (heaterState) turnHeaterOff(); else turnHeaterOn();
      } else if (t == "led") {
        ledAuto = false;
        if (ledState) turnLEDOff(); else turnLEDOn();
      } else if (t == "mist") {
        mistAuto = false;
        if (mistState) setMist(0); else setMist(1);
      }
    }
    req->send(200, "text/plain", "OK");
  });

  server.onNotFound([](AsyncWebServerRequest *req){
    req->send(404, "text/plain", "404 Not Found");
  });

  server.begin();
}

String getStatusJSON() {
  char buf[400];
  snprintf(buf, sizeof(buf),
    "{\"temp\":%.1f,\"hum\":%.1f,\"gas\":%d,\"soil\":%d,\"ldr\":%d,"
    "\"mode\":%d,\"heater\":%d,\"mist\":%d,\"led\":%d,"
    "\"peltier\":%d,\"ledlevel\":%d,\"auto\":%d,\"emergency\":%d,\"gasEmergency\":%d}",
    temp, hum, gas, soil, ldr,
    mode, heaterState?1:0, mistState, ledState?1:0, heaterState?1:0, ledTarget,
    autoControl?1:0, emergency?1:0, gasEmergency?1:0);
  return String(buf);
}

// ============== SENSOR READING ==============
// DHT11 must be sampled at most once per second. We sample exactly once
// per 2s cycle (see loop). The Adafruit DHT lib caches each sample for 2s,
// so the temp + humidity pair below comes from a single sensor read.
void readSensors() {
  // Guard: skip the DHT11 read if a cycle already ran less than 1s ago.
  static unsigned long lastDHT = 0;
  unsigned long now = millis();
  if (now - lastDHT >= 1000) {
    lastDHT = now;
    float t = NAN, h = NAN;
    // Single read attempt (no blocking retry loop -> one sample per cycle).
    h = dht.readHumidity();
    t = dht.readTemperature();
    if (!isnan(t)) temp = temp * 0.7 + t * 0.3;
    if (!isnan(h)) hum = hum * 0.7 + h * 0.3;
  }

  gas  = analogRead(PIN_MQ6);
  soil = analogRead(PIN_SOIL);
  ldr  = analogRead(PIN_LDR);
}

// ============== AUTO CONTROL LOGIC ==============
void autoControlLogic() {
  // Temperature emergency â€” shut everything down
  if (temp >= TEMP_EMERGENCY) {
    emergency = true;
    turnHeaterOff();
    turnLEDOff();
    setMist(0);
    return;
  }
  emergency = false;

  // Gas emergency â€” blink LED, alarm, but don't kill heater/mist
  gasEmergency = (gas >= GAS_VERY_BAD);

  // Each actuator honours its own auto flag plus the master switch.
  if (autoControl && heaterAuto &&
      millis() - lastHeaterToggle > HEATER_COOLDOWN_MS) {
    if (temp < TEMP_HEAT_ON && !heaterState) {
      turnHeaterOn();
      lastHeaterToggle = millis();
    } else if (temp > TEMP_HEAT_OFF && heaterState) {
      turnHeaterOff();
      lastHeaterToggle = millis();
    }
  }

  // Mist: soil irrigation takes priority, then humidity logic
  if (autoControl && mistAuto) {
    if (soil >= SOIL_DRY) {
      setMist(1);            // dry soil â†’ irrigate
    } else if (soil <= SOIL_WET) {
      // soil is wet enough â†’ use humidity logic
      if (hum <= MIST_HUM_LOW) setMist(1);
      else if (hum >= MIST_HUM_HIGH) setMist(0);
    }
    // between SOIL_WET and SOIL_DRY, maintain current state (hysteresis)
  }

  updateLED();
}

// ============== BUTTONS ==============
void checkButtons() {
  if (millis() - lastModeChange < BUTTON_DEBOUNCE_MS) return;

  if (digitalRead(PIN_BTN_EARTH) == LOW) {
    mode = 0;
    lastModeChange = millis();
    updateLED();
  } else if (digitalRead(PIN_BTN_MARS) == LOW) {
    mode = 1;
    lastModeChange = millis();
    updateLED();
  } else if (digitalRead(PIN_BTN_MOON) == LOW) {
    mode = 2;
    lastModeChange = millis();
    updateLED();
  }
}

// ============== LCD DISPLAY ==============
// Builds the text for sensor screen `idx` (no uptime / no actuator info):
// 0 GAS, 1 TEMP, 2 HUMIDITY, 3 SOIL, 4 LIGHT, 5 MODE
// Top row = centred sensor name, bottom row = centred value (+ label for
// gas/soil/light, matching the web categories).
const char* gasLabel(int v)   { return v>=2500 ? "DEADLY" : v>=1500 ? "BAD"   : v>=800 ? "MEDIUM" : v>=300 ? "GOOD" : "BEST"; }
const char* soilLabel(int v)  { return v>=2000 ? "DRY"    : v>=1200 ? "MEDIUM" : "WET"; }
const char* lightLabel(int v) { return v>=3000 ? "BRIGHT" : v>=1500 ? "MEDIUM" : v>=500 ? "DIM" : "DARK"; }

// ============== LCD ORIENTATION ==============
// The LCD is mounted normally (not rotated). Set lcdFlip=true only if you
// mount it upside-down (180deg) so the image reads upright.
#define LCD_COLS 16
bool lcdFlip = false;

void lcdSetCursor(int row, int col) {
  int c = lcdFlip ? (LCD_COLS - 1 - col) : col;
  int r = lcdFlip ? (1 - row) : row;
  lcd.setCursor(c, r);
}

void lcdClearRow(int row) {
  for (int i = 0; i < LCD_COLS; i++) { lcdSetCursor(row, i); lcd.print(' '); }
}

// Flip-safe print of `text` starting at logical (row, col). Writes one char at
// a time so the mirrored column order comes out readable when rotated.
void lcdPrintAt(int row, int col, const char* text) {
  int n = strlen(text);
  for (int i = 0; i < n; i++) { lcdSetCursor(row, col + i); lcd.print(text[i]); }
}

void centerText(char* out, int limit, const char* text) {
  int len = strlen(text);
  int pad = (16 - len) / 2;
  if (pad < 0) pad = 0;
  for (int i = 0; i < 16 && i < limit; i++) {
    if (i < pad)            out[i] = ' ';
    else if (i - pad < len) out[i] = text[i - pad];
    else                    out[i] = ' ';
  }
}

void buildLCDScreen(int idx) {
  char top[17], bot[17];
  switch (idx) {
    case 0: strcpy(top, "GAS");       snprintf(bot, 17, "%s %d",     gasLabel(gas),   gas);  break;
    case 1: strcpy(top, "TEMP");      snprintf(bot, 17, "%.1f C",    temp);                  break;
    case 2: strcpy(top, "HUMIDITY");  snprintf(bot, 17, "%d %%RH",   (int)hum);              break;
    case 3: strcpy(top, "SOIL");      snprintf(bot, 17, "%s %d",     soilLabel(soil), soil); break;
    case 4: strcpy(top, "LIGHT");     snprintf(bot, 17, "%s %d",     lightLabel(ldr), ldr);  break;
    default:
      strcpy(top, "MODE");
      snprintf(bot, 17, "%s", mode == 0 ? "EARTH" : mode == 1 ? "MARS" : "MOON");
      break;
  }
  centerText(lcdLine0, 17, top);
  centerText(lcdLine1, 17, bot);
  lcdLine0[16] = 0;
  lcdLine1[16] = 0;
}

// Starts a wipe animation: erase right->left, then reveal new screen left->right.
void switchLCDScreen() {
  if (emergency) return;
  lcdIdx = (lcdIdx + 1) % LCD_SCREENS;
  lcdErasePhase = true;
  lcdReveal = 15;
  lcdRevealing = true;
}

// Non-blocking animation step (called every loop). ~16ms per column.
void lcdAnimStep() {
  if (!lcdOK || emergency || !lcdRevealing) return;
  unsigned long now = millis();
  if (now - lcdAnimT < 16) return;
  lcdAnimT = now;

  if (lcdErasePhase) {
    // erase right -> left
    if (lcdReveal >= 0) {
      lcdSetCursor(0, lcdReveal); lcd.print(' ');
      lcdSetCursor(1, lcdReveal); lcd.print(' ');
      lcdReveal--;
      if (lcdReveal < 0) {
        lcdErasePhase = false;
        lcdReveal = 0;
        buildLCDScreen(lcdIdx);
      }
    }
  } else {
    // reveal left -> right
    if (lcdReveal < 16) {
      lcdSetCursor(0, lcdReveal); lcd.print(lcdLine0[lcdReveal]);
      lcdSetCursor(1, lcdReveal); lcd.print(lcdLine1[lcdReveal]);
      lcdReveal++;
      if (lcdReveal >= 16) lcdRevealing = false;
    }
  }
}

void updateDisplay() {
  if (!lcdOK) return;
  if (emergency) {
    // blinking emergency banner
    static bool blink = false; blink = !blink;
    if (blink) {
      lcdPrintAt(0, 0, "!! EMERGENCY !!");
      char row1[17];
      snprintf(row1, 17, "T:%.1fC  HEAT:OFF", temp);
      lcdPrintAt(1, 0, row1);
    } else {
      lcdClearRow(0);
      lcdClearRow(1);
    }
    return;
  }
  if (gasEmergency) {
    static bool gBlink = false; gBlink = !gBlink;
    if (gBlink) {
      lcdPrintAt(0, 0, "! GAS EMERG !");
      char row1[17];
      snprintf(row1, 17, "GAS:%d VENT ON", gas);
      lcdPrintAt(1, 0, row1);
    } else {
      lcdClearRow(0);
      lcdClearRow(1);
    }
    return;
  }
  // switch data screen every 0.5s with a wipe animation
  switchLCDScreen();
}

// Cool power-on boot animation (blocking, runs once during startup).
void bootAnimation() {
  if (!lcdOK) return;
  startupSound();
  lcd.clear();

  // 1) type "SITE-R 7372" onto row 0
  char title[] = " SITE-R 7372 "; // 12 chars
  for (int c = 0; c <= 12; c++) {
    String line = String(title).substring(0, c);
    lcdPrintAt(0, 0, line.c_str());
    delay(45);
  }

  // 2) progress bar fills row 1
  for (int p = 0; p <= 16; p++) {
    lcdClearRow(1);
    for (int x = 0; x < 16; x++) { lcdSetCursor(1, x); lcd.print(x < p ? (char)255 : ' '); }
    delay(30);
  }
  delay(200);

  // 3) short system status sequence with a small load bar
  const char* steps[] = {"SYS INIT.....", "DHT11....OK ", "SENSORS...OK ", "NETWORK...OK ", "AUTO-CNT OFF ", "MISSION READY"};
  for (int s = 0; s < 6; s++) {
    lcd.clear();
    lcdPrintAt(0, 0, "> BOOT CHECK");
    lcdPrintAt(1, 0, steps[s]);
    for (int p = 0; p <= 12; p++) {
      lcdPrintAt(0, 0, "> BOOT CHECK");
      for (int x = 12; x < 16; x++) { lcdSetCursor(0, x); lcd.print(x - 12 < p / 4 ? (char)255 : ' '); }
      delay(25);
    }
    delay(100);
  }

  // 4) show first telemetry screen (no stall - warmup display keeps cycling)
  lcdIdx = 0;
  buildLCDScreen(0);
  lcd.clear();
  lcdPrintAt(0, 0, lcdLine0);
  lcdPrintAt(1, 0, lcdLine1);
  lcdReveal = 16;
  lcdRevealing = false;
}

// ============== SETUP ==============
void setup() {
  initPWM();
  dht.begin();
  initLCD();
  bootAnimation();
  initDefaults();
  initWiFi();
  initWebServer();

  // 5s warmup after boot so sensors/peripherals stabilise before first read.
  // Display keeps animating through the telemetry screens during this time.
  warmupDisplay();

  readSensors();
  autoControlLogic();
}

// ============== MAIN LOOP ==============
void loop() {
  unsigned long now = millis();

  // Sensors + auto-control exactly every 2s (DHT11 sampled once per cycle).
  if ((now - lastSensorRead) >= SENSOR_INTERVAL_MS) {
    readSensors();
    autoControlLogic();
    lastSensorRead = now;
  }

  // Grow-light always follows ambient light (unless manually overridden).
  updateLED();
  // Smooth fade toward the LED target every loop (auto + manual).
  fadeLED();

  // Physical buttons polled every loop for instant response (debounced).
  checkButtons();

  // Drive the LCD wipe animation smoothly (non-blocking, ~16ms steps).
  lcdAnimStep();

  // Alarm buzzer (non-blocking) - beeps on temp/humidity/gas danger.
  updateBuzzer();

  // Switch data screen every 1s.
  if ((now - lastDisplayUpdate) >= DISPLAY_INTERVAL_MS) {
    updateDisplay();
    lastDisplayUpdate = now;
  }

  yield();
}
