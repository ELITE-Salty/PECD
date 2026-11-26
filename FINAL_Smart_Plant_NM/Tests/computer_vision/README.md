# Computer vision and Pi cycle tests

| File | What it tests |
| --- | --- |
| `test_plant_cycle.py` | `RaspberryPi/plant_cycle.py`: decision policy, serial protocol and error handling (with fakes, no hardware) |
| `test_pi_wake_schedule.c` | `ESP32/main/pi_wake_schedule.h`: 24 h wake schedule |

Energy of the two vision models was measured on the Pi with the Monsoon
(`Results/energy_consumption/esp32_sleep_and_pi_summary.json`, `ai_bursts`).
