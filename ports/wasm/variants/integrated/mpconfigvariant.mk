# Integrated variant - Full CircuitPython supervisor compatibility
FROZEN_MANIFEST ?= $(VARIANT_DIR)/manifest.py

# Use the WASM-adapted supervisor implementation
USE_SUPERVISOR_WASM = 1

# Enable code analysis and cooperative yielding support
ENABLE_CODE_ANALYSIS = 1

# Add code analysis source file
SRC_C += supervisor/code_analysis.c

# Add cooperative supervisor JavaScript
SRC_JS += src/core/cooperative_supervisor.js

# Export code analysis functions for JavaScript
EXPORTED_FUNCTIONS_EXTRA += ,\
	_analyze_code_structure,\
	_is_valid_python_syntax,\
	_extract_loop_body
