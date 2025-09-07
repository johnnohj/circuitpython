#ifndef MICROPY_INCLUDED_WEBASM_INPUT_H
#define MICROPY_INCLUDED_WEBASM_INPUT_H

// WebAssembly input stub - minimal implementation for mphalport.h compatibility

// Stub for input prompt function (no interactive input in WebAssembly)
char *prompt(const char *p);

#endif // MICROPY_INCLUDED_WEBASM_INPUT_H