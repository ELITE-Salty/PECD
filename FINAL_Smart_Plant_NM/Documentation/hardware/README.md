# Hardware

## ESP32 wiring (from the existing modules)

| Device | ESP32 pin | Peripheral | Notes |
| --- | --- | --- | --- |
| Soil moisture sensor, analog output | GPIO34 | ADC1 channel 6 | Input-only pin; output must stay ≤ 3.3 V |
| DHT11 data | GPIO26 | GPIO (bit-banged protocol) | Internal pull-up enabled in firmware |
| Passive buzzer | GPIO27 | LEDC PWM, 800 Hz | Driven directly from the GPIO |
| All grounds | GND | - | Common ground required |

## To be confirmed by the team

- ESP32 board model and flash size (firmware assumes 4 MB).
- Soil moisture sensor type (capacitive or resistive) and model.
- Supply voltage of the soil sensor and the DHT11 (3.3 V or 5 V). A 5 V
  sensor output must not be connected directly to GPIO34.
- DHT11: bare sensor or module with on-board pull-up resistor.
- Whether the buzzer is driven directly from GPIO27 or through a transistor,
  and its current draw.
- Raspberry Pi model, camera model and interface, power supply.
- Energy measurement instrument.

A wiring diagram (photo or schematic) should be added here once confirmed.
