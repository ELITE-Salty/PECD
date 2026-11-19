#include <assert.h>
#include <stdio.h>
#include "../../ESP32/main/pi_wake_schedule.h"

int main(void)
{
    pi_wake_schedule_t schedule;
    int64_t start = INT64_C(5000000000); /* Beyond 32-bit microsecond range. */
    pi_wake_schedule_init(&schedule, start);
    assert(pi_wake_seconds_left(&schedule, start) == 86400);
    assert(!pi_wake_schedule_due(&schedule, start));
    assert(!pi_wake_schedule_due(&schedule, start + PI_WAKE_INTERVAL_US - 1));
    assert(pi_wake_schedule_due(&schedule, start + PI_WAKE_INTERVAL_US));
    assert(!pi_wake_schedule_due(&schedule, start + PI_WAKE_INTERVAL_US));
    assert(pi_wake_seconds_left(&schedule, start + PI_WAKE_INTERVAL_US) == 86400);
    assert(pi_wake_schedule_due(&schedule, start + 5 * PI_WAKE_INTERVAL_US));
    assert(!pi_wake_schedule_due(&schedule, start + 5 * PI_WAKE_INTERVAL_US));
    schedule.next_us = start + INT64_C(60) * 1000000; /* Accelerated wake test. */
    assert(!pi_wake_schedule_due(&schedule, start + INT64_C(59) * 1000000));
    assert(pi_wake_schedule_due(&schedule, start + INT64_C(60) * 1000000));
    assert(pi_wake_seconds_left(&schedule, start + INT64_C(60) * 1000000) == 86400);
    puts("Pi wake schedule checks passed");
}
