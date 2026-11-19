# Software

## ESP32 firmware (`ESP32/main/main.c`)

Single ESP-IDF application, organised in sections:

| Section | Functions | Origin |
| --- | --- | --- |
| Storage and event log | `storage_init`, `log_event`, `load_rules` | first integrated version |
| Buzzer | `buzzer_init`, `beep`, `evaluate_rules` | module 1 + integrated version |
| Soil moisture | `soil_init`, `sample_soil` | module 2 |
| DHT11 | `dht_init`, `sample_dht` | module 3 |
| Serial log export | `send_file`, `check_serial_command` | first integrated version |
| Main loop | `app_main` | new: independent per-sensor deadlines |

Control flow: initialise storage → load rules → initialise buzzer, ADC, DHT11
pin and UART → wait 2 s for the DHT11 → loop: sample each sensor whose
deadline has passed, evaluate its rules, check for the `logs` command,
block until the next deadline.

See `ESP32/README.md` for configuration values and limitations, and
`Documentation/architecture/technical_decisions.md` for the reasoning.

## Raspberry Pi software

Not yet integrated; see `RaspberryPi/README.md`.
