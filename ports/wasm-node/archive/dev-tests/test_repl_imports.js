#!/usr/bin/env node

import('./build-standard/circuitpython.mjs').then(async (CircuitPython) => {
  console.log('=== CircuitPython REPL Import Test ===\n');
  
  let output = '';
  const mp = await CircuitPython.default({
    print: (text) => { 
      output += text + '\n';
      console.log('[PRINT]', text); 
    },
    printErr: (text) => { 
      output += '[ERR] ' + text + '\n';
      console.error('[PRINTERR]', text); 
    },
    stdout: (charCode) => { 
      const char = String.fromCharCode(charCode);
      output += char;
      process.stdout.write(char);
    },
    stderr: (charCode) => { 
      const char = String.fromCharCode(charCode);
      output += '[STDERR]' + char;
      process.stderr.write('[STDERR]' + char); 
    }
  });
  
  mp._mp_js_init_with_heap(8 * 1024 * 1024);
  mp._mp_js_repl_init();
  
  // Function to send a complete command
  function sendCommand(cmd) {
    console.log(`\n>>> Sending: "${cmd}"`);
    output = ''; // Clear output buffer
    
    for (let i = 0; i < cmd.length; i++) {
      mp._mp_js_repl_process_char(cmd.charCodeAt(i));
    }
    mp._mp_js_repl_process_char(13); // Enter key
    
    setTimeout(() => {
      console.log(`Output: "${output.trim()}"`);
    }, 50);
  }
  
  // Wait for initial output, then test commands
  setTimeout(() => {
    console.log('\n=== Test 1: Import sys ===');
    sendCommand('import sys');
    
    setTimeout(() => {
      console.log('\n=== Test 2: Check sys in namespace ===');
      sendCommand('dir()');
      
      setTimeout(() => {
        console.log('\n=== Test 3: Use sys ===');
        sendCommand('print(sys.version)');
        
        setTimeout(() => {
          console.log('\n=== Test 4: Import os ===');
          sendCommand('import os');
          
          setTimeout(() => {
            console.log('\n=== Test 5: Use os ===');
            sendCommand('print(os.name if hasattr(os, "name") else "no name attr")');
            
            setTimeout(() => {
              console.log('\n=== Test 6: Multi-statement ===');
              sendCommand('x = 42; print(x)');
              
              setTimeout(() => {
                process.exit(0);
              }, 200);
            }, 200);
          }, 200);
        }, 200);
      }, 200);
    }, 200);
  }, 500);
}).catch(err => {
  console.error('Failed to load CircuitPython:', err);
  process.exit(1);
});