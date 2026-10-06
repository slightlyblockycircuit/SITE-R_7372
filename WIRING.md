# SITE-R 7372 Wiring Guide

## Power
- 5V from an external supply into the ESP32 `5V` pin (VIN/5V)
- All grounds (GND) are common — connect every module GND together
- DHT11, sensors and relay coil run from 3.3V where noted; Peltier from 5V/12V via relay

## Sensors
| Module | VCC | GND | Signal |
|---|---|---|---|
| DHT11 | 3.3V | GND | GPIO **4** |
| MQ6 gas | 5V | GND | GPIO **34** (ADC) |
| Soil moisture | 3.3V | GND | GPIO **35** (ADC) |
| LDR | 3.3V | GND | GPIO **32** (ADC, with 10kΩ divider) |

> LDR: wire it as `3V3 — LDR — GPIO32 — 10kΩ — GND` so brighter light = higher reading.

## Actuators
| Module | VCC | GND | Control |
|---|---|---|---|
| Peltier heater | 12V+ (via relay NO) | GND | relay coil on GPIO **19** |
| Grow LED (PWM) | 5V | GND | GPIO **26** (PWM) + GPIO **25** (LED driver IN1) |
| Mist pump/fogger | 5V | GND | GPIO **23** (PWM) |
| Buzzer | 5V | GND | GPIO **33** (PWM) |

## Buttons (modes)
- EARTH: GPIO **13** → other leg to GND (INPUT_PULLUP)
- MARS:  GPIO **16** → other leg to GND
- MOON:  GPIO **17** → other leg to GND

## LCD (I2C)
| LCD Pin | ESP32 |
|---|---|
| SDA | GPIO **21** |
| SCL | GPIO **22** |
| VCC | 5V |
| GND | GND |

> LCD address is commonly `0x27`; if the screen stays blank, try `0x3F` in the sketch.

## Notes
- The 38-pin Robocraze board runs 3.3V logic — use a resistor divider on 5V sensor outputs (LDR/MQ6) or use 3.3V-tolerant modules.
- Keep the Peltier on its own power; do not power it from the ESP32's onboard regulator.
