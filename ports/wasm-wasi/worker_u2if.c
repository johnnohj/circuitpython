/*
 * worker_u2if.c — U2IF command dispatcher for the worker.
 *
 * JS calls worker_u2if_dispatch(packet_ptr, response_ptr) when a
 * U2IF command arrives via postMessage from weBlinka.  The packet
 * is a 64-byte U2IF report in WASM linear memory.  The response
 * is written to response_ptr (64 bytes).
 *
 * Dispatches to the same state arrays used by the C common-hal,
 * so the worker's display/terminal/etc. see the changes immediately.
 */

#include <string.h>
#include <stdint.h>

/* State arrays from common-hal */
#include "common-hal/digitalio/DigitalInOut.h"
#include "common-hal/pwmio/PWMOut.h"
#include "common-hal/analogio/AnalogIn.h"
#include "hw_opfs.h"

/* U2IF opcodes (matches weblinka.js CMD constants) */
#define U2IF_GPIO_INIT          0x20
#define U2IF_GPIO_SET_VALUE     0x21
#define U2IF_GPIO_GET_VALUE     0x22

#define U2IF_PWM_INIT           0x30
#define U2IF_PWM_DEINIT         0x31
#define U2IF_PWM_SET_FREQ       0x32
#define U2IF_PWM_GET_FREQ       0x33
#define U2IF_PWM_SET_DUTY       0x34
#define U2IF_PWM_GET_DUTY       0x35

#define U2IF_ADC_INIT           0x40
#define U2IF_ADC_GET_VALUE      0x41

#define U2IF_STATUS_OK          0x01
#define U2IF_STATUS_NOK         0x02
#define U2IF_REPORT_SIZE        64

__attribute__((export_name("worker_u2if_dispatch")))
int worker_u2if_dispatch(uint8_t *packet, uint8_t *response) {
    memset(response, 0, U2IF_REPORT_SIZE);
    response[0] = 0x00;  /* report ID */
    response[1] = U2IF_STATUS_OK;

    uint8_t cmd = packet[1];

    switch (cmd) {

    /* ── GPIO ─────────────────────────────────────── */

    case U2IF_GPIO_INIT: {
        uint8_t pin  = packet[2];
        uint8_t mode = packet[3];  /* 0=IN, 1=OUT */
        uint8_t pull = packet[4];  /* 0=none, 1=up, 2=down */
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        gpio_state[pin].enabled   = true;
        gpio_state[pin].direction = mode;
        gpio_state[pin].pull      = pull;
        gpio_state[pin].value     = false;
        gpio_state[pin].open_drain = false;
        hw_opfs_gpio_dirty = true;
        break;
    }

    case U2IF_GPIO_SET_VALUE: {
        uint8_t pin = packet[2];
        uint8_t val = packet[3];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        gpio_state[pin].value = val ? true : false;
        hw_opfs_gpio_dirty = true;
        break;
    }

    case U2IF_GPIO_GET_VALUE: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        response[2] = gpio_state[pin].value ? 1 : 0;
        break;
    }

    /* ── PWM ──────────────────────────────────────── */

    case U2IF_PWM_INIT: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        pwm_state[pin].enabled = true;
        hw_opfs_pwm_dirty = true;
        break;
    }

    case U2IF_PWM_DEINIT: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        pwm_state[pin].enabled = false;
        pwm_state[pin].duty_cycle = 0;
        pwm_state[pin].frequency = 0;
        hw_opfs_pwm_dirty = true;
        break;
    }

    case U2IF_PWM_SET_FREQ: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        uint32_t freq;
        memcpy(&freq, &packet[3], 4);
        pwm_state[pin].frequency = freq;
        hw_opfs_pwm_dirty = true;
        break;
    }

    case U2IF_PWM_GET_FREQ: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        uint32_t freq = pwm_state[pin].frequency;
        memcpy(&response[2], &freq, 4);
        break;
    }

    case U2IF_PWM_SET_DUTY: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        uint16_t duty;
        memcpy(&duty, &packet[3], 2);
        pwm_state[pin].duty_cycle = duty;
        hw_opfs_pwm_dirty = true;
        break;
    }

    case U2IF_PWM_GET_DUTY: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        uint16_t duty = pwm_state[pin].duty_cycle;
        memcpy(&response[2], &duty, 2);
        break;
    }

    /* ── ADC ──────────────────────────────────────── */

    case U2IF_ADC_INIT: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        analog_state[pin].enabled = 1;
        analog_state[pin].is_output = 0;
        analog_state[pin].value = 32768;  /* midpoint default */
        hw_opfs_analog_dirty = true;
        break;
    }

    case U2IF_ADC_GET_VALUE: {
        uint8_t pin = packet[2];
        if (pin >= 64) { response[1] = U2IF_STATUS_NOK; break; }
        uint16_t val = analog_state[pin].value;
        response[3] = val & 0xFF;
        response[4] = (val >> 8) & 0xFF;
        break;
    }

    default:
        response[1] = U2IF_STATUS_NOK;
        break;
    }

    return response[1] == U2IF_STATUS_OK ? 0 : -1;
}

