#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Elapsed-time schedule, not wall-clock time. ESP32 restart starts a new day. */
#define PI_WAKE_INTERVAL_US (INT64_C(24) * 60 * 60 * 1000000)

typedef struct {
    int64_t next_us;
} pi_wake_schedule_t;

static inline void pi_wake_schedule_init(pi_wake_schedule_t *schedule, int64_t now)
{
    schedule->next_us = now + PI_WAKE_INTERVAL_US;
}

static inline bool pi_wake_schedule_due(pi_wake_schedule_t *schedule, int64_t now)
{
    if (now < schedule->next_us) return false;
    /* If execution was delayed, send one pulse, not a burst of missed days. */
    pi_wake_schedule_init(schedule, now);
    return true;
}

static inline int64_t pi_wake_seconds_left(const pi_wake_schedule_t *schedule, int64_t now)
{
    int64_t left = schedule->next_us - now;
    return left > 0 ? (left + 999999) / 1000000 : 0;
}
