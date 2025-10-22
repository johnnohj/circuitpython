// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Message queue system for async JavaScript operations with CircuitPython yielding
// Allows synchronous CircuitPython APIs to yield while waiting for async JS operations

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Maximum number of concurrent pending requests
#define MESSAGE_QUEUE_MAX_REQUESTS 32

// Maximum data payload size for a request/response
#define MESSAGE_QUEUE_MAX_PAYLOAD 256

// Request types - maps to JavaScript operation types
typedef enum {
    MSG_TYPE_NONE = 0,

    // GPIO operations
    MSG_TYPE_GPIO_SET = 1,
    MSG_TYPE_GPIO_GET = 2,
    MSG_TYPE_GPIO_SET_DIRECTION = 3,
    MSG_TYPE_GPIO_SET_PULL = 4,

    // Analog operations
    MSG_TYPE_ANALOG_INIT = 10,
    MSG_TYPE_ANALOG_DEINIT = 11,
    MSG_TYPE_ANALOG_READ = 12,
    MSG_TYPE_ANALOG_WRITE = 13,

    // I2C operations
    MSG_TYPE_I2C_INIT = 20,
    MSG_TYPE_I2C_DEINIT = 21,
    MSG_TYPE_I2C_WRITE = 22,
    MSG_TYPE_I2C_READ = 23,
    MSG_TYPE_I2C_WRITE_READ = 24,
    MSG_TYPE_I2C_PROBE = 25,

    // SPI operations
    MSG_TYPE_SPI_INIT = 30,
    MSG_TYPE_SPI_DEINIT = 31,
    MSG_TYPE_SPI_TRANSFER = 32,
    MSG_TYPE_SPI_WRITE = 33,
    MSG_TYPE_SPI_READ = 34,
    MSG_TYPE_SPI_CONFIGURE = 35,

    // Time operations
    MSG_TYPE_TIME_SLEEP = 40,
    MSG_TYPE_TIME_GET_MONOTONIC = 41,

    // Console operations
    MSG_TYPE_CONSOLE_WRITE = 50,
    MSG_TYPE_CONSOLE_READ = 51,

    // UART operations
    MSG_TYPE_UART_INIT = 52,
    MSG_TYPE_UART_DEINIT = 53,
    MSG_TYPE_UART_READ = 54,
    MSG_TYPE_UART_WRITE = 55,
    MSG_TYPE_UART_SET_BAUDRATE = 56,
    MSG_TYPE_UART_RX_AVAILABLE = 57,
    MSG_TYPE_UART_CLEAR_RX = 58,

    // System operations
    MSG_TYPE_MCU_RESET = 60,
} message_type_t;

// Request status
typedef enum {
    MSG_STATUS_IDLE = 0,        // Slot available
    MSG_STATUS_PENDING = 1,     // Sent to JS, waiting for response
    MSG_STATUS_COMPLETE = 2,    // Response received from JS
    MSG_STATUS_ERROR = 3,       // Error occurred
} message_status_t;

