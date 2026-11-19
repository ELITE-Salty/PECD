# Software

## ESP32 firmware (`ESP32/main/main.c`)

Single ESP-IDF application, organised in sections:

| Section | Functions | Origin |
| --- | --- | --- |
| Storage and event log | `storage_init`, `log_event`, `load_rules` | first integrated version |
| Buzzer | `buzzer_init`, `beep`, `evaluate_rules` | module 1 + integrated version |
| Soil moisture | `soil_init`, `sample_soil` | module 2 |
| DHT11 | `dht_init`, `sample_dht` | module 3 |
| Serial log export and remote alarm | `send_file`, `check_serial_port` | UART0/UART2 commands for Pi integration |
| Daily Pi wake | `pi_wake_init`, `check_pi_wake` | GPIO25 open-drain pulse and 24-hour uptime schedule |
| Main loop | `app_main` | new: independent per-sensor deadlines |

Control flow: initialise storage → load rules → initialise peripherals and
wake schedule → wait 2 s for the DHT11 → loop: check the wake deadline,
sample due sensors and evaluate rules, process UART commands, then yield for
at most 20 ms. Log exports and beep sequences still block the single loop.

See `ESP32/README.md` for configuration values and limitations, and
`Documentation/architecture/technical_decisions.md` for the reasoning.

## Raspberry Pi software

`RaspberryPi/plant_cycle.py` captures a photo, runs colour screening with the
small model, optionally invokes the larger classifier, requests remote alarms
and saves ESP32 log snapshots. `--logs-only` skips camera/model processing.
The supplied systemd service runs one cycle at boot and powers off afterwards;
it is not installed because sudo setup and post-wake UART diagnosis remain.
See `RaspberryPi/README.md` for configuration, validation and limitations.
