################################################################################
# WASM-dist port Makefile
#
# JS/Emscripten is the host OS; Python is the guest.
# No Asyncify, no JSPI — synchronous execution inside each WebWorker.
# Virtual hardware bus via Emscripten MEMFS (/dev/, /mem/, /flash/, /state/).
################################################################################

# Select the variant to build
ifdef VARIANT_DIR
VARIANT ?= $(notdir $(VARIANT_DIR:/=))
else
VARIANT ?= dist
VARIANT_DIR ?= variants/$(VARIANT)
endif

ifeq ($(wildcard $(VARIANT_DIR)/.),)
$(error Invalid VARIANT specified: $(VARIANT_DIR))
endif

BUILD ?= build-$(VARIANT)

include ../../py/mkenv.mk
-include mpconfigport.mk
include $(VARIANT_DIR)/mpconfigvariant.mk

# Use the variant-specified frozen manifest.
FROZEN_MANIFEST ?= $(VARIANT_DIR)/manifest.py

# Qstr definitions (must come before including py.mk).
QSTR_DEFS += qstrdefsport.h

# Include py core make definitions.
include $(TOP)/py/py.mk

################################################################################
# Compiler / linker

CC  = emcc
LD  = emcc
NODE ?= node

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(VARIANT_DIR)

CFLAGS += -std=c99 -Wall -Wdouble-promotion -Wfloat-conversion
CFLAGS += -Os -DNDEBUG
CFLAGS += $(INC)
CFLAGS += -DCIRCUITPY=1

################################################################################
# Emscripten / JS flags

# Target: browser workers and web (no Node NODEFS, no IDBFS)
JSFLAGS += -s ENVIRONMENT='worker,web,node'
JSFLAGS += -s MODULARIZE=1 -s EXPORT_NAME=createCircuitPythonModule
JSFLAGS += -s EXPORT_ES6=1
JSFLAGS += -s FILESYSTEM=1 -s FORCE_FILESYSTEM=1
# IDBFS for browser /circuitpy/ persistence (IndexedDB backed)
JSFLAGS += -lidbfs.js
# Support longjmp via native WASM exceptions (setjmp/longjmp for NLR).
# 'emscripten' mode corrupts mp_execute_bytecode locals during nested calls
# (imports inside stackless __init__). 'wasm' mode uses native EH and is correct.
# MUST be set at both compile time AND link time.
SUPPORT_LONGJMP ?= wasm
CFLAGS += -s SUPPORT_LONGJMP=$(SUPPORT_LONGJMP)
JSFLAGS += -s SUPPORT_LONGJMP=$(SUPPORT_LONGJMP)

JSFLAGS += -s EXPORTED_FUNCTIONS="[\
	_free,\
	_malloc,\
	_mp_js_init,\
	_mp_js_run,\
	_mp_js_do_exec,\
	_mp_js_do_import,\
	_mp_js_register_js_module,\
	_proxy_c_init,\
	_proxy_c_free_obj,\
	_proxy_c_to_js_call,\
	_proxy_c_to_js_lookup_attr,\
	_proxy_c_to_js_store_attr,\
	_proxy_c_to_js_dir,\
	_proxy_c_to_js_iternext,\
	_proxy_c_to_js_get_type,\
	_proxy_c_to_js_has_attr,\
	_proxy_c_to_js_delete_attr,\
	_proxy_c_to_js_resume,\
	_proxy_convert_mp_to_js_obj_cside,\
	_mp_sched_vm_abort,\
	_mp_sched_keyboard_interrupt,\
	_mp_hal_get_interrupt_char,\
	_vdev_bc_in_write,\
	_vdev_bc_out_drain,\
	_mp_js_compile_to_mpy,\
	_mp_js_enable_trace,\
	_mp_js_checkpoint_vm,\
	_mp_js_restore_vm,\
	_mp_repl_continue_with_input,\
	_hw_reg_read,\
	_hw_reg_write,\
	_hw_reg_write_batch,\
	_hw_reg_sync_from_bc_in,\
	_mp_vm_start,\
	_mp_vm_step,\
	_mp_bc_out_flush,\
	_mp_bc_out_enqueue,\
	_mp_bc_in_write,\
	_mp_bc_in_read,\
	_mp_bc_in_pending,\
	_mp_tasks_init,\
	_mp_tasks_poll,\
	_mp_tasks_request_yield,\
	_mp_gc_heap_addr,\
	_mp_gc_heap_size,\
	_mp_gc_pool_used,\
	_mp_gc_pool_total,\
	_mp_gc_pool_free,\
	_mp_state_ctx_addr,\
	_mp_state_ctx_size,\
	_mp_pystack_get_addr,\
	_mp_pystack_get_size,\
	_mp_pystack_get_used,\
	_mp_async_request_delay,\
	_mp_async_consume_delay,\
	_mp_async_has_delay,\
	_mp_js_set_deadline]"

JSFLAGS += -s EXPORTED_RUNTIME_METHODS="[\
	FS,\
	IDBFS,\
	ccall,\
	cwrap,\
	getValue,\
	setValue,\
	HEAPU8,\
	UTF8ToString,\
	stringToUTF8,\
	lengthBytesUTF8,\
	stringToNewUTF8]"

# Host OS JS libraries (virtual devices, hook, stdout capture, step-wise VM, IPC, tasks)
JSFLAGS += --js-library library.js
JSFLAGS += --js-library libpyvm.js
JSFLAGS += --js-library libpybroadcast.js
JSFLAGS += --js-library libpytasks.js
JSFLAGS += --js-library libpyasync.js

################################################################################
# Source files

SRC_SHARED = $(addprefix shared/,\
	runtime/buffer_helper.c \
	runtime/context_manager_helpers.c \
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

# CircuitPython translate system — provides decompress/decompress_length
# needed by py/mpprint.c
SRC_SHARED += supervisor/shared/translate/translate.c

SRC_C += \
	main.c \
	vdev.c \
	memfs_state.c \
	semihosting_wasm.c \
	mphalport.c \
	mpthread_wasm.c \
	lexer_dedent.c \
	modjs.c \
	modjsffi.c \
	objjsproxy.c \
	proxy_c.c \
	modblinka.c \

SRC_QSTR += $(SRC_C) $(SRC_SHARED)

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

################################################################################
# JavaScript source files appended after the WASM module

SRC_JS = \
	proxy_js.js \
	api.js \

################################################################################
# Build targets

.PHONY: all clean

all: $(BUILD)/circuitpython.mjs

$(BUILD)/circuitpython.mjs: $(OBJ) library.js libpyasync.js $(SRC_JS)
	$(ECHO) "LINK $@"
	$(Q)emcc $(LDFLAGS) -o $@ $(OBJ) $(JSFLAGS)
	$(Q)cat $(SRC_JS) >> $@

clean:
	$(RM) -rf $(BUILD)

################################################################################
include $(TOP)/py/mkrules.mk
