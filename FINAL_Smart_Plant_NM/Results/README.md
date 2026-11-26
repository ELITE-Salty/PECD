# Results

Only real, measured data and values computed from it are stored here. The raw
Monsoon recordings are in `../Measurements/`; the scripts are in `../Tests/`.

| File | Content | Produced by |
| --- | --- | --- |
| `energy_consumption/esp32_unoptimised_summary.json` | ESP32 baseline: idle power, mean power, energy per cycle, 5 s beep (6 runs) | `Tests/energy_measurements/analyse_pt5.py` |
| `energy_consumption/esp32_sleep_and_pi_summary.json` | ESP32 light/deep sleep cycles; Raspberry Pi boot, idle and vision runs | `Tests/energy_measurements/analyse_sleep_and_pi.py` |
| `energy_consumption/energy_model_summary.json` | Inputs and outputs of the measurement-based energy model | `Tests/energy_measurements/energy_model.py` |
| `adaptive_sampling_summary.json` | Simulated scenario: readings, energy and alarm delays per strategy, Pi wakes | `Tests/adaptive_sampling/simulate_scenario.py` |
| `pi_wake_test_2026-11-19.json` | ESP32 to Pi GPIO wake test (pulse timing, boot evidence, open issues) | manual test on the Pi |

The measurement units of the `.pt5` files (current in uA, voltage in 0.5 mV)
were inferred from the files: they give 4.99 V for the 5.0 V setting and
~43 mA for the awake ESP32 DevKit.