/*
 * Scratch area for JS to write U2IF packets into WASM linear memory.
 * JS writes the 64-byte packet here, then calls worker_u2if_dispatch.
 */
static uint8_t _u2if_packet[U2IF_REPORT_SIZE];
static uint8_t _u2if_response[U2IF_REPORT_SIZE];

__attribute__((export_name("worker_u2if_packet_ptr")))
uintptr_t worker_u2if_packet_ptr(void) {
    return (uintptr_t)_u2if_packet;
}

__attribute__((export_name("worker_u2if_response_ptr")))
uintptr_t worker_u2if_response_ptr(void) {
    return (uintptr_t)_u2if_response;
}

__attribute__((export_name("worker_u2if_exec")))
int worker_u2if_exec(void) {
    return worker_u2if_dispatch(_u2if_packet, _u2if_response);
}

/* ------------------------------------------------------------------ */
/* State diff export — worker → main thread via U2IF packets           */
/* ------------------------------------------------------------------ */
/*
 * When hw dirty flags are set after worker_step(), JS calls
 * worker_u2if_get_diff_count() to check how many state-change
 * packets are pending, then worker_u2if_get_diff_ptr() to get
 * the buffer of packed U2IF reports.
 *
 * Each changed pin emits one 64-byte U2IF packet:
 *   GPIO:   GPIO_INIT packet with full pin state
 *   PWM:    PWM_SET_DUTY + PWM_SET_FREQ (two packets)
 *   Analog: ADC_GET_VALUE response format
 *
 * Max: 64 GPIO + 64 PWM*2 + 64 ADC = 256 packets × 64 bytes = 16KB
 * In practice, only changed pins emit, so typically 1-5 packets.
 */

/* Previous state for diffing */
static gpio_pin_state_t _prev_gpio[64];
static pwm_state_t      _prev_pwm[64];
static analog_pin_state_t _prev_analog[64];
static bool _prev_initialized = false;

/* Diff output buffer: max 256 packets */
#define MAX_DIFF_PACKETS 256
static uint8_t _diff_buf[MAX_DIFF_PACKETS * U2IF_REPORT_SIZE];
static int _diff_count = 0;

static void _emit_diff_packet(uint8_t opcode, uint8_t pin,
                               uint8_t b2, uint8_t b3,
                               uint8_t b4, uint8_t b5,
                               uint8_t b6, uint8_t b7) {
    if (_diff_count >= MAX_DIFF_PACKETS) return;
    uint8_t *p = &_diff_buf[_diff_count * U2IF_REPORT_SIZE];
    memset(p, 0, U2IF_REPORT_SIZE);
    p[0] = 0x00;   /* report ID */
    p[1] = opcode;
    p[2] = pin;
    p[3] = b2;
    p[4] = b3;
    p[5] = b4;
    p[6] = b5;
    p[7] = b6;
    p[8] = b7;
    _diff_count++;
}

