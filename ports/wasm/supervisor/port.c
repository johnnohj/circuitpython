// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port supervisor implementation with cooperative yielding

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <emscripten.h>

#include "supervisor/background_callback.h"
#include "supervisor/port.h"
#include "shared-bindings/microcontroller/__init__.h"
#include "supervisor/shared/safe_mode.h"
#include "supervisor/shared/tick.h"
#include "common-hal/microcontroller/Pin.h"
#include "common-hal/digitalio/DigitalInOut.h"
#include "common-hal/analogio/AnalogIn.h"
#include "proxy_c.h"

// =============================================================================
// COOPERATIVE YIELDING STATE
// =============================================================================

static volatile uint64_t wasm_yields_count = 0;
volatile bool wasm_should_yield_to_js = false;
static uint32_t wasm_bytecode_count = 0;
static double last_yield_time = 0;

// Tunable parameters - adjust based on testing
// NOTE: mp_js_hook is called every 10 bytecodes (MICROPY_VM_HOOK_COUNT = 10)
// ASYNCIFY has limits - yield LESS frequently to avoid "unreachable" errors
// Longer intervals = fewer unwind/rewind cycles = more stability
#define HOOK_CALLS_PER_YIELD 100     // Check timing every 100 hook calls (1000 bytecodes)
#define YIELD_INTERVAL_MS 100.0      // Yield every ~100ms (less frequent = more stable)

// Called from RUN_BACKGROUND_TASKS macro (defined in mpconfigport.h)
// This is the heart of the cooperative yielding system
void wasm_check_yield_point(void) {
    wasm_bytecode_count++;

    // Only check time periodically to reduce overhead
    if (wasm_bytecode_count >= HOOK_CALLS_PER_YIELD) {
        double now = emscripten_get_now();

        // Yield if enough time has passed
        if (now - last_yield_time >= YIELD_INTERVAL_MS) {
            wasm_should_yield_to_js = true;
            wasm_bytecode_count = 0;
            last_yield_time = now;
        } else {
            // Reset counter but don't yield yet
            wasm_bytecode_count = 0;
        }
    }
}

// Called from JavaScript to reset yield state before each iteration
EMSCRIPTEN_KEEPALIVE
void wasm_reset_yield_state(void) {
    wasm_should_yield_to_js = false;
    wasm_bytecode_count = 0;
}

// Query yield state from JavaScript (for debugging)
EMSCRIPTEN_KEEPALIVE
bool wasm_get_yield_state(void) {
    return wasm_should_yield_to_js;
}

// =============================================================================
// PIN INITIALIZATION
// =============================================================================

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
// PORT INITIALIZATION AND RESET
// =============================================================================

safe_mode_t port_init(void) {
    // Enable all 64 GPIO pins (only needed once at startup)
    enable_all_pins();

    // Reset peripherals to safe state
    reset_port();

    // Initialize yield timing
    last_yield_time = emscripten_get_now();

    return SAFE_MODE_NONE;
}

void reset_port(void) {
    // Reset all peripherals to safe defaults
    // This is called at startup AND between REPL runs (soft reset)

    extern void digitalio_reset_gpio_state(void);
    extern void analogio_reset_analog_state(void);
    extern void pwmio_reset_pwm_state(void);
    extern void neopixel_reset_state(void);
    extern void busio_reset_i2c_state(void);
    extern void busio_reset_uart_state(void);
    extern void busio_reset_spi_state(void);

    digitalio_reset_gpio_state();   // All GPIO → input mode
    analogio_reset_analog_state();  // All analog → disabled
    pwmio_reset_pwm_state();        // All PWM → disabled, 500Hz
    neopixel_reset_state();         // All NeoPixels → off
    busio_reset_i2c_state();        // All I2C buses → disabled
    busio_reset_uart_state();       // All UART ports → disabled
    busio_reset_spi_state();        // All SPI buses → disabled

    // NOTE: We don't reset raw_ticks (the tick counter used by time.monotonic())
    // Like a hardware crystal oscillator, it keeps running across soft resets
    // to maintain time continuity between REPL sessions.
}

void reset_to_bootloader(void) {
    // In WASM, "bootloader mode" is repurposed for profile loading/switching
    // For now, loop forever (actual reset happens via JavaScript)
    while (true) {
    }
}

