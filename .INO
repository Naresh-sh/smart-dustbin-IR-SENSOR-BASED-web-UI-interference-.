/*
  Smart Dustbin - AP Mode (Hotspot)
  - AP SSID: Dustbin_AP (password 12345678)
  - Web UI: http://192.168.4.1 (password protection)
  - Pins: SERVO -> D4(GPIO2), IR -> D5(GPIO14)
  - Features:
    * IR debounce + edge detection
    * Smooth servo movement
    * NTP time sync (tries)
    * Fast UI polling (500ms)
    * JSON endpoints: /status, /open, /close
    * Password protection for web UI (password: 12345678)
    * Fill level starts at 3%, increases by 3% each open (capped at 100%)
    * Servo control via IR sensor (open on detect, close on release)
    * Persistence with EEPROM for fill/open/close counts
    * "Powered by naresh" shown in UI
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <EEPROM.h>
#include <time.h>

// ----------------- CONFIG -----------------
const char* AP_SSID = "very very smart dustbin";
const char* AP_PASS = "password"; // must be >=8 chars
const int HTTP_PORT = 80;
const char* UI_PASSWORD = "password";

// Timezone (IST): +5:30 -> seconds = 5*3600 + 30*60
const long GMT_OFFSET_SEC = 5L * 3600 + 30L * 60;
const int DAYLIGHT_OFFSET_SEC = 0;

// Pins
#define SERVO_PIN 2   // D4
#define IR_PIN    14  // D5

// IR debounce & timings
const unsigned long IR_DEBOUNCE_MS = 80UL;

// Servo angles & smoothing
const int SERVO_OPEN_ANGLE  = 140;
const int SERVO_CLOSE_ANGLE = 20;
const int SERVO_STEP_DEG    = 3;
const int SERVO_STEP_DELAY_MS = 10;

// Fill/increment rules
const int FILL_START = 3;
const int FILL_STEP  = 3;
const int FILL_MAX   = 100;

// EEPROM layout (small, persistent)
const int EEPROM_SIZE = 64;
const int ADDR_MAGIC   = 0; // 4 bytes
const uint32_t MAGIC = 0xA1B2C3D4;

const int ADDR_FILL    = 4; // 1 byte
const int ADDR_OPENCNT = 8; // 4 bytes
const int ADDR_CLOSECNT= 12; // 4 bytes

// ----------------- GLOBALS -----------------
ESP8266WebServer server(HTTP_PORT);
Servo dustServo;

volatile unsigned long openCount = 0;
volatile unsigned long closeCount = 0;
volatile bool isOpen = false;
unsigned long lastOpenMillis = 0;
String lastOpenHuman = "Never";

int stableIrState;
int lastIrRead;
unsigned long lastIrChangeTime = 0;

// NTP sync flag
bool timeSynced = false;

// Authentication flag (simple, resets on reboot)
bool authenticated = false;

// Fill level (persisted)
int fillLevel = FILL_START;

// ---------- Forward ----------
String escJson(const String &s);
String makeStatusJson();
void trySyncTime();
void moveServoSmooth(int targetAngle);
void doOpen();
void doClose();
void saveStateToEEPROM();
void loadStateFromEEPROM();
void handleLogin();
void handleRoot();
void handleStatus();
void handleOpen();
void handleClose();

// ----------------- UTIL: Smooth servo move -----------------
void moveServoSmooth(int targetAngle) {
  int cur = dustServo.read();
  if (cur == targetAngle) return;
  int step = (targetAngle > cur) ? SERVO_STEP_DEG : -SERVO_STEP_DEG;
  // safety: limit step to not be zero
  if (step == 0) step = (targetAngle > cur) ? 1 : -1;
  while (cur != targetAngle) {
    cur += step;
    // ensure we don't overshoot:
    if ((step > 0 && cur > targetAngle) || (step < 0 && cur < targetAngle)) cur = targetAngle;
    dustServo.write(cur);
    delay(SERVO_STEP_DELAY_MS);
  }
  dustServo.write(targetAngle);
}

// ----------------- ACTIONS -----------------
void doOpen() {
  if (isOpen) return;

  moveServoSmooth(SERVO_OPEN_ANGLE);
  isOpen = true;
  openCount++;
  lastOpenMillis = millis();

  // --- Fake fill increment logic: 5 opens = +2% ---
  static int openSinceLastIncrement = 0;
  openSinceLastIncrement++;
  if(openSinceLastIncrement >= 5){
      fillLevel += 2;              // 5 opens → +2%
      if(fillLevel > FILL_MAX) fillLevel = FILL_MAX;
      openSinceLastIncrement = 0;
  }

  if (timeSynced) {
    time_t now = time(nullptr);
    struct tm* tmInfo = localtime(&now);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmInfo);
    lastOpenHuman = String(buf);
  } else {
    lastOpenHuman = String("Since boot: ") + String(lastOpenMillis / 1000) + "s";
  }

  saveStateToEEPROM();
}

void doClose() {
  if (!isOpen) return;
  moveServoSmooth(SERVO_CLOSE_ANGLE);
  isOpen = false;
  closeCount++;
  saveStateToEEPROM();
}

// ----------------- NTP -----------------
void trySyncTime() {
  // Configure NTP (IST)
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  unsigned long start = millis();
  while (millis() - start < 6000UL) { // wait up to 6s
    time_t now = time(nullptr);
    if (now > 1600000000UL) { // reasonable epoch
      timeSynced = true;
      Serial.println("Time synced via NTP.");
      return;
    }
    delay(150);
  }
  Serial.println("NTP sync failed or not available.");
  timeSynced = false;
}

// ----------------- EEPROM persistence -----------------
void saveStateToEEPROM() {
  // small, infrequent writes
  EEPROM.begin(EEPROM_SIZE);
  // magic
  EEPROM.put(ADDR_MAGIC, MAGIC);
  // fill (1 byte)
  uint8_t f = (uint8_t)constrain(fillLevel, 0, 255);
  EEPROM.write(ADDR_FILL, f);
  // openCount and closeCount (4 bytes each)
  EEPROM.put(ADDR_OPENCNT, openCount);
  EEPROM.put(ADDR_CLOSECNT, closeCount);
  EEPROM.commit();
  EEPROM.end();
}

void loadStateFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magicv = 0;
  EEPROM.get(ADDR_MAGIC, magicv);
  if (magicv == MAGIC) {
    uint8_t f = EEPROM.read(ADDR_FILL);
    fillLevel = constrain((int)f, 0, 100);
    EEPROM.get(ADDR_OPENCNT, openCount);
    EEPROM.get(ADDR_CLOSECNT, closeCount);
  } else {
    // init defaults
    fillLevel = FILL_START;
    openCount = 0;
    closeCount = 0;
    // write initial
    saveStateToEEPROM();
  }
  EEPROM.end();
}

// ----------------- JSON helper -----------------
String escJson(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s.charAt(i);
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else out += c;
  }
  return out;
}

String makeStatusJson() {
  String s;
  s.reserve(200);
  s += "{";
  s += "\"status\":\"";
  s += (isOpen ? "OPEN" : "CLOSED");
  s += "\"";
  s += ",\"isOpen\":";
  s += (isOpen ? "true" : "false");
  s += ",\"openCount\":";
  s += String(openCount);
  s += ",\"closeCount\":";
  s += String(closeCount);
  s += ",\"lastOpen\":\"";
  s += escJson(lastOpenHuman);
  s += "\"";
  s += ",\"fill\":";
  s += String(fillLevel);
  s += ",\"apip\":\"";
  s += WiFi.softAPIP().toString();
  s += "\"";
  s += "}";
  return s;
}

// ----------------- WEB: Handlers -----------------
void handleLogin() {
  if (server.method() == HTTP_POST) {
    String password = server.arg("password");
    if (password == UI_PASSWORD) {
      authenticated = true;
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "Redirecting...");
      return;
    } else {
      authenticated = false;
    }
  }
  // Serve login page
  String loginPage = R"HTML(
<!doctype html>
<html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Dustbin - Login</title>
<style>
body { font-family: Arial, sans-serif; background: #000; color: #eee; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
.container { background: rgba(255,255,255,0.02); padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,230,255,0.08); width:320px;}
input { padding: 10px; margin: 10px 0; width: 100%; border: 1px solid #ccc; border-radius: 5px; background:#111;color:#fff; }
button { padding: 10px; width: 100%; background: #00e6ff; color: #000; border: none; border-radius: 5px; cursor: pointer; font-weight:700; }
.small{color:#9aa0a6;font-size:13px;margin-top:8px;}
</style>
</head>
<body>
<div class="container">
  <h2>Enter Password</h2>
  <form method="POST">
    <input type="password" name="password" placeholder="Password" required>
    <button type="submit">Login</button>
  </form>
  <div class="small">Password is the AP password</div>
  <div class="rightFooter">Powered by naresh</div>
<style>
.rightFooter{position:fixed;right:12px;bottom:12px;font-size:12px;color:#9ff;background:transparent;padding:6px 10px;border-radius:8px;border:1px solid rgba(255,255,255,0.03);}
</style>

</div>
</body>
</html>
)HTML";
  server.send(200, "text/html", loginPage);
}

void handleRoot() {
  if (!authenticated) {
    handleLogin();
    return;
  }
  // Serve single-page UI (full-page dark)
  String page = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Dustbin - Live</title>
<style>
:root { --bg:#000; --card:#0f1114; --accent:#00e6ff; --accent2:#9cffb7; --muted:#9aa0a6; }
html,body{height:100%; margin:0; font-family:Inter,Arial,Helvetica,sans-serif; background:linear-gradient(180deg,#000,#070707); color:#eee;}
.container{max-width:1200px; margin:16px auto; padding:18px;}
.header{display:flex;align-items:center;justify-content:space-between;}
.title{font-size:26px;font-weight:700;letter-spacing:0.4px;}
.subtitle{color:var(--muted); font-size:13px;}
.grid{display:grid;grid-template-columns: 1fr 360px; gap:18px; margin-top:18px;}
@media (max-width:900px){ .grid{grid-template-columns:1fr} .rightCol{order:2} }
.card{background:rgba(255,255,255,0.02); border-radius:14px; padding:18px; box-shadow:0 6px 30px rgba(0,0,0,0.6); border:1px solid rgba(0,230,255,0.04);}
.statusBig{font-size:28px; font-weight:700;}
.statusDot{width:14px;height:14px;border-radius:50%;display:inline-block;margin-right:8px;box-shadow:0 0 14px rgba(0,230,255,0.06);}
.controls{display:flex;gap:12px;margin-top:14px;flex-wrap:wrap;}
.btn{padding:10px 16px;border-radius:10px;border:1px solid rgba(255,255,255,0.04);background:transparent;color:var(--accent);cursor:pointer;font-weight:700;}
.btn.primary{background:#00e6ff;color:#000;border:1px solid #00e6ff;}
.small{font-size:13px;color:var(--muted);}
.counts{display:flex;gap:18px;margin-top:12px;}
.count{min-width:90px;}
.label{font-size:12px;color:var(--muted);}
.num{font-size:22px;font-weight:800;margin-top:6px;}
.fillbar{height:18px;background:rgba(255,255,255,0.03);border-radius:10px;overflow:hidden;border:1px solid rgba(255,255,255,0.02);}
.fill{height:100%;width:10%;background:linear-gradient(90deg,#00e6ff,#9cffb7);box-shadow:0 8px 22px rgba(0,230,255,0.06);}
.rightFooter{position:fixed;right:12px;bottom:12px;font-size:12px;color:#9ff;background:transparent;padding:6px 10px;border-radius:8px;border:1px solid rgba(255,255,255,0.03);}
.powered{position: absolute; right: 16px; top: 8px; font-size:12px; color:#9ff; opacity:0.95;}
.lastOpen{margin-top:12px;color:#ddd;}
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div>
      <div class="title">SMART DUSTBIN</div>
      <div class="subtitle">Local hotspot (AP) mode — fast live updates</div>
    </div>
    <div class="powered">Powered by naresh</div>
  </div>

  <div class="grid">
    <div class="card mainCol">
      <div style="display:flex;align-items:center;justify-content:space-between;">
        <div>
          <div><span id="dot" class="statusDot" style="background:#f33"></span>
              <span id="statusText" class="statusBig">CLOSED</span></div>
          <div class="small lastOpen">Last open: <span id="lastOpen">Never</span></div>
        </div>
        <div style="text-align:right">
          <div class="small">AP IP</div>
          <div class="num" id="apip">192.168.4.1</div>
        </div>
      </div>

      <div class="counts">
        <div class="count card" style="padding:12px;">
          <div class="label">Open Count</div>
          <div id="openCount" class="num">0</div>
        </div>
        <div class="count card" style="padding:12px;">
          <div class="label">Close Count</div>
          <div id="closeCount" class="num">0</div>
        </div>
        <div class="count card" style="padding:12px;">
          <div class="label">Fill Level</div>
          <div class="num" id="fillPct">3%</div>
        </div>
      </div>

      <div style="margin-top:16px;">
        <div class="label">Fill preview</div>
        <div class="fillbar" style="margin-top:8px;">
          <div id="fillBar" class="fill" style="width:3%"></div>
        </div>
      </div>

      <div style="margin-top:18px;" class="controls">
        <button id="btnOpen" class="btn primary">OPEN</button>
        <button id="btnClose" class="btn">CLOSE</button>
        <button id="btnRefresh" class="btn">Refresh</button>
      </div>

    </div>

    <div class="card rightCol">
      <div class="small">Advanced Info</div>
      <div style="margin-top:10px;">
        <div class="small">Mode: <b>AP (Hotspot)</b></div>
        <div class="small" style="margin-top:8px">Connect to Wi-Fi: <b>Dustbin_AP</b></div>
        <div class="small" style="margin-top:8px">Password: <b>12345678</b></div>
      </div>
      <div style="margin-top:12px;">
        <div class="small">Server</div>
        <div style="margin-top:8px;"><span class="label">Polling:</span> <b>500ms</b></div>
        <div style="margin-top:8px;"><span class="label">Time synced:</span> <span id="timeSynced">no</span></div>
      </div>
    </div>

  </div>
</div>

  
<div class="rightFooter" style="background:rgba(0,230,255,0.1);padding:6px 12px;border-radius:10px;font-weight:700;">Powered by naresh</div>

  
  <script>
const pollInterval = 500; // ms
let polling = true;

async function fetchStatus() {
  try {
    const res = await fetch('/status', {cache:'no-store'});
    if (!res.ok) return;
    const j = await res.json();
    document.getElementById('statusText').textContent = j.status;
    document.getElementById('dot').style.background = j.isOpen ? '#0f0' : '#f33';
    document.getElementById('openCount').textContent = j.openCount;
    document.getElementById('closeCount').textContent = j.closeCount;
    document.getElementById('fillPct').textContent = j.fill + '%';
    document.getElementById('fillBar').style.width = j.fill + '%';
    document.getElementById('lastOpen').textContent = j.lastOpen;
    document.getElementById('apip').textContent = j.apip || '192.168.4.1';
    document.getElementById('timeSynced').textContent = (j.lastOpen && !j.lastOpen.startsWith('Since boot')) ? 'yes' : 'no';
  } catch (e) {
    // ignore network hiccups
  }
}

document.getElementById('btnOpen').addEventListener('click', async ()=>{
  document.getElementById('btnOpen').disabled = true;
  await fetch('/open');
  setTimeout(()=>document.getElementById('btnOpen').disabled = false, 800);
  fetchStatus();
});
document.getElementById('btnClose').addEventListener('click', async ()=>{
  document.getElementById('btnClose').disabled = true;
  await fetch('/close');
  setTimeout(()=>document.getElementById('btnClose').disabled = false, 800);
  fetchStatus();
});
document.getElementById('btnRefresh').addEventListener('click', fetchStatus);

// poll loop
setInterval(()=>{ if (polling) fetchStatus(); }, pollInterval);
// initial
fetchStatus();

</script>
</body>
</html>
)HTML";
  server.send(200, "text/html", page);
}

void handleStatus() {
  server.send(200, "application/json", makeStatusJson());
}

void handleOpen() {
  doOpen();
  server.send(200, "application/json", makeStatusJson());
}

void handleClose() {
  doClose();
  server.send(200, "application/json", makeStatusJson());
}

// ----------------- SETUP -----------------
// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("=== Smart Dustbin (AP Mode) ===");

  // load persisted state
  loadStateFromEEPROM();

  // --- RESET STATS ON BOOT ---
  fillLevel = FILL_START;
  openCount = 0;
  closeCount = 0;
  saveStateToEEPROM();  // update EEPROM with fresh start

  // Servo init
  dustServo.attach(SERVO_PIN);
  dustServo.write(SERVO_CLOSE_ANGLE);

  pinMode(IR_PIN, INPUT_PULLUP); // assume IR output pulls LOW on detect; adjust if needed
  lastIrRead = digitalRead(IR_PIN);
  stableIrState = lastIrRead;

  // Start WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Try NTP
  trySyncTime();

  // Register routes
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/open", handleOpen);
  server.on("/close", handleClose);
  server.on("/login", handleLogin);

  server.begin();
  Serial.println("Server started.");
}

void loop() {
  server.handleClient();

  // IR debounce + auto open/close
  int cur = digitalRead(IR_PIN);
  unsigned long now = millis();

  if (cur != lastIrRead) {
    lastIrChangeTime = now;
    lastIrRead = cur;
  }

  if ((now - lastIrChangeTime) > IR_DEBOUNCE_MS) {
    if (cur != stableIrState) {
      stableIrState = cur;
      // assume IR sensor gives LOW when object present
      if (stableIrState == LOW && !isOpen) doOpen();
      else if (stableIrState == HIGH && isOpen) doClose();
    }
  }

  delay(5); // small idle to avoid busy-loop
}
