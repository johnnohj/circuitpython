/*
 * chassis/port_step.c — Resumable port state machine.
 *
 * Demonstrates halt/resume with zero overhead:
 *   - Work state is in port_stack_t (MEMFS-backed linear memory)
 *   - Budget check between work units
 *   - Yield = save position in stack frame, return
 *   - Resume = read position from stack frame, continue
 *   - C call stack is empty between frames
 *
 * The simulated workload processes N "items" across frames.
 * Each item takes ~10µs of busy work (enough to see budget
 * enforcement without being too slow).
 */

#include "port_step.h"
#include "port_memory.h"
#include "budget.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Work phases — the top-level state machine                           */
/* ------------------------------------------------------------------ */

/* Top-level phases */
#define STEP_IDLE       0   /* no work pending */
#define STEP_WORK       1   /* processing work items */
#define STEP_FINALIZE   2   /* work complete, finalize */

/* STEP_WORK sub-phases: stored in frame.sub_phase */
/* (sub_phase = current item index) */

/* STEP_WORK frame.data layout:
 *   [0..3]  uint32  total_items
 *   [4..7]  uint32  completed_items
 *   [8..11] uint32  checksum (accumulated result)
 */
#define WORK_DATA_TOTAL      0
#define WORK_DATA_COMPLETED  4
#define WORK_DATA_CHECKSUM   8

/* ------------------------------------------------------------------ */
/* Simulated work — expensive enough to test budget enforcement        */
/* ------------------------------------------------------------------ */

static uint32_t do_one_work_item(uint32_t item_index, uint32_t prev_checksum) {
    /* Simulate ~10µs of work per item.
     * On WASM at ~1GHz effective, ~10K iterations ≈ 10µs. */
    volatile uint32_t acc = prev_checksum;
    for (int i = 0; i < 5000; i++) {
        acc = acc * 31 + item_index + i;
    }
    return acc;
}

/* ------------------------------------------------------------------ */
/* Stack helpers                                                       */
/* ------------------------------------------------------------------ */

static inline uint32_t frame_u32(port_stack_frame_t *f, int offset) {
    uint32_t val;
    memcpy(&val, &f->data[offset], sizeof(val));
    return val;
}

static inline void frame_set_u32(port_stack_frame_t *f, int offset, uint32_t val) {
    memcpy(&f->data[offset], &val, sizeof(val));
}

/* ------------------------------------------------------------------ */
/* port_step — run one budget slice of the state machine               */
/* ------------------------------------------------------------------ */

uint32_t port_step(void) {
    port_stack_t *stk = port_stack();

    if (!(stk->flags & STACK_FLAG_ACTIVE)) {
        return PORT_RC_DONE;  /* no work */
    }

    port_stack_frame_t *f = &stk->frames[stk->depth];

    switch (f->phase) {

    case STEP_WORK: {
        uint32_t total = frame_u32(f, WORK_DATA_TOTAL);
        uint32_t completed = frame_u32(f, WORK_DATA_COMPLETED);
        uint32_t checksum = frame_u32(f, WORK_DATA_CHECKSUM);

        /* Process items until budget or completion */
        while (completed < total) {
            checksum = do_one_work_item(completed, checksum);
            completed++;

            /* Save progress after each item */
            frame_set_u32(f, WORK_DATA_COMPLETED, completed);
            frame_set_u32(f, WORK_DATA_CHECKSUM, checksum);

            /* Budget check — soft deadline means "wrap up" */
            if (budget_soft_expired()) {
                stk->flags |= STACK_FLAG_YIELDED;
                return PORT_RC_YIELD;
            }
        }

        /* All items done — advance to finalize */
        f->phase = STEP_FINALIZE;
        /* fall through */
    }

    case STEP_FINALIZE: {
        /* Work complete — clear stack */
        stk->flags = (stk->flags & ~(STACK_FLAG_ACTIVE | STACK_FLAG_YIELDED))
                     | STACK_FLAG_COMPLETE;
        return PORT_RC_DONE;
    }

    default:
        return PORT_RC_ERROR;
    }
}

/* ------------------------------------------------------------------ */
/* Submit / Query                                                      */
/* ------------------------------------------------------------------ */

void port_submit_work(uint32_t total_items) {
    port_stack_t *stk = port_stack();

    /* Reset stack */
    memset(stk, 0, sizeof(*stk));
    stk->depth = 0;
    stk->flags = STACK_FLAG_ACTIVE;

    /* Set up frame 0 for STEP_WORK */
    port_stack_frame_t *f = &stk->frames[0];
    f->phase = STEP_WORK;
    f->sub_phase = 0;
    frame_set_u32(f, WORK_DATA_TOTAL, total_items);
    frame_set_u32(f, WORK_DATA_COMPLETED, 0);
    frame_set_u32(f, WORK_DATA_CHECKSUM, 0);
}

int port_work_active(void) {
    return (port_stack()->flags & STACK_FLAG_ACTIVE) ? 1 : 0;
}

uint32_t port_work_progress(void) {
    port_stack_frame_t *f = &port_stack()->frames[0];
    return frame_u32(f, WORK_DATA_COMPLETED);
}
