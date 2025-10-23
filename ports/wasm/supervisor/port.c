// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port supervisor implementation with virtual timing

#include <stdint.h>
#include <string.h>

#include "supervisor/background_callback.h"
#include "supervisor/port.h"
#include "shared-bindings/microcontroller/__init__.h"
#include "supervisor/shared/safe_mode.h"
#include "shared_memory.h"
#include "common-hal/microcontroller/Pin.h"

// =============================================================================
// PIN INITIALIZATION
// =============================================================================
//
// WASM port: Single default board with all 64 GPIO pins enabled
// All pins have full capabilities (digital I/O, analog, PWM, I2C, SPI, UART)
// No profile system - just maximum flexibility for browser-based development

// Enable all 64 GPIO pins at startup
void enable_all_pins(void) {
    // Enable all 4 banks
    enable_gpio_bank_0(true);  // GPIO0-15
    enable_gpio_bank_1(true);  // GPIO16-31
    enable_gpio_bank_2(true);  // GPIO32-47
    enable_gpio_bank_3(true);  // GPIO48-63

    // All pins start enabled
    for (uint8_t i = 0; i < 64; i++) {
        mcu_pin_obj_t *pin = get_pin_by_number(i);
        if (pin != NULL) {
            pin->enabled = true;
        }
    }
}

// =============================================================================
// Port Initialization
// =============================================================================

safe_mode_t port_init(void) {
    // Reset everything into a known state
    reset_port();

    // Enable all 64 GPIO pins
    enable_all_pins();

    return SAFE_MODE_NONE;
}

void reset_port(void) {
    // Nothing to reset in WASM - JavaScript handles hardware state
}

void reset_to_bootloader(void) {
    // In WASM, "bootloader mode" is repurposed for profile loading/switching
    // The next_profile_id has already been set by common_hal_mcu_on_next_reset()
    // On next reset, port_init() will apply the new profile

    // For now, loop forever (actual reset happens via JavaScript)
    while (true) {
    }
}

void reset_cpu(void) {
    // Handled by common-hal/microcontroller/__init__.c (sends MSG_TYPE_MCU_RESET)
    while (true) {
    }
}

uint32_t *port_stack_get_limit(void) {
    // WASM has its own stack management
    return NULL;
}

uint32_t *port_stack_get_top(void) {
    return NULL;
}

uint32_t *port_heap_get_bottom(void) {
    // Emscripten manages heap
    extern uint32_t _ld_heap_start;
    return &_ld_heap_start;
}

uint32_t *port_heap_get_top(void) {
    extern uint32_t _ld_heap_end;
    return &_ld_heap_end;
}

static uint32_t saved_word;

void port_set_saved_word(uint32_t value) {
    saved_word = value;
}

uint32_t port_get_saved_word(void) {
    return saved_word;
}

// =============================================================================
// VIRTUAL TIMING - THE HEART OF THE DUAL CLOCK SYSTEM
// =============================================================================
//
// This is like Renode's Simple32kHz.cs - JavaScript controls the virtual
// crystal oscillator by directly writing to shared memory.
//
// WASM reads the counter instantly (no message queue!) just like reading
// from hardware address 0x40001000 in Renode.
//
// The supervisor converts these raw ticks to milliseconds:
//   supervisor_ticks_ms64() = (raw_ticks * 1000) / 1024
//

uint64_t port_get_raw_ticks(uint8_t *subticks) {
    // Read from shared memory - instant, no yielding!
    // JavaScript increments virtual_hardware.ticks_32khz
    uint64_t ticks_32khz = read_virtual_ticks_32khz();

    // Convert 32kHz to 1024Hz (like all CircuitPython ports)
    uint64_t ticks_1024hz = ticks_32khz / 32;

    // Optionally return sub-tick precision (0-31)
    if (subticks != NULL) {
        *subticks = (uint8_t)(ticks_32khz % 32);
    }

    return ticks_1024hz;
}

static volatile bool ticks_enabled = false;
static volatile bool _woken_up = false;

// Enable 1/1024 second tick
void port_enable_tick(void) {
    ticks_enabled = true;
}

