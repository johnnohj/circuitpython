// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/budget.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/budget.c — Frame budget tracking via clock_gettime.
//
// Design refs:
//   design/behavior/06-runtime-environments.md  (frame budget model)

#include "port/budget.h"
#include <time.h>

static uint64_t frame_start_ns;
static uint32_t soft_deadline_us = BUDGET_SOFT_US;
static uint32_t firm_deadline_us = BUDGET_FIRM_US;

void budget_frame_start(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    frame_start_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint32_t budget_elapsed_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    return (uint32_t)((now_ns - frame_start_ns) / 1000);
}

bool budget_soft_expired(void) {
    return budget_elapsed_us() >= soft_deadline_us;
}

bool budget_firm_expired(void) {
    return budget_elapsed_us() >= firm_deadline_us;
}

void budget_set_deadlines(uint32_t soft_us, uint32_t firm_us) {
    soft_deadline_us = soft_us ? soft_us : BUDGET_SOFT_US;
    firm_deadline_us = firm_us ? firm_us : BUDGET_FIRM_US;
}

uint32_t budget_get_soft_us(void) { return soft_deadline_us; }
uint32_t budget_get_firm_us(void) { return firm_deadline_us; }
