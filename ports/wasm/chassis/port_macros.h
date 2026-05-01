/*
 * chassis/port_macros.h — PLACE_IN_DTCM / PLACE_IN_ITCM macros.
 *
 * On ARM Cortex-M, these macros place data/code in tightly-coupled
 * memory sections.  The linker script collects them; the startup code
 * copies from flash to fast SRAM.
 *
 * On WASM with MEMFS-in-linear-memory, there's no fast/slow memory
 * distinction — linear memory is flat.  But the concept still applies:
 * PLACE_IN_DTCM marks data that should be at a known, stable address
 * accessible to both C and JS via MEMFS.
 *
 * For the chassis, these are identity macros.  The registration with
 * MEMFS happens explicitly in port_memory_register().  In the future,
 * a linker script could collect .memfs sections and auto-register them.
 *
 * Usage:
 *   PLACE_IN_DTCM static uint32_t my_counter;
 *   PLACE_IN_ITCM void my_hot_function(void) { ... }
 */

#ifndef CHASSIS_PORT_MACROS_H
#define CHASSIS_PORT_MACROS_H

/*
 * Identity macros — on WASM, all memory is "tightly coupled" (flat
 * linear memory).  These exist for documentation and compatibility
 * with upstream CircuitPython ports.
 *
 * On ARM:
 *   PLACE_IN_DTCM = __attribute__((section(".dtcm_data")))
 *   PLACE_IN_ITCM = __attribute__((section(".itcm_text")))
 *
 * On WASM (this port):
 *   MEMFS-in-linear-memory means the named storage IS the execution
 *   memory.  Like XIP — flash mapped to RAM, no copy needed.
 */
#define PLACE_IN_DTCM  /* identity — linear memory is flat */
#define PLACE_IN_ITCM  /* identity — linear memory is flat */

/*
 * MEMFS_REGION — annotate a variable as a MEMFS-registered region.
 * Currently documentation-only.  A future linker script could use
 * __attribute__((section(".memfs"))) to collect these for auto-
 * registration.
 *
 * Usage:
 *   MEMFS_REGION("/hal/gpio")
 *   static uint8_t gpio_data[768];
 */
#define MEMFS_REGION(path)  /* documentation: this is MEMFS file `path` */

#endif /* CHASSIS_PORT_MACROS_H */