void reset_cpu(void) {
    // Handled by JavaScript
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
// TIMING AND TICK MANAGEMENT
// =============================================================================

static volatile uint64_t raw_ticks = 0;

// Called from JavaScript setInterval(1ms)
// This simulates the hardware tick interrupt
// IMPORTANT: Only ticks when not executing Python to avoid nlr_jump_fail crashes
EMSCRIPTEN_KEEPALIVE
void supervisor_tick_from_js(void) {
    raw_ticks++;

    // TEMPORARILY DISABLED to test if this is causing crashes
    // Only tick if we're not currently executing Python code
    // if (external_call_depth_get() == 0) {
    //     supervisor_tick();  // Safe to trigger background tasks
    // }

    // Don't call supervisor_tick at all for now - just accumulate ticks
}

uint64_t port_get_raw_ticks(uint8_t* subticks) {
    if (subticks) *subticks = 0;
    return raw_ticks;
}

void port_background_tick(void) {
    // Called from supervisor_tick() in interrupt-like context
    // Keep this FAST - just set flags
    // Actual work happens in port_background_task()
}

void port_start_background_tick(void) {
    // No-op for WASM - ticks come from JavaScript
}

void port_finish_background_tick(void) {
    // Update yield statistics
    wasm_yields_count++;
}

// =============================================================================
// BACKGROUND TASKS - RUNS BETWEEN BYTECODES
// =============================================================================

void port_background_task(void) {
    // Called by background_callback_run_all() in supervisor/shared/background_callback.c
    // This is called BEFORE running the callback queue and happens *very* often
    // Keep this lightweight!
    
    // Service virtual hardware (if needed)
    // In future: Web Serial, canvas updates, etc.
}

// =============================================================================
// IDLE AND YIELD POINTS
// =============================================================================

static volatile bool _woken_up = false;

void port_interrupt_after_ticks(uint32_t ticks) {
    _woken_up = false;
    // In WASM, JavaScript advances virtual time during yields
    // No interrupt setup needed
}

void port_idle_until_interrupt(void) {
    // In WASM, we can't truly idle - just check for yield
    // In real hardware this would be __WFI() (wait for interrupt)
    common_hal_mcu_disable_interrupts();
    if (!background_callback_pending() && !wasm_should_yield_to_js) {
        // Could yield to JavaScript here
    }
    common_hal_mcu_enable_interrupts();
}

void port_yield(void) {
    // Another yield point for JavaScript
    // Currently a no-op, but could set wasm_should_yield_to_js
}

void port_boot_info(void) {
    // Could log to JavaScript console
}

// =============================================================================
// SUPERVISOR UTILITY FUNCTIONS
// =============================================================================

#include <time.h>
#include "supervisor/shared/translate/translate.h"

// Forward declare types
typedef struct _fs_user_mount_t fs_user_mount_t;

// Safe mode reset - in WASM this triggers a controlled abort
void reset_into_safe_mode(safe_mode_t reason) {
    (void)reason;
    // In WASM, we abort the execution to match the noreturn contract
    abort();
}

// Stack checking - WASM manages its own stack
bool stack_ok(void) {
    return true; // Assume stack is always OK in WASM
}

// Heap assertion - WASM/Emscripten manages heap
void assert_heap_ok(void) {
    // No-op in WASM
}

// =============================================================================
// FAT FILESYSTEM SUPPORT
// =============================================================================

uint32_t get_fattime(void) {
    // Get current time in milliseconds since epoch
    double now_ms = emscripten_get_now();
    time_t now_sec = (time_t)(now_ms / 1000.0);

    // Convert to broken-down time (UTC)
    struct tm *timeinfo = gmtime(&now_sec);

    if (timeinfo == NULL) {
        // Fallback to a fixed date if time conversion fails
        // 2024-01-01 00:00:00
        return ((2024 - 1980) << 25) | (1 << 21) | (1 << 16);
    }

    // FAT timestamp format (packed 32-bit):
    // Bits 31-25: Year (0 = 1980, 127 = 2107)
    // Bits 24-21: Month (1-12)
    // Bits 20-16: Day (1-31)
    // Bits 15-11: Hour (0-23)
    // Bits 10-5:  Minute (0-59)
    // Bits 4-0:   Second/2 (0-29)

    uint32_t year = (timeinfo->tm_year + 1900) - 1980;  // tm_year is years since 1900
    uint32_t month = timeinfo->tm_mon + 1;              // tm_mon is 0-11
    uint32_t day = timeinfo->tm_mday;                   // 1-31
    uint32_t hour = timeinfo->tm_hour;                  // 0-23
    uint32_t minute = timeinfo->tm_min;                 // 0-59
    uint32_t second = timeinfo->tm_sec / 2;             // 0-29 (FAT uses 2-second resolution)

    return (year << 25) | (month << 21) | (day << 16) |
           (hour << 11) | (minute << 5) | second;
}

// =============================================================================
// DEBUGGING AND STATISTICS
// =============================================================================

// Get yield statistics (callable from JavaScript)
EMSCRIPTEN_KEEPALIVE
uint64_t wasm_get_yield_count(void) {
    return wasm_yields_count;
}

EMSCRIPTEN_KEEPALIVE
uint32_t wasm_get_bytecode_count(void) {
    return wasm_bytecode_count;
}

EMSCRIPTEN_KEEPALIVE
double wasm_get_last_yield_time(void) {
    return last_yield_time;
}

// =============================================================================
// ASYNCIFY-BASED COOPERATIVE YIELDING (asyncified variant only)
// =============================================================================

#ifdef EMSCRIPTEN_ASYNCIFY_ENABLED
// For asyncified variant: C implementation that can yield via ASYNCIFY
// Called from JavaScript library's mp_js_hook() wrapper

static volatile uint64_t asyncify_yields_count = 0;
static volatile uint64_t asyncify_hook_calls = 0;

EMSCRIPTEN_KEEPALIVE
void mp_js_hook_asyncify_impl(void) {
    asyncify_hook_calls++;  // Track every call for debugging

    // Check if it's time to yield based on timing
    wasm_check_yield_point();

    // If we should yield, use emscripten_sleep to yield to event loop
    if (wasm_should_yield_to_js) {
        // Reset yield state before sleeping
        wasm_should_yield_to_js = false;

        // Increment yield counter for debugging
        asyncify_yields_count++;

        // Yield to JavaScript event loop - this is the magic!
        // ASYNCIFY will unwind the stack, run JS tasks, then rewind
        emscripten_sleep(0);  // Sleep for 0ms = just yield and resume
    }
}

// Get ASYNCIFY yield statistics (callable from JavaScript)
EMSCRIPTEN_KEEPALIVE
uint64_t wasm_get_asyncify_yield_count(void) {
    return asyncify_yields_count;
}

EMSCRIPTEN_KEEPALIVE
uint64_t wasm_get_asyncify_hook_calls(void) {
    return asyncify_hook_calls;
}

#endif  // EMSCRIPTEN_ASYNCIFY_ENABLED