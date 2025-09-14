// Simple test to check if dynamic_import function is available
import createCircuitPythonModule from './build/circuitpython.mjs';
import { setupModuleResolver } from './module_resolver.js';

createCircuitPythonModule().then(CP => {
  console.log("CircuitPython module loaded");
  
  // Setup module resolver
  setupModuleResolver(CP);
  
  // Initialize CircuitPython
  CP._mp_js_init_with_heap(512 * 1024);
  CP._mp_js_repl_init();
  
  // Test 1: Check if dynamic_import is available
  console.log("Checking if dynamic_import is available...");
  
  // Send simple command to check
  const checkCode = "print(dir())\n";
  for (let char of checkCode) {
    CP._mp_js_repl_process_char(char.charCodeAt(0));
  }
  
  setTimeout(() => {
    console.log("Testing dynamic_import access...");
    const testCode = "dynamic_import\n";
    for (let char of testCode) {
      CP._mp_js_repl_process_char(char.charCodeAt(0));
    }
  }, 1000);
  
}).catch(error => {
  console.error("Failed to load CircuitPython module:", error);
});