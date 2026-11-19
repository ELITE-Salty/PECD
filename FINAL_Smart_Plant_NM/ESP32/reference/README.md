# Reference sources (not built)

Unmodified copies of the code from which `../main/main.c` was derived. They
are kept so that the development history can be traced in the report and
presentation even if this folder is published as a standalone repository.

| File | Original location | Notes |
| --- | --- | --- |
| `module1_buzzer_main.c` | `project_01_buzzer_passive/main/main.c` | LEDC buzzer, GPIO27, 800 Hz |
| `module2_soil_moisture_main.c` | `02_moisture_adc/main/main.c` | ADC1 CH6 one-shot, 1000 ms |
| `module3_dht11_main.c` | `03_dht11/main/main.c` | DHT11 on GPIO26, 2500 ms |
| `integrated_v0_NM_main.c` | `Smart_Plant_Monitoring_System_NM/main/main.c` | First integration: single 5 s loop, SPIFFS rules/log, soil mapped to 0..1000 with placeholder calibration |

## Changes from `integrated_v0_NM_main.c` to `main/main.c`

| Change | Reason |
| --- | --- |
| Soil value kept raw (0..4095); 0..1000 mapping removed | Mapping used uncalibrated placeholder constants (3200/1300) |
| Single 5 s loop → independent soil (1 s) and DHT11 (2.5 s) deadlines | Sampling intervals must be independently configurable |
| Rule check logs only on transitions | Avoid console flooding at 1 s sampling |
| Log rotation to `events_prev.txt` (256 KiB) | Bound flash usage; v0 already exported `events_prev.txt` but never created it |
| Failed sensor reads written to the log | DHT11 failure rate is needed for the experiments |
| Invalid configuration lines reported; `#` comments allowed; up to 8 rules | v0 silently ignored invalid lines and stopped after 3 rules |
| 600 ms gap after each beep group | Distinguish beep counts when two rules trigger together |
| Reference to `tools/export_logs.py` removed | Script does not exist in the project |
| Explicit `esp_timer` component dependency, `sdkconfig.defaults` added | v0 had a custom `partitions.csv` but nothing selecting it, and the table needs 4 MB flash |

Not carried over: `SmartPlantESP32/` (adaptive sampling + light sleep). It
was never confirmed to work and the features belong to Experiments 2 and 4;
it remains available in the original working directory as a reference.
