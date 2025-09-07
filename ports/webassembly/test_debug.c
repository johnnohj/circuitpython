/*
 * Debug test file to verify output chain
 */
#include <stdio.h>
#include "py/mphal.h"
#include "py/mpprint.h"

// Test function to verify the output chain
void test_output_chain(void) {
    printf("DEBUG: Test 1 - Direct printf\n");
    
    mp_hal_stdout_tx_str("DEBUG: Test 2 - mp_hal_stdout_tx_str\n");
    
    mp_hal_stdout_tx_strn_cooked("DEBUG: Test 3 - mp_hal_stdout_tx_strn_cooked\n", 42);
    
    mp_printf(&mp_plat_print, "DEBUG: Test 4 - mp_plat_print\n");
    
    // Test the REPL print function directly
    extern const mp_print_t mp_plat_print;
    mp_print_str(&mp_plat_print, "DEBUG: Test 5 - mp_print_str with mp_plat_print\n");
}