// Test dynamic module importing functionality
import createCircuitPythonModule from './build/circuitpython.mjs';
import { setupModuleResolver } from './module_resolver.js';

createCircuitPythonModule().then(CP => {
  console.log("CircuitPython module loaded");
  
  // Setup module resolver
  setupModuleResolver(CP);
  
  // Initialize CircuitPython
  CP._mp_js_init_with_heap(512 * 1024);
  CP._mp_js_repl_init();
  
  console.log("Testing dynamic module import...");
  
  // Test 1: Try to call dynamic_import function from Python
  try {
    // Send Python code to test dynamic import
    const pythonCode = `
try:
    result = dynamic_import('hello')
    print("Dynamic import succeeded!")
    print(result)
except Exception as e:
    print("Dynamic import failed:", e)
`;
    
    // Process each character of the Python code
    for (let char of pythonCode) {
      CP._mp_js_repl_process_char(char.charCodeAt(0));
    }
    
    // Send Enter to execute
    CP._mp_js_repl_process_char(13); // Enter
    
  } catch (error) {
    console.error("Error testing dynamic import:", error);
  }
  
  console.log("Dynamic import test completed");
}).catch(error => {
  console.error("Failed to load CircuitPython module:", error);
});