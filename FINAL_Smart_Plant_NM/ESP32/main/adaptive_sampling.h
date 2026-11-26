/*
 * adaptive_sampling.h - how the ESP32 picks its next sensing interval and
 * when it is worth waking the Raspberry Pi for a photo.
 *
 * Pure C with no ESP-IDF calls, so the exact same logic is compiled into the
 * firmware (adaptive.c) and into the host test/simulator
 * (Tests/adaptive_sampling/test_adaptive_sampling.c).
 *
 * Idea (assignment feature 3, "sample less often when conditions are stable"):
 *   - start at the minimum interval;
 *   - every reading that is "about the same" as the previous one doubles the
 *     interval, up to the maximum;
 *   - a real change (bigger than the sensor noise) or a value close to an
 *     alarm threshold drops the interval straight back to the minimum.
 * This is the same "back off while nothing happens" pattern as TCP/Ethernet
 * back-off, just used for sensing.
 *
 * How the numbers were chosen (see the report, Section 3.2):
 *   MIN 60 s   - worst-case alarm delay while something is changing. A plant
 *                does not suffer in one minute, and at 60 s the energy is
 *                already close to the sleep floor of our DevKit (model in
 *                Tests/energy_measurements/energy_model.py).
 *   MAX 900 s  - worst-case delay to notice the first change after a stable
 *                period. Soil dries over hours/days and room temperature
 *                moves slowly, so 15 min loses no relevant event.
 *   Change thresholds are just above the noise we measured / the sensor
 *   resolution, so noise alone never resets the interval:
 *     soil  20 /1000 (our wet-soil noise was about +-15 raw = +-6 /1000)
 *     temp  1.0 C    (DHT11 resolution 1 C)
 *     hum   3 %RH    (DHT11 resolution 1 %RH, readings jitter by 1-2 %)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AS_MIN_INTERVAL_S      60u
#define AS_MAX_INTERVAL_S      900u

#define AS_SOIL_DELTA          20.0f   /* 0..1000 scale */
#define AS_TEMP_DELTA          1.0f    /* degC */
#define AS_HUM_DELTA           3.0f    /* %RH */

/* "Close to a rule threshold": inside this band we sample at the minimum
 * interval so an alarm is raised quickly when the threshold is crossed. */
#define AS_SOIL_NEAR           50.0f
#define AS_TEMP_NEAR           2.0f
#define AS_HUM_NEAR            5.0f

/* Camera (Raspberry Pi) policy. One wake costs ~300 J (boot ~200 J + cycle),
 * the same as ~1.6 h of the whole ESP32 board asleep, so photos are rare:
 *   - one routine photo every 24 h;
 *   - an extra photo when an alarm rule has just become active (the
 *     conditions changed enough to matter for the plant), but never more
 *     often than every 6 h.
 * We first also woke the Pi after a large soil change (150/1000), but in the
 * simulated scenario normal drying alone triggered it about once a day, so
 * that trigger was removed. */
#define AS_PI_ROUTINE_S        (24u * 3600u)
#define AS_PI_MIN_GAP_S        (6u * 3600u)

typedef struct {
    float soil;          /* 0..1000, NAN-free: use valid flags below */
    float temp;          /* degC */
    float hum;           /* %RH */
    bool soil_ok;
    bool dht_ok;
} as_reading_t;

typedef struct {
    uint32_t interval_s;     /* interval chosen for the next sleep */
    as_reading_t last;       /* previous valid reading */
    bool have_last;
    uint32_t since_photo_s;  /* seconds since the last Pi wake (set by caller) */
} as_state_t;

static inline float as_absf(float x) { return x < 0 ? -x : x; }

static inline void as_init(as_state_t *s)
{
    s->interval_s = AS_MIN_INTERVAL_S;
    s->have_last = false;
    s->since_photo_s = 0;
}

/* Did anything move more than the noise since the previous reading?
 * A failed read counts as "no information", not as a change. */
static inline bool as_changed(const as_state_t *s, const as_reading_t *r)
{
    if (!s->have_last) return true;
    if (r->soil_ok && s->last.soil_ok &&
        as_absf(r->soil - s->last.soil) >= AS_SOIL_DELTA) return true;
    if (r->dht_ok && s->last.dht_ok &&
        (as_absf(r->temp - s->last.temp) >= AS_TEMP_DELTA ||
         as_absf(r->hum - s->last.hum) >= AS_HUM_DELTA)) return true;
    return false;
}

/* Updates the state with a new reading and returns the next interval.
 * near_threshold is computed by the caller from the loaded alarm rules. */
static inline uint32_t as_next_interval(as_state_t *s, const as_reading_t *r,
                                        bool near_threshold)
{
    if (near_threshold || as_changed(s, r)) {
        s->interval_s = AS_MIN_INTERVAL_S;
    } else {
        uint32_t doubled = s->interval_s * 2u;
        s->interval_s = doubled > AS_MAX_INTERVAL_S ? AS_MAX_INTERVAL_S : doubled;
    }
    /* Keep the last VALID value per sensor so one failed read does not
     * erase the reference used for change detection. */
    if (r->soil_ok) { s->last.soil = r->soil; s->last.soil_ok = true; }
    if (r->dht_ok) {
        s->last.temp = r->temp; s->last.hum = r->hum; s->last.dht_ok = true;
    }
    s->have_last = true;
    return s->interval_s;
}

/* Called once per cycle after the caller has set since_photo_s.
 * Returns true when the Pi should be woken now. */
static inline bool as_should_wake_pi(as_state_t *s, bool new_alarm)
{
    bool wake = s->since_photo_s >= AS_PI_ROUTINE_S ||                 /* daily */
                (new_alarm && s->since_photo_s >= AS_PI_MIN_GAP_S);   /* event */
    if (wake) s->since_photo_s = 0;
    return wake;
}
