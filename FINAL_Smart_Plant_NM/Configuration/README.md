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

`soil_humidity` uses the assignment's 0..1000 scale. We calibrated our sensor
with two points: probe in air = raw 0, probe in freshly watered soil =
raw 2340-2370 (noise about +-15 raw). The sensor saturates around raw 2560,
which we map to 1000, so wet soil reads about 920 and **higher = wetter**.
`main.c` and `adaptive.c` both convert raw ADC counts with
`soil = 1000 * raw / 2560` (clamped to 0..1000) before checking the rules.

## Current thresholds (dwarf sunflower 'Sunspot', *Helianthus annuus*)

| Beeps | Rule | Why |
| --- | --- | --- |
| 1 | `air_humidity > 85` | long periods of very humid air favour fungal diseases of sunflower (e.g. *Sclerotinia*, downy mildew) [Schneiter 1997; Berglund 2007] |
| 2 | `air_temperature < 10` | sunflower growth almost stops below ~6-10 degC (base temperature used for sunflower growing-degree days is ~6.7 degC) [Schneiter 1997] |
| 3 | `soil_humidity < 300` | the assignment's example value; on our scale it is ~1/3 of a freshly watered pot, i.e. the top soil has clearly dried out. Sunflowers tolerate some drought, but a young plant in a small pot should be watered before this point. Not validated with plant wilting. |
| 4 | *(reserved)* | vision alarm from the Raspberry Pi (`alarm_beeps` in `RaspberryPi/config.json`) |
| 5 | `air_temperature > 35` | heat above ~35 degC reduces pollen viability and seed set [Schneiter 1997] |

References (full entries in the report):
- A. A. Schneiter (ed.), *Sunflower Technology and Production*, Agronomy
  Monograph 35, ASA/CSSA/SSSA, 1997.
- D. R. Berglund (ed.), *Sunflower Production*, NDSU Extension A-1331, 2007.

These are general sunflower values from the literature, not measured on our
plant; the indoor dwarf variety may differ.

## Applying changes

```bash
cd ESP32
idf.py -p <PORT> storage-flash   # note: also erases events_log.txt on the device
```

Export the log first (`logs` command in the serial monitor).
