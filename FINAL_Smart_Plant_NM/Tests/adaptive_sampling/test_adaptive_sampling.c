/*
 * Host test + simulator for ESP32/main/adaptive_sampling.h
 * (the same header that is compiled into the firmware adaptive.c).
 *
 * Build (any PC with gcc):
 *   gcc -std=c11 -O2 -I../../ESP32/main test_adaptive_sampling.c -o test_adaptive_sampling
 * Unit tests:
 *   ./test_adaptive_sampling
 * Simulation of a sensor trace (CSV "t_s,soil,temp,hum", one row per 10 s):
 *   ./test_adaptive_sampling --simulate trace.csv > decisions.csv
 * The simulator applies the SAME rules as Configuration/configuration.txt
 * (kept in sync by hand, see RULES below).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adaptive_sampling.h"

/* ---------------- unit tests ---------------- */
static as_reading_t rd(float soil, float temp, float hum)
{
    as_reading_t r = { soil, temp, hum, true, true };
    return r;
}

static void test_backoff(void)
{
    as_state_t s; as_init(&s);
    as_reading_t r = rd(700, 22, 50);
    assert(as_next_interval(&s, &r, false) == 60);    /* first reading */
    assert(as_next_interval(&s, &r, false) == 120);   /* stable -> doubles */
    assert(as_next_interval(&s, &r, false) == 240);
    assert(as_next_interval(&s, &r, false) == 480);
    assert(as_next_interval(&s, &r, false) == 900);   /* capped */
    assert(as_next_interval(&s, &r, false) == 900);
}

static void test_noise_is_ignored_but_change_resets(void)
{
    as_state_t s; as_init(&s);
    as_reading_t r = rd(700, 22, 50);
    as_next_interval(&s, &r, false);
    as_next_interval(&s, &r, false);                  /* 120 */
    r.soil = 710; r.hum = 52;                         /* below noise thresholds */
    assert(as_next_interval(&s, &r, false) == 240);
    r.temp = 23;                                      /* 1 degC step = real change */
    assert(as_next_interval(&s, &r, false) == 60);
    r.soil = 680;                                     /* 30/1000 step = real change */
    assert(as_next_interval(&s, &r, false) == 60);
}

static void test_near_threshold_forces_minimum(void)
{
    as_state_t s; as_init(&s);
    as_reading_t r = rd(330, 22, 50);
    as_next_interval(&s, &r, false);
    assert(as_next_interval(&s, &r, true) == 60);
    assert(as_next_interval(&s, &r, true) == 60);
}

static void test_failed_read_keeps_reference(void)
{
    as_state_t s; as_init(&s);
    as_reading_t r = rd(700, 22, 50);
    as_next_interval(&s, &r, false);
    as_reading_t bad = r; bad.dht_ok = false; bad.soil_ok = false;
    assert(as_next_interval(&s, &bad, false) == 120); /* no info != change */
    assert(s.last.temp == 22.0f && s.last.soil == 700.0f);
}

static void test_pi_policy(void)
{
    as_state_t s; as_init(&s);
    s.since_photo_s = 3600;
    assert(!as_should_wake_pi(&s, true));             /* alarm, but < 6 h */
    s.since_photo_s = 7 * 3600;
    assert(!as_should_wake_pi(&s, false));            /* > 6 h, no alarm */
    assert(as_should_wake_pi(&s, true));              /* > 6 h and new alarm */
    assert(s.since_photo_s == 0);
    s.since_photo_s = 24 * 3600;
    assert(as_should_wake_pi(&s, false));             /* daily photo */
}

/* ---------------- simulator ---------------- */
typedef struct { int sensor; char op; float thr; } rule_t;  /* 0 hum, 1 temp, 2 soil */
/* Same rules as Configuration/configuration.txt */
static const rule_t RULES[] = {
    {0, '>', 85}, {1, '<', 10}, {2, '<', 300}, {1, '>', 35},
};
#define NRULES (int)(sizeof(RULES) / sizeof(RULES[0]))

static int simulate(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    static float T[200000], SO[200000], TE[200000], HU[200000];
    int n = 0;
    char line[128];
    fgets(line, sizeof line, f);                      /* header */
    while (n < 200000 && fscanf(f, "%f,%f,%f,%f", &T[n], &SO[n], &TE[n], &HU[n]) == 4) n++;
    fclose(f);
    if (n < 2) return 1;
    float step = T[1] - T[0];

    as_state_t s; as_init(&s);
    s.since_photo_s = AS_PI_ROUTINE_S;                /* photo at installation */
    bool active[NRULES] = { false };
    double t = 0, last_photo = -1e9;
    printf("t_s,soil,temp,hum,interval_s,pi_wake,new_alarm\n");
    while (t <= T[n - 1]) {
        int k = (int)(t / step);
        as_reading_t r = rd(SO[k], TE[k], HU[k]);
        float v[3] = { r.hum, r.temp, r.soil };
        static const float band[3] = { AS_HUM_NEAR, AS_TEMP_NEAR, AS_SOIL_NEAR };
        bool new_alarm = false, near = false;
        for (int i = 0; i < NRULES; i++) {
            float x = v[RULES[i].sensor];
            bool on = RULES[i].op == '>' ? x > RULES[i].thr : x < RULES[i].thr;
            if (on && !active[i]) new_alarm = true;
            active[i] = on;
            float d = x - RULES[i].thr; if (d < 0) d = -d;
            if (d <= band[RULES[i].sensor]) near = true;
        }
        s.since_photo_s = (uint32_t)(t - last_photo);
        bool wake = as_should_wake_pi(&s, new_alarm);
        if (wake) last_photo = t;
        uint32_t iv = as_next_interval(&s, &r, near);
        printf("%.0f,%.1f,%.1f,%.1f,%u,%d,%d\n", t, r.soil, r.temp, r.hum, iv, wake, new_alarm);
        t += iv;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--simulate") == 0) return simulate(argv[2]);
    test_backoff();
    test_noise_is_ignored_but_change_resets();
    test_near_threshold_forces_minimum();
    test_failed_read_keeps_reference();
    test_pi_policy();
    puts("all adaptive_sampling tests passed");
    return 0;
}