// Request structure
typedef struct {
    message_type_t type;
    message_status_t status;
    uint32_t request_id;

    // Request parameters
    union {
        struct {
            uint8_t pin;
            uint8_t value;
        } gpio_set;

        struct {
            uint8_t pin;
        } gpio_get;

        struct {
            uint8_t pin;
            uint8_t direction;  // 0=input, 1=output
        } gpio_direction;

        struct {
            uint8_t pin;
            uint8_t pull;  // 0=none, 1=up, 2=down
        } gpio_pull;

        struct {
            uint8_t pin;
            bool is_output;
        } analog_init;

        struct {
            uint8_t pin;
        } analog_deinit;

        struct {
            uint8_t pin;
        } analog_read;

        struct {
            uint8_t pin;
            uint16_t value;
        } analog_write;

        struct {
            uint8_t scl_pin;
            uint8_t sda_pin;
            uint32_t frequency;
        } i2c_init;

        struct {
            uint8_t scl_pin;
        } i2c_deinit;

        struct {
            uint8_t address;
            uint16_t length;
            uint8_t data[MESSAGE_QUEUE_MAX_PAYLOAD];
        } i2c_write;

        struct {
            uint8_t address;
            uint16_t length;
        } i2c_read;

        struct {
            uint8_t address;
            uint16_t write_length;
            uint16_t read_length;
            uint8_t write_data[MESSAGE_QUEUE_MAX_PAYLOAD];
        } i2c_write_read;

        struct {
            uint8_t address;
        } i2c_probe;

        struct {
            uint8_t clock_pin;
            uint8_t mosi_pin;
            uint8_t miso_pin;
        } spi_init;

        struct {
            uint8_t clock_pin;
        } spi_deinit;

        struct {
            uint32_t baudrate;
            uint8_t polarity;
            uint8_t phase;
            uint8_t bits;
        } spi_configure;

        struct {
            uint16_t length;
            uint8_t data[MESSAGE_QUEUE_MAX_PAYLOAD];
        } spi_write;

        struct {
            uint16_t length;
            uint8_t write_value;
        } spi_read;

        struct {
            uint16_t length;
            uint8_t data_out[MESSAGE_QUEUE_MAX_PAYLOAD];
        } spi_transfer;

        struct {
            uint32_t milliseconds;
        } time_sleep;

        struct {
            uint8_t tx_pin;
            uint8_t rx_pin;
            uint32_t baudrate;
            uint8_t bits;
            uint8_t parity;
            uint8_t stop;
        } uart_init;

        struct {
            uint8_t tx_pin;
        } uart_deinit;

        struct {
            uint16_t length;
        } uart_read;

        struct {
            uint16_t length;
            uint8_t data[MESSAGE_QUEUE_MAX_PAYLOAD];
        } uart_write;

        struct {
            uint32_t baudrate;
        } uart_set_baudrate;
    } params;

    // Response data
    union {
        struct {
            uint8_t value;
        } gpio_value;

        struct {
            uint16_t value;  // 0-65535
        } analog_value;

        struct {
            bool success;
        } i2c_result;

        struct {
            uint16_t length;
            uint8_t data[MESSAGE_QUEUE_MAX_PAYLOAD];
        } i2c_data;

        // Generic data response for SPI/I2C reads
        uint8_t data[MESSAGE_QUEUE_MAX_PAYLOAD];

        struct {
            uint64_t milliseconds;
        } time_value;

        struct {
            uint16_t length;
            uint8_t data[MESSAGE_QUEUE_MAX_PAYLOAD];
        } uart_data;

        struct {
            uint32_t count;
        } uart_available;
    } response;

    // Error information
    int error_code;
} message_request_t;

// Message queue API

// Initialize the message queue system
void message_queue_init(void);

// Allocate a new request slot (returns request_id or -1 if queue full)
int32_t message_queue_alloc(void);

// Get a request by ID (returns NULL if invalid)
message_request_t* message_queue_get(uint32_t request_id);

// Mark a request as pending (sent to JavaScript)
void message_queue_mark_pending(uint32_t request_id);

// Mark a request as complete (response received from JavaScript)
void message_queue_mark_complete(uint32_t request_id);

// Mark a request as error
void message_queue_mark_error(uint32_t request_id, int error_code);

// Free a request slot
void message_queue_free(uint32_t request_id);

// Check if a request is complete
bool message_queue_is_complete(uint32_t request_id);

// Check if a request has an error
bool message_queue_has_error(uint32_t request_id);

// Process all pending requests (called from background task)
void message_queue_process(void);

// Send a request to JavaScript
void message_queue_send_to_js(uint32_t request_id);

// Get statistics for debugging
typedef struct {
    uint32_t total_requests;
    uint32_t pending_requests;
    uint32_t completed_requests;
    uint32_t errors;
    uint32_t queue_full_count;
} message_queue_stats_t;

void message_queue_get_stats(message_queue_stats_t* stats);

// Helper macros for yielding while waiting for completion
#define WAIT_FOR_REQUEST_COMPLETION(request_id) \
    do { \
        while (!message_queue_is_complete(request_id)) { \
            RUN_BACKGROUND_TASKS; \
            mp_handle_pending(false); \
        } \
    } while (0)

#define WAIT_FOR_REQUEST_WITH_TIMEOUT(request_id, timeout_ms) \
    ({ \
        uint64_t start = common_hal_time_monotonic_ms(); \
        bool timed_out = false; \
        while (!message_queue_is_complete(request_id)) { \
            if ((common_hal_time_monotonic_ms() - start) > (timeout_ms)) { \
                timed_out = true; \
                break; \
            } \
            RUN_BACKGROUND_TASKS; \
            mp_handle_pending(false); \
        } \
        timed_out; \
    })
