# SITE-R 7372 — Regolith Station Life Support Controller

SITE-R 7372 is an ESP32-based habitat life-support controller for a sealed glass regolith tank. It reads environmental sensors and drives heaters, a grow light, and a misting pump in a safe, automated way, with a live web dashboard and an on-site LCD.

## Hardware
- ESP32 Dev Module (38-pin, Robocraze)
- DHT11 temperature + humidity
- MQ6 gas sensor
- 2-pin soil moisture sensor
- LDR light sensor
- Peltier heater
- PWM grow-light LED
- Mist pump
- 16x2 I2C LCD
- 3 physical mode buttons (EARTH / MARS / MOON)
- Buzzer

## Features

### Sensors
- DHT11 temp (°C) + humidity (%RH) sampled every 2 s, smoothed with a low-pass filter
- MQ6 gas 0–4095 → BEST / GOOD / MEDIUM / BAD / DEADLY
- Soil moisture 0–4095 → WET / MEDIUM / DRY
- LDR light 0–4095 → DARK / DIM / MEDIUM / BRIGHT

### Actuators & Modes
- **AUTO-GUIDANCE off by default** on boot
- Heater: ON <20 °C, OFF >23 °C (+15 s cooldown)
- Mist: auto-waters dry soil (>=2000) or low humidity (<50 %), stops on wet soil (<=1200) or high humidity (>70 %)
- Grow light "rocket throttle": 0–255 LED brightness slider, smooth ~0.8 s fade
- Three mission modes: EARTH (max 255), MARS (max 153), MOON (max 60) — all dim with LDR
  - LDR >= 2000 → LED off; LDR <= 100 → LED max
- Any manual change while AUTO is on automatically turns AUTO off (`ensureManual`)

### Web dashboard (ESP32-hosted)
- Live telemetry tiles (auto-refresh every 2 s)
- SVG scrolling chart (TEMP / HUMIDITY / GAS / LDR), persisted via localStorage, toggleable
- Red pulsing alarm glow from the top of the screen while any alarm is active
- Status dot + text: SYSTEM NOMINAL / TEMP CRITICAL / HUM CRITICAL / SOIL DRY CRITICAL / GAS CRITICAL / LINK LOST
- **Web alarm audio** (Web Audio API): urgent 900 Hz bursts on gas; 700 Hz triple-beep on temp/humidity/soil. Tap the **SOUND** button to enable (browser autoplay restriction), stops when the alarm clears
- JSON `/data` endpoint and `/set?` control endpoints
- mDNS hostname configurable

### LCD 16x2
- Cycles six screens: GAS / TEMP / HUMIDITY / SOIL / LIGHT / MODE
- Smooth wipe animation; pauses for emergency
- Boot sequence: SYS INIT / sensors -> OK / network / AUTO-CNT OFF / MISSION READY
- `!! EMERGENCY !!` blinks on temp emergency; gas shows its own banner

### Alarm & safety (synced web + model)
| Trigger | Condition | Web | Model |
|---|---|---|---|
| High temp | ≥35 °C | glow + beep + CRITICAL | LED off, heater off, buzzer, LCD |
| Low temp | ≤10 °C | glow + beep | buzzer |
| Gas | MQ6 ≥2500 | urgent glow + beep | fast LED blink + urgent buzzer |
| Humidity high | >90 % | glow + beep | buzzer |
| Humidity low | <30 % | glow + beep | buzzer |
| Soil dry | ≥2000 | glow + beep | buzzer |


SITE-R 7372 parts list

ESP32 Dev Module 38-pin (Robocraze) — 1
DHT11 temp/humidity module — 1
MQ6 gas sensor module — 1
Soil moisture sensor (2-pin analog) — 1
LDR photoresistor — 1
10kΩ resistor (for LDR voltage divider) — 2
I2C 16x2 LCD with PCF8574 backpack — 1
L298N motor driver module (grow light channel) — 1
HJR-3FC 5V relay module (for Peltier) — 1
Peltier element (TEC1-12706) — 1
Ultrasonic mist maker / fogger — 1
MOSFET driver module (IRF520, for mist) — 1
Passive buzzer 2-pin — 1
Tactile push buttons (EARTH / MARS / MOON) — 3
Breadboard 830-point — 1
Jumper wire kit — 1
5V/2A USB power supply — 1
12V/2A power supply (for Peltier via relay) — 1
Glass terrarium / display enclosure — 1
Zip ties, hot glue, standoffs, cable clips — 1 set

## Build & flash (PlatformIO)
```bash
pio run
pio run -t upload
```
Set your AP SSID/password and IP in `src/main.cpp` (search `AP_SSID`, `AP_PASS`, `WiFi.softAPConfig`, `MDNS.begin`).

## Wiring
See `WIRING.md` for the full pin-by-pin guide.

## Monitoring
```bash
pio device monitor --baud 115200
```

## License
Apache License 2.0 — see `LICENSE`.
