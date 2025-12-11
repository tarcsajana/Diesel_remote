# ESP32-C3 Diesel Heater Remote - Pinout Diagram

## ESP32-C3 Board Layout (Top View)

```
                    USB
                     |
    ┌────────────────────────────────┐
    │  ESP32-C3 Development Board    │
    │                                │
    │  GND  3V3  EN   IO0   GND      │  (Top row - USB side)
    │                                │
    │  IO20  IO19  IO18  IO17  IO16  │
    │  IO21  IO9   IO8   IO7   IO6   │
    │  IO22  IO10  IO5   IO4   IO3   │
    │  IO23  IO11  IO2   IO1   GND   │
    │                                │
    │  GND   5V   GND   GND   GND    │  (Bottom row)
    └────────────────────────────────┘

```

## Diesel Heater Remote Connection

```
┌─────────────────────────────────────────────────────┐
│          Diesel Heater Remote Wiring                │
└─────────────────────────────────────────────────────┘

ESP32-C3 GPIO Pins ──> Remote Control Relay Module

┌──────────────────────┐         ┌──────────────────────┐
│   ESP32-C3           │         │  Relay Module        │
│                      │         │                      │
│  GPIO4 (BUTTON_POWER)├────────►│  Relay 1 / POWER    │
│                      │         │                      │
│  GPIO5 (BUTTON_DOWN) ├────────►│  Relay 2 / DOWN     │
│                      │         │                      │
│  GPIO6 (BUTTON_UP)   ├────────►│  Relay 3 / UP       │
│                      │         │                      │
│  GPIO8 (LED_PIN)     ├────────►│  Status LED         │
│                      │         │                      │
│  GND                 ├────────►│  GND (Common)       │
│                      │         │                      │
│  5V (Optional)       ├────────►│  VCC (Power)        │
└──────────────────────┘         └──────────────────────┘
```

## Pin Configuration Summary

| GPIO | Function | Purpose | State |
|------|----------|---------|-------|
| **4** | BUTTON_POWER | Controls Power button on remote | LOW (default) → HIGH (500ms) → LOW |
| **5** | BUTTON_DOWN | Controls Down button on remote | LOW (default) → HIGH (500ms) → LOW |
| **6** | BUTTON_UP | Controls Up button on remote | LOW (default) → HIGH (500ms) → LOW |
| **8** | LED_PIN | Status LED indicator | HIGH when WiFi/MQTT connected |
| **GND** | Ground | Common ground with relay module | Reference voltage (0V) |

## Signal Timing Diagram

```
MQTT Command: mosquitto_pub -h 10.0.0.20 -t "diesel_heater/power" -m "PRESS"

GPIO4 (BUTTON_POWER) Voltage Over Time:

5V  ┐
    │
    │     ┌─────────┐
3.3V├─────┤         │
    │     │         │
    │     │ 500ms   │
0V  └─────┴─────────┴─────► time
         ↑           ↑
      Message    Released
      Received   (Timeout)

```

## Connection Notes

- **Pins are OUTPUT mode** - ESP32 drives the relay control pins
- **Default state: LOW** (unpressed/inactive)
- **Pulse on command: HIGH for 500ms** (simulates button press)
- **Auto-release: Returns to LOW** after 500ms timeout
- **GND must be connected** to relay module common ground for circuit to work
- **Each relay controls one button** on the diesel heater remote

## MQTT Control Topics

```
Topic: diesel_heater/power    ──► GPIO4 goes HIGH → Pulls relay 1
Topic: diesel_heater/down     ──► GPIO5 goes HIGH → Pulls relay 2
Topic: diesel_heater/up       ──► GPIO6 goes HIGH → Pulls relay 3

Payload: "PRESS" or "ON" or "1"
```

## Troubleshooting Checklist

✓ GPIO pins are valid (4, 5, 6, 8 exist on ESP32-C3)
✓ Pins initialized as OUTPUT in setup()
✓ Default state set to LOW
✓ MQTT subscriptions active
✓ Button press sets pin HIGH
✓ Timer triggers pin LOW after 500ms
✓ GND connection between ESP32 and relay module
✓ Relay module has proper power supply (5V or 3.3V depending on relay type)
