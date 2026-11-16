/*
 * ESP32 Live Dashboard (local web server)
 * Receives  T=..,P=..,AQ=..,S=..  lines from the STM32 on UART2 (GPIO16 = RX2)
 * and serves a live web page at http://<ESP32-IP>/  (also http://envmonitor.local/)
 * Board: "ESP32 Dev Module". Libraries: none to install (WiFi, WebServer, ESPmDNS are built in).
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";       // <-- edit (2.4 GHz network)
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";   // <-- edit

#define RX2 16
#define TX2 17

WebServer server(80);
float gT = NAN, gP = NAN;
int   gAQ = -1;
char  gS[12] = "NO DATA";
unsigned long lastRx = 0;
String line;
String lineUsb;   // for typing test frames in the Serial Monitor

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Environment Monitor</title>
<style>
:root{--bg:#f4f6f8;--card:#fff;--tx:#1b1f23;--mut:#6a737d}
@media(prefers-color-scheme:dark){:root{--bg:#14171a;--card:#1f2428;--tx:#e6edf3;--mut:#9aa4ad}}
body{margin:0;font-family:system-ui,Arial,sans-serif;background:var(--bg);color:var(--tx)}
.w{max-width:760px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 12px}
#banner{padding:18px;border-radius:12px;text-align:center;font-size:28px;font-weight:700;color:#fff;background:#6a737d}
.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin:12px 0}
.c{background:var(--card);border-radius:12px;padding:14px}
.c small{color:var(--mut)} .c b{display:block;font-size:30px;margin-top:4px}
canvas{width:100%;height:150px;background:var(--card);border-radius:12px;margin-bottom:12px}
#foot{color:var(--mut);font-size:12px}
</style></head><body><div class="w">
<h1>Indoor Environment Monitor</h1>
<div id="banner">NO DATA</div>
<div class="g">
<div class="c"><small>Temperature</small><b id="t">--</b></div>
<div class="c"><small>Pressure</small><b id="p">--</b></div>
<div class="c"><small>Air-quality reading (relative)</small><b id="aq">--</b></div>
</div>
<small>Temperature (last 2 min)</small><canvas id="ct" width="700" height="150"></canvas>
<small>Air-quality reading (last 2 min)</small><canvas id="ca" width="700" height="150"></canvas>
<div id="foot">Updated: --</div></div>
<script>
const H={t:[],a:[]};const col={NORMAL:'#2e9d4f',WARNING:'#e0a400',CRITICAL:'#d6342c'};
function draw(id,arr,lo,hi,c){const cv=document.getElementById(id),x=cv.getContext('2d');
x.clearRect(0,0,cv.width,cv.height);if(arr.length<2)return;
let mn=Math.min(lo,...arr),mx=Math.max(hi,...arr);x.strokeStyle=c;x.lineWidth=2;x.beginPath();
arr.forEach((v,i)=>{const px=i/(120-1)*cv.width,py=cv.height-8-(v-mn)/(mx-mn||1)*(cv.height-16);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke();
x.fillStyle='#888';x.font='12px sans-serif';x.fillText(mx.toFixed(0),4,12);x.fillText(mn.toFixed(0),4,cv.height-2)}
async function tick(){const b=document.getElementById('banner');
try{const r=await fetch('/data');const d=await r.json();
if(!d.ok){b.textContent='NO DATA FROM STM32';b.style.background='#6a737d';return}
b.textContent=d.s;b.style.background=col[d.s]||'#6a737d';
t.textContent=d.t.toFixed(1)+' \u00B0C';p.textContent=d.p.toFixed(0)+' hPa';aq.textContent=d.aq;
H.t.push(d.t);H.a.push(d.aq);if(H.t.length>120){H.t.shift();H.a.shift()}
draw('ct',H.t,20,40,'#1f6feb');draw('ca',H.a,0,4095,'#a371f7');
foot.textContent='Updated: '+new Date().toLocaleTimeString()}
catch(e){b.textContent='ESP32 UNREACHABLE';b.style.background='#6a737d'}}
setInterval(tick,1000);tick();
</script></body></html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleData() {
  bool ok = (lastRx != 0) && (millis() - lastRx < 5000);   // STM32 data fresh within 5 s
  char buf[160];
  if (ok) snprintf(buf, sizeof(buf), "{\"ok\":true,\"t\":%.1f,\"p\":%.0f,\"aq\":%d,\"s\":\"%s\"}", gT, gP, gAQ, gS);
  else    snprintf(buf, sizeof(buf), "{\"ok\":false}");
  server.send(200, "application/json", buf);
}

void parseLine(const String &s) {
  float t, p; int aq; char st[12];
  if (sscanf(s.c_str(), "T=%f,P=%f,AQ=%d,S=%11s", &t, &p, &aq, st) == 4) {
    gT = t; gP = p; gAQ = aq;
    strncpy(gS, st, sizeof(gS) - 1); gS[sizeof(gS) - 1] = 0;
    lastRx = millis();
    Serial.printf("RX: T=%.1f P=%.0f AQ=%d S=%s\n", t, p, aq, gS);
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RX2, TX2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected. Open http://%s/\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin("envmonitor")) Serial.println("Also: http://envmonitor.local/");
  } else {
    Serial.println("\nWi-Fi FAILED - check SSID/password (2.4 GHz only).");
  }

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  server.handleClient();

  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') { line.trim(); parseLine(line); line = ""; }
    else if (c != '\r' && line.length() < 80) line += c;
  }

  // TEST MODE: you can type a frame such as  T=25.0,P=1013,AQ=1200,S=NORMAL  in the Serial Monitor
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { lineUsb.trim(); parseLine(lineUsb); lineUsb = ""; }
    else if (c != '\r' && lineUsb.length() < 80) lineUsb += c;
  }

  static uint32_t lastTry = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastTry > 10000) {   // auto-reconnect
    lastTry = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}
