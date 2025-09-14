import createCircuitPythonModule from './build/circuitpython.mjs';

createCircuitPythonModule().then(CP => {
  console.log("CircuitPython module loaded");
  
  // Initialize CircuitPython
  CP._mp_js_init_with_heap(512 * 1024);
  CP._mp_js_repl_init();
  
  // Send Ctrl+B to show the banner (reset repl and show version info)
  const output = [];
  
  // Capture output by overriding console.log temporarily
  const originalLog = console.log;
  console.log = function(...args) {
    output.push(args.join(' '));
    originalLog.apply(console, args);
  };
  
  // Send Ctrl+B (repl reset) to trigger banner
  CP._mp_js_repl_process_char(2); // Ctrl+B
  
  // Restore console.log
  console.log = originalLog;
  
  console.log("Banner test completed");
});