// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/port_macros.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/macros.h — PLACE_IN_DTCM / PLACE_IN_ITCM / MEMFS_REGION macros.
//
// On ARM Cortex-M, PLACE_IN_DTCM/ITCM place data/code in tightly-coupled
// memory sections via linker script.  On WASM, linear memory is flat — no
// fast/slow distinction — so these are identity macros.
//
// MEMFS_REGION is documentation-only: it marks a variable as backed by
// a MEMFS file at a known path.  Registration with MEMFS happens
// explicitly in port_memory_register().  A future linker script could
// collect .memfs sections and auto-register them.
//
// Design refs:
//   design/wasm-layer.md                 (wasm layer, MEMFS-in-linear-memory)
//   design/behavior/01-hardware-init.md  (GPIO slots as MEMFS regions)

#ifndef PORT_MACROS_H
#define PORT_MACROS_H

// Identity macros — on WASM, all memory is "tightly coupled" (flat
// linear memory).  These exist for documentation and compatibility
// with upstream CircuitPython ports that use them.
//
// On ARM:
//   PLACE_IN_DTCM = __attribute__((section(".dtcm_data")))
//   PLACE_IN_ITCM = __attribute__((section(".itcm_text")))
//
// On WASM:
//   MEMFS-in-linear-memory means the named storage IS the execution
//   memory.  Like XIP — flash mapped to RAM, no copy needed.
#define PLACE_IN_DTCM  /* identity — linear memory is flat */
#define PLACE_IN_ITCM  /* identity — linear memory is flat */

// MEMFS_REGION — annotate a variable as a MEMFS-registered region.
// Currently documentation-only.  A future linker script could use
// __attribute__((section(".memfs"))) to collect these for auto-
// registration.
//
// Usage:
//   MEMFS_REGION("/hal/gpio")
//   static uint8_t gpio_data[GPIO_SLOT_SIZE * GPIO_MAX_PINS];
#define MEMFS_REGION(path)  /* documentation: this is MEMFS file `path` */

#endif // PORT_MACROS_H
