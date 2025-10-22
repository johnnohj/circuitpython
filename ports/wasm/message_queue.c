// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

#include "message_queue.h"
#include <string.h>
#include <emscripten.h>

// Message queue storage
static message_request_t message_queue[MESSAGE_QUEUE_MAX_REQUESTS];
static uint32_t next_request_id = 1;
static message_queue_stats_t stats = {0};

// JavaScript bridge functions
// These will be called to send requests to JavaScript and notify of completions

EM_JS(void, js_send_request, (uint32_t request_id, int type, const void* params, int params_size), {
    // JavaScript receives request and processes it asynchronously
    if (Module.onWASMRequest) {
        const paramsArray = new Uint8Array(Module.HEAPU8.buffer, params, params_size);
        Module.onWASMRequest(request_id, type, paramsArray);
    }
});

// Initialize the message queue
void message_queue_init(void) {
    memset(message_queue, 0, sizeof(message_queue));
    memset(&stats, 0, sizeof(stats));
    next_request_id = 1;

    // Mark all slots as idle
    for (int i = 0; i < MESSAGE_QUEUE_MAX_REQUESTS; i++) {
        message_queue[i].status = MSG_STATUS_IDLE;
    }
}

// Allocate a new request slot
int32_t message_queue_alloc(void) {
    // Find an idle slot
    for (int i = 0; i < MESSAGE_QUEUE_MAX_REQUESTS; i++) {
        if (message_queue[i].status == MSG_STATUS_IDLE) {
            message_queue[i].request_id = next_request_id++;
            message_queue[i].status = MSG_STATUS_PENDING;
            message_queue[i].error_code = 0;
            stats.total_requests++;
            return message_queue[i].request_id;
        }
    }

    // Queue is full
    stats.queue_full_count++;
    return -1;
}

// Get a request by ID
message_request_t* message_queue_get(uint32_t request_id) {
    if (request_id == 0) {
        return NULL;
    }

    for (int i = 0; i < MESSAGE_QUEUE_MAX_REQUESTS; i++) {
        if (message_queue[i].request_id == request_id &&
            message_queue[i].status != MSG_STATUS_IDLE) {
            return &message_queue[i];
        }
    }

    return NULL;
}

// Mark a request as pending
void message_queue_mark_pending(uint32_t request_id) {
    message_request_t* req = message_queue_get(request_id);
    if (req) {
        req->status = MSG_STATUS_PENDING;
        stats.pending_requests++;
    }
}

// Mark a request as complete
void message_queue_mark_complete(uint32_t request_id) {
    message_request_t* req = message_queue_get(request_id);
    if (req) {
        req->status = MSG_STATUS_COMPLETE;
        if (stats.pending_requests > 0) {
            stats.pending_requests--;
        }
        stats.completed_requests++;
    }
}

// Mark a request as error
void message_queue_mark_error(uint32_t request_id, int error_code) {
    message_request_t* req = message_queue_get(request_id);
    if (req) {
        req->status = MSG_STATUS_ERROR;
        req->error_code = error_code;
        if (stats.pending_requests > 0) {
            stats.pending_requests--;
        }
        stats.errors++;
    }
}

// Free a request slot
void message_queue_free(uint32_t request_id) {
    message_request_t* req = message_queue_get(request_id);
    if (req) {
        if (req->status == MSG_STATUS_PENDING && stats.pending_requests > 0) {
            stats.pending_requests--;
        }
        req->status = MSG_STATUS_IDLE;
        req->request_id = 0;
    }
}

// Check if a request is complete
bool message_queue_is_complete(uint32_t request_id) {
    message_request_t* req = message_queue_get(request_id);
    return req && (req->status == MSG_STATUS_COMPLETE || req->status == MSG_STATUS_ERROR);
}

// Check if a request has an error
bool message_queue_has_error(uint32_t request_id) {
    message_request_t* req = message_queue_get(request_id);
    return req && (req->status == MSG_STATUS_ERROR);
}

// Process all pending requests
// This is called from port_background_task() during RUN_BACKGROUND_TASKS
void message_queue_process(void) {
    // This function is called frequently by CircuitPython's background task system
    // JavaScript has already updated the request statuses via WASM memory
    // We just need to check if any requests need notification

    // In the current design, JavaScript directly modifies the message_queue array
    // via exposed WASM memory, so there's nothing to process here.
    // This function is a placeholder for future optimizations.
}

// Get statistics
void message_queue_get_stats(message_queue_stats_t* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(message_queue_stats_t));
    }
}

// Helper function to send a request to JavaScript
void message_queue_send_to_js(uint32_t request_id) {
    message_request_t* req = message_queue_get(request_id);
    if (!req) {
        return;
    }

    // Send request to JavaScript with parameters
    js_send_request(request_id, req->type, &req->params, sizeof(req->params));
}

// Exported C functions that JavaScript can call to complete requests

EMSCRIPTEN_KEEPALIVE
void wasm_complete_request(uint32_t request_id, const void* response_data, int response_size) {
    message_request_t* req = message_queue_get(request_id);
    if (req && response_size <= sizeof(req->response)) {
        memcpy(&req->response, response_data, response_size);
        message_queue_mark_complete(request_id);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_error_request(uint32_t request_id, int error_code) {
    message_queue_mark_error(request_id, error_code);
}

// Direct memory access functions for JavaScript
// JavaScript can directly read/write the message queue via WASM memory

EMSCRIPTEN_KEEPALIVE
message_request_t* wasm_get_request_ptr(uint32_t request_id) {
    return message_queue_get(request_id);
}

EMSCRIPTEN_KEEPALIVE
void* wasm_get_queue_base_ptr(void) {
    return message_queue;
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_queue_size(void) {
    return MESSAGE_QUEUE_MAX_REQUESTS;
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_request_struct_size(void) {
    return sizeof(message_request_t);
}
