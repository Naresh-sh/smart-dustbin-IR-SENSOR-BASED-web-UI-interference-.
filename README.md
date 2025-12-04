# smart-dustbin-IR-SENSOR-BASED-web-UI-interference-.



SMART DUSTBIN 🚮

Wi-Fi smart dustbin with automatic lid and fill tracking.


--------------------------------------------------FEATURES ;-------------------------------------------

AP Mode hotspot (Dustbin_AP) 🌐

Web UI with password protection 🔒

IR sensor auto-open 🤖

Smooth servo control 🌀

Fill level tracking (5 opens → +2%) 📊

EEPROM saves counts & fill after reboot 💾

NTP time sync ⏱

JSON API: /status, /open, /close


---------------------------------------------------------HARDWARE'S :-------------------------

ESP8266 (NodeMCU / Wemos D1 Mini)

Servo → D4(GPIO2)

IR sensor → D5(GPIO14)

Power → 5V USB


-------------------------------------------------------------------USAGE---------------------

Flash code to ESP8266.

Connect Servo & IR sensor.

Power on → connect to Wi-Fi Dustbin_AP (Password: PASSWORD).

Open browser → http://192.168.4.1 → enter password.

Control manually or let IR auto-open.

Check counts & fill level live.


-----------------------------------------------------------------------API----------------------------------

GET /status → lid state, counts, fill
GET /open → open lid
GET /close → close lid