__attribute__((export_name("worker_u2if_build_diff")))
int worker_u2if_build_diff(void) {
    _diff_count = 0;

    if (!_prev_initialized) {
        /* First call: snapshot everything, emit full state for enabled pins */
        memcpy(_prev_gpio, gpio_state, sizeof(_prev_gpio));
        memcpy(_prev_pwm, pwm_state, sizeof(_prev_pwm));
        memcpy(_prev_analog, analog_state, sizeof(_prev_analog));
        _prev_initialized = true;

        for (int i = 0; i < 64; i++) {
            if (gpio_state[i].enabled) {
                _emit_diff_packet(U2IF_GPIO_INIT, i,
                    gpio_state[i].direction,
                    gpio_state[i].pull,
                    gpio_state[i].value ? 1 : 0,
                    0, 0, 0);
            }
        }
        return _diff_count;
    }

    /* GPIO diffs */
    if (hw_opfs_gpio_dirty) {
        for (int i = 0; i < 64; i++) {
            if (memcmp(&gpio_state[i], &_prev_gpio[i], sizeof(gpio_pin_state_t)) != 0) {
                /* Emit GPIO_INIT with full state */
                _emit_diff_packet(U2IF_GPIO_INIT, i,
                    gpio_state[i].direction,
                    gpio_state[i].pull,
                    gpio_state[i].value ? 1 : 0,
                    gpio_state[i].open_drain ? 1 : 0,
                    gpio_state[i].enabled ? 1 : 0,
                    0);
                _prev_gpio[i] = gpio_state[i];
            }
        }
        hw_opfs_gpio_dirty = false;
    }

    /* PWM diffs */
    if (hw_opfs_pwm_dirty) {
        for (int i = 0; i < 64; i++) {
            if (memcmp(&pwm_state[i], &_prev_pwm[i], sizeof(pwm_state_t)) != 0) {
                /* Emit PWM state: duty in [3:5], freq in [5:9] */
                uint8_t duty_lo = pwm_state[i].duty_cycle & 0xFF;
                uint8_t duty_hi = (pwm_state[i].duty_cycle >> 8) & 0xFF;
                uint8_t f0 = pwm_state[i].frequency & 0xFF;
                uint8_t f1 = (pwm_state[i].frequency >> 8) & 0xFF;
                uint8_t f2 = (pwm_state[i].frequency >> 16) & 0xFF;
                uint8_t f3 = (pwm_state[i].frequency >> 24) & 0xFF;
                _emit_diff_packet(U2IF_PWM_SET_DUTY, i,
                    duty_lo, duty_hi, f0, f1, f2, f3);
                _prev_pwm[i] = pwm_state[i];
            }
        }
        hw_opfs_pwm_dirty = false;
    }

    /* Analog diffs */
    if (hw_opfs_analog_dirty) {
        for (int i = 0; i < 64; i++) {
            if (memcmp(&analog_state[i], &_prev_analog[i], sizeof(analog_pin_state_t)) != 0) {
                _emit_diff_packet(U2IF_ADC_GET_VALUE, i,
                    0, /* padding to match response offset */
                    analog_state[i].value & 0xFF,
                    (analog_state[i].value >> 8) & 0xFF,
                    analog_state[i].enabled ? 1 : 0,
                    0, 0);
                _prev_analog[i] = analog_state[i];
            }
        }
        hw_opfs_analog_dirty = false;
    }

    return _diff_count;
}

__attribute__((export_name("worker_u2if_diff_ptr")))
uintptr_t worker_u2if_diff_ptr(void) {
    return (uintptr_t)_diff_buf;
}

__attribute__((export_name("worker_u2if_diff_count")))
int worker_u2if_diff_count(void) {
    return _diff_count;
}
