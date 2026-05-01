/**
 * chassis-constants.mjs — JS mirror of chassis_constants.h.
 *
 * CANONICAL SOURCE: chassis_constants.h
 * This file must match exactly.  When adding constants, update both.
 */

// ── Frame return codes ──
export const RC_DONE    = 0;
export const RC_YIELD   = 1;
export const RC_EVENTS  = 2;
export const RC_ERROR   = 3;

export const RC_NAMES = ['DONE', 'YIELD', 'EVENTS', 'ERROR'];

// ── Port flags (port_state_t.flags) ──
export const PF_INITIALIZED = 1 << 0;
export const PF_HAS_EVENTS  = 1 << 1;
export const PF_HAL_DIRTY   = 1 << 2;

// ── Port state struct offsets (port_state_t, 32 bytes) ──
export const PS_PHASE       = 0;
export const PS_SUB_PHASE   = 4;
export const PS_FRAME_COUNT = 8;
export const PS_NOW_US      = 12;
export const PS_BUDGET_US   = 16;
export const PS_ELAPSED_US  = 20;
export const PS_STATUS      = 24;
export const PS_FLAGS       = 28;

// ── Stack flags (port_stack_t.flags) ──
export const SF_ACTIVE   = 1 << 0;
export const SF_YIELDED  = 1 << 1;
export const SF_COMPLETE = 1 << 2;

// ── Event types ──
export const EVT_NONE          = 0x00;
export const EVT_GPIO_CHANGE   = 0x01;
export const EVT_ANALOG_CHANGE = 0x02;
export const EVT_KEY_DOWN      = 0x10;
export const EVT_KEY_UP        = 0x11;
export const EVT_WAKE          = 0x20;

// ── Event ring layout ──
export const EVENT_SIZE      = 8;
export const RING_HEADER_SIZE = 8;

// ── GPIO slot layout (12 bytes per pin) ──
export const GPIO_SLOT_SIZE  = 12;
export const GPIO_ENABLED    = 0;
export const GPIO_DIRECTION  = 1;
export const GPIO_VALUE      = 2;
export const GPIO_PULL       = 3;
export const GPIO_ROLE       = 4;
export const GPIO_FLAGS      = 5;
export const GPIO_CATEGORY   = 6;
export const GPIO_LATCHED    = 7;

// ── GPIO roles ──
export const ROLE_UNCLAIMED  = 0x00;
export const ROLE_DIGITAL_IN = 0x01;
export const ROLE_DIGITAL_OUT = 0x02;
export const ROLE_ADC        = 0x03;
export const ROLE_DAC        = 0x04;
export const ROLE_PWM        = 0x05;
export const ROLE_NEOPIXEL   = 0x06;
export const ROLE_I2C        = 0x07;
export const ROLE_SPI        = 0x08;
export const ROLE_UART       = 0x09;

export const ROLE_NAMES = [
    'UNCLAIMED', 'DIGITAL_IN', 'DIGITAL_OUT', 'ADC', 'DAC',
    'PWM', 'NEOPIXEL', 'I2C', 'SPI', 'UART',
];

// ── GPIO flags ──
export const GF_JS_WROTE = 0x01;
export const GF_C_WROTE  = 0x02;
export const GF_C_READ   = 0x04;
export const GF_LATCHED  = 0x08;

// ── C→JS notification types ──
export const NOTIFY_PIN_CHANGED = 1;
export const NOTIFY_TX_DATA     = 2;
export const NOTIFY_WORK_DONE   = 3;
export const NOTIFY_ERROR       = 4;
