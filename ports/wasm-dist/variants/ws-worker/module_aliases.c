// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-License-Identifier: MIT

// module_aliases.c — Register C modules under _prefixed names.
//
// This lets frozen Python wrappers shadow the original names:
//   import digitalio  → loads frozen digitalio.py (protocol wrapper)
//     → from _digitalio import *  → loads C module
//
// The C module object is the same — just a second registration.
// No upstream changes needed.  The _prefix convention matches
// CPython's pattern (_io, _thread, _collections, etc.).

#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

// ── Module extern declarations ──
// These are the module objects defined in shared-bindings/*/__init__.c

#if CIRCUITPY_DIGITALIO
extern const mp_obj_module_t digitalio_module;
MP_REGISTER_MODULE(MP_QSTR__digitalio, digitalio_module);
#endif

#if CIRCUITPY_ANALOGIO
extern const mp_obj_module_t analogio_module;
MP_REGISTER_MODULE(MP_QSTR__analogio, analogio_module);
#endif

#if CIRCUITPY_BUSIO
extern const mp_obj_module_t busio_module;
MP_REGISTER_MODULE(MP_QSTR__busio, busio_module);
#endif

#if CIRCUITPY_PWMIO
extern const mp_obj_module_t pwmio_module;
MP_REGISTER_MODULE(MP_QSTR__pwmio, pwmio_module);
#endif

#if CIRCUITPY_ROTARYIO
extern const mp_obj_module_t rotaryio_module;
MP_REGISTER_MODULE(MP_QSTR__rotaryio, rotaryio_module);
#endif

#if CIRCUITPY_NEOPIXEL_WRITE
extern const mp_obj_module_t neopixel_write_module;
MP_REGISTER_MODULE(MP_QSTR__neopixel_write, neopixel_write_module);
#endif

#if CIRCUITPY_MICROCONTROLLER
extern const mp_obj_module_t microcontroller_module;
MP_REGISTER_MODULE(MP_QSTR__microcontroller, microcontroller_module);
#endif
