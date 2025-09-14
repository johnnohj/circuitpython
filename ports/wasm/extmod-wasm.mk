# WebAssembly-specific extmod additions
# This file adds back modules that CircuitPython's extmod.mk removes but 
# are needed by core py/ code for WebAssembly builds

# NOTE: CircuitPython removed uctypes module entirely, so we include from MicroPython
# Copy this from MicroPython's extmod/moductypes.c if full uctypes support is needed
# For now, use stubs

# Add ring I/O support (referenced by py/modmicropython.c)  
# NOTE: objringio.c has missing dependencies, so we provide a stub instead
# SRC_C += py/objringio.c

# VFS-related lexer support (referenced by py/builtinevex.c, py/builtinimport.c)
ifeq ($(MICROPY_VFS),1)
SRC_SHARED += shared/memzip/lexermemzip.c
endif

# Add stubs for missing symbols and REPL functions
SRC_C += webasm_stubs.c