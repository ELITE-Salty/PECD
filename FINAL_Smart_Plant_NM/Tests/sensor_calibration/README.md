# Soil sensor calibration

Two-point calibration of our soil moisture sensor on GPIO34 (ADC1 CH6, 12 bit,
12 dB attenuation), together with the DHT11 readings taken at the same time.

| Condition | Soil raw ADC | Air temperature | Air humidity |
| --- | ---: | ---: | ---: |
| Probe in air (nothing connected to soil) | 0 | 24 degC | 57 % |
| Probe in freshly watered soil, after 5-10 min | 2340-2370 | 24.3 degC | 60 % |

- Higher value = wetter soil. Noise in wet soil: about +-15 raw.
- The sensor saturates around raw 2560, which we map to 1000 on the
  assignment's 0..1000 scale: `soil = 1000 * raw / 2560` (clamped). Wet soil
  therefore reads ~915-925.
- Only two points were measured, so values in between assume a linear sensor.