// Disable 1/1024 second tick
void port_disable_tick(void) {
    ticks_enabled = false;
}

// Called by sleep functions
void port_interrupt_after_ticks(uint32_t ticks) {
    _woken_up = false;
    // In WASM, JavaScript advances virtual time during yields
    // No interrupt setup needed
}

void port_idle_until_interrupt(void) {
    // In WASM, this is where we yield to JavaScript
    // JavaScript can then:
    // 1. Advance virtual time
    // 2. Process hardware events
    // 3. Handle user input
    common_hal_mcu_disable_interrupts();
    if (!background_callback_pending() && !_woken_up) {
        // Yield point - JavaScript event loop runs here
        // In real hardware this would be __WFI() (wait for interrupt)
    }
    common_hal_mcu_enable_interrupts();
}

void port_yield(void) {
    // Another yield point for JavaScript
}

void port_boot_info(void) {
    // Could log to JavaScript console
}

// Background task hooks
void port_start_background_tick(void) {
}

void port_finish_background_tick(void) {
    // Update yield statistics
    virtual_hardware.wasm_yields_count++;
}

void port_background_tick(void) {
    // Called during RUN_BACKGROUND_TASKS
    // JavaScript can process events here
}

// =============================================================================
// SUPERVISOR UTILITY FUNCTIONS (from supervisor_stubs.c)
// =============================================================================

#include <time.h>
#include <emscripten.h>
#include "supervisor/shared/translate/translate.h"

// Forward declare types
typedef struct _fs_user_mount_t fs_user_mount_t;

// Safe mode reset - in WASM this is a no-op
// Unlike physical boards, we keep everything running
void reset_into_safe_mode(safe_mode_t reason) {
    (void)reason;
    // In WASM, "safe mode" just means we had an error
    // But we don't actually disable anything - the REPL continues working
}

// Stack checking - WASM manages its own stack
bool stack_ok(void) {
    return true; // Assume stack is always OK in WASM
}

// Heap assertion - WASM/Emscripten manages heap
void assert_heap_ok(void) {
    // No-op in WASM
}

// Filesystem writable stubs - Emscripten VFS is always writable
bool filesystem_is_writable_by_python(fs_user_mount_t *vfs) {
    (void)vfs;
    return true;
}

void filesystem_set_writable_by_usb(fs_user_mount_t *vfs, bool writable) {
    (void)vfs;
    (void)writable;
    // No-op in WASM
}

// FAT filesystem timestamp function
// Returns a packed date/time value in FAT format
uint32_t get_fattime(void) {
    // Get current time in milliseconds since epoch
    double now_ms = emscripten_get_now();
    time_t now_sec = (time_t)(now_ms / 1000.0);

    // Convert to broken-down time (UTC)
    struct tm *timeinfo = gmtime(&now_sec);

    if (timeinfo == NULL) {
        // Fallback to a fixed date if time conversion fails
        // 2024-01-01 00:00:00
        return ((uint32_t)(44 << 25) | (1 << 21) | (1 << 16));
    }

    // FAT timestamp format:
    // Year: 7 bits (0-127, representing 1980-2107)
    // Month: 4 bits (1-12)
    // Day: 5 bits (1-31)
    // Hour: 5 bits (0-23)
    // Minute: 6 bits (0-59)
    // Second: 5 bits (0-29, representing 0-58 in 2-second intervals)

    uint32_t year = (timeinfo->tm_year + 1900) - 1980; // tm_year is years since 1900
    uint32_t month = timeinfo->tm_mon + 1;             // tm_mon is 0-11
    uint32_t day = timeinfo->tm_mday;                  // 1-31
    uint32_t hour = timeinfo->tm_hour;                 // 0-23
    uint32_t minute = timeinfo->tm_min;                // 0-59
    uint32_t second = timeinfo->tm_sec / 2;            // 0-29 (2-second resolution)

    return ((year & 0x7F) << 25) |
           ((month & 0x0F) << 21) |
           ((day & 0x1F) << 16) |
           ((hour & 0x1F) << 11) |
           ((minute & 0x3F) << 5) |
           (second & 0x1F);
}
