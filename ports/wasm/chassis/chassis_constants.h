/*
 * chassis/chassis_constants.h — Shared constants for C/JS FFI.
 *
 * This is the CANONICAL source.  The JS mirror (chassis-constants.mjs)
 * must match exactly.  When adding constants, update both files.
 *
 * Convention: all constants are plain #defines (no enums) so the
 * values are trivially portable to JS.
 */

#ifndef CHASSIS_CONSTANTS_H
#define CHASSIS_CONSTANTS_H

/* ── Frame return codes ── */
#define RC_DONE         0   /* frame complete, no more work */
#define RC_YIELD        1   /* budget exhausted, more work pending */
#define RC_EVENTS       2   /* processed events, may need repaint */
#define RC_ERROR        3   /* something went wrong */

/* ── Port flags (port_state_t.flags) ── */
#define PF_INITIALIZED  (1 << 0)
#define PF_HAS_EVENTS   (1 << 1)
#define PF_HAL_DIRTY    (1 << 2)

/* ── Port state struct offsets (port_state_t, 32 bytes) ── */
#define PS_PHASE         0
#define PS_SUB_PHASE     4
#define PS_FRAME_COUNT   8
#define PS_NOW_US       12
#define PS_BUDGET_US    16
#define PS_ELAPSED_US   20
#define PS_STATUS       24
#define PS_FLAGS        28

/* ── Stack flags (port_stack_t.flags) ── */
#define SF_ACTIVE       (1 << 0)
#define SF_YIELDED      (1 << 1)
#define SF_COMPLETE     (1 << 2)

/* ── Event types ── */
#define EVT_NONE            0x00
#define EVT_GPIO_CHANGE     0x01
#define EVT_ANALOG_CHANGE   0x02
#define EVT_KEY_DOWN        0x10
#define EVT_KEY_UP          0x11
#define EVT_WAKE            0x20

/* ── Event ring layout ── */
#define EVENT_SIZE          8   /* sizeof(port_event_t) */
#define RING_HEADER_SIZE    8   /* sizeof(event_ring_header_t) */

/* ── GPIO slot layout (12 bytes per pin) ── */
#define GPIO_SLOT_SIZE      12

#define GPIO_ENABLED         0
#define GPIO_DIRECTION       1
#define GPIO_VALUE           2
#define GPIO_PULL            3
#define GPIO_ROLE            4
#define GPIO_FLAGS           5
#define GPIO_CATEGORY        6
#define GPIO_LATCHED         7

/* ── GPIO roles ── */
#define ROLE_UNCLAIMED      0x00
#define ROLE_DIGITAL_IN     0x01
#define ROLE_DIGITAL_OUT    0x02
#define ROLE_ADC            0x03
#define ROLE_DAC            0x04
#define ROLE_PWM            0x05
#define ROLE_NEOPIXEL       0x06
#define ROLE_I2C            0x07
#define ROLE_SPI            0x08
#define ROLE_UART           0x09

/* ── GPIO flags ── */
#define GF_JS_WROTE         0x01
#define GF_C_WROTE          0x02
#define GF_C_READ           0x04
#define GF_LATCHED          0x08

/* ── C→JS notification types (ffi.notify) ── */
#define NOTIFY_PIN_CHANGED   1   /* C wrote a pin value */
#define NOTIFY_TX_DATA       2   /* serial TX has data */
#define NOTIFY_WORK_DONE     3   /* work completed */
#define NOTIFY_ERROR         4   /* error occurred */

#endif /* CHASSIS_CONSTANTS_H */
