/*
 * chassis/port_step.h — Resumable port state machine.
 *
 * port_step() runs work in budget-aware increments.  When budget
 * expires, it saves position in port_stack_t and returns YIELD.
 * Next frame picks up where it left off.
 *
 * The stack supports nesting: a phase can push a sub-task, which
 * can itself yield and resume.  This is the foundation for the VM
 * to run on the same stack — the VM becomes a work phase that
 * yields at backwards branches.
 */

#ifndef CHASSIS_PORT_STEP_H
#define CHASSIS_PORT_STEP_H

#include <stdint.h>

/* Run the port state machine for one budget slice.
 * Returns PORT_RC_DONE, PORT_RC_YIELD, or PORT_RC_EVENTS. */
uint32_t port_step(void);

/* Submit a workload: N items of work to process.
 * The workload runs across frames, yielding when budget expires. */
void port_submit_work(uint32_t total_items);

/* Check if work is in progress */
int port_work_active(void);

/* Get progress: items completed so far */
uint32_t port_work_progress(void);

#endif /* CHASSIS_PORT_STEP_H */
