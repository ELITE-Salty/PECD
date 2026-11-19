# Report outline

Legend: **[Draft now]** can be written with the information available;
**[Needs design]** depends on team decisions not yet taken;
**[Needs evidence]** requires experimental results.

## 1. Introduction [Draft now]
- Motivation: plant monitoring with limited energy budgets.
- Objectives from the assignment (environmental monitoring, visual health,
  energy efficiency, on-device CV).
- Contributions of the project and of each team member.

## 2. Background and related work [Draft now]
- Soil moisture sensing (resistive vs. capacitive), DHT11 characteristics and
  limitations (resolution, accuracy, minimum sampling period).
- ESP32 power modes (active, modem sleep, light sleep, deep sleep).
- Efficient CV on embedded devices: model size, quantisation, ONNX Runtime /
  TFLite on Raspberry Pi; edge vs. server inference.
- Plant requirements for the species in the kit (sources for thresholds).

## 3. System architecture [Needs design]
- Block diagram (from `Documentation/architecture/system_architecture.md`).
- Division of responsibilities between ESP32 and Raspberry Pi.
- Communication protocol and its justification (not yet decided).

## 4. Implementation
- 4.1 ESP32 firmware [Draft now]: peripherals, scheduling, rule engine,
  logging, design decisions (`technical_decisions.md`).
- 4.2 Raspberry Pi and computer vision [Needs design]: camera, model, pipeline.
- 4.3 Configuration and event log [Draft now, update later]: format,
  timestamps (current limitation: uptime only).

## 5. Energy optimisation [Needs evidence]
- Solutions considered, implemented and evaluated (presentation requirement).
- Sampling-rate optimisation (adaptive sampling).
- Sleep modes.
- Hierarchical sensing / camera activation.
- Model optimisation (quantisation).
- For each: hypothesis, implementation, measured effect, trade-offs.

## 6. Evaluation [Needs evidence]
- 6.1 Measurement setup and methodology (instrument, measurement point).
- 6.2 Energy: idle consumption, energy per sensing operation, buzzer, camera,
  CV, complete system (presentation requirement).
- 6.3 Environmental change detection accuracy and latency.
- 6.4 Visual distress detection accuracy.
- 6.5 Comparative analysis against at least one alternative.
- 6.6 Practical test scenario / use case.

## 7. Discussion [Needs evidence]
- Interpretation of results, trade-offs, unsuccessful approaches and why.
- Limitations (development-board overhead, DHT11 accuracy, dataset size).

## 8. Conclusion and future work [Needs evidence]

## Appendices
- Wiring diagram and pin table [Draft now].
- `configuration.txt` final version and justification of thresholds
  [Needs evidence: plant species research].
- Raw data references (`Results/`).
