# Configuration

`configuration.txt` defines the conditions that trigger buzzer alerts. It is
the **single source** of the rules: the ESP32 build copies it into the SPIFFS
image (see `ESP32/main/CMakeLists.txt`).

## Format

```
<beeps>: <sensor> <operator> <threshold>
```

| Field | Allowed values |
| --- | --- |
| `beeps` | 1..10 - number of buzzer signals |
| `sensor` | `air_humidity` (%RH), `air_temperature` (°C), `soil_humidity` |
| `operator` | `<`, `>` |
| `threshold` | number |

Blank lines and lines starting with `#` are ignored. Invalid lines are
reported on the serial console and ignored. Up to 8 rules are loaded.

## Units of `soil_humidity`

The assignment defines soil humidity on a 0..1000 scale. Until the soil
sensor is calibrated (Experiment 1), the firmware compares **raw ADC counts
(0..4095)**. Soil thresholds must be rewritten after calibration.

## Current thresholds

The current values are the examples from the assignment and are
**placeholders**. Final thresholds should be justified from the optimal
conditions of the plant species in the greenhouse kit, with sources cited in
the report.

## Applying changes

```bash
cd ESP32
idf.py -p <PORT> storage-flash   # note: also erases events_log.txt on the device
```

Export the log first (`logs` command in the serial monitor).
