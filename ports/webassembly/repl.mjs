#!/usr/bin/env node

// Interactive REPL for CircuitPython WebAssembly port
import loadCircuitPython from './build-standard/circuitpython.mjs';
import readline from 'readline';
import { stdin as input, stdout as output } from 'process';

async function startREPL() {
    console.log('Loading CircuitPython WebAssembly port...');
    
    // Create the CircuitPython module
    const mp = await loadCircuitPython({
        print: (text) => console.log(text),
        printErr: (text) => console.error(text),
        stdin: () => null  // We'll handle stdin separately
    });
    
    // Initialize Python with 256KB heap
    mp._mp_js_init(8 * 1024, 256 * 1024);
    
    // Print banner
    const bannerPtr = mp._malloc(4);
    mp._mp_js_do_exec("import sys; print(f'CircuitPython {sys.version} on WebAssembly port')", bannerPtr);
    mp._free(bannerPtr);
    
    console.log('Type "help()" for more information, Ctrl+C to exit\n');
    
    // Create readline interface
    const rl = readline.createInterface({ input, output });
    
    // REPL state
    let multilineBuffer = '';
    let inMultiline = false;
    
    function prompt() {
        rl.setPrompt(inMultiline ? '... ' : '>>> ');
        rl.prompt();
    }
    
    // Handle line input
    rl.on('line', (line) => {
        if (inMultiline) {
            if (line === '') {
                // Empty line ends multiline input
                inMultiline = false;
                executeCode(multilineBuffer);
                multilineBuffer = '';
            } else {
                multilineBuffer += line + '\n';
            }
        } else {
            if (line.endsWith(':')) {
                // Start multiline input
                inMultiline = true;
                multilineBuffer = line + '\n';
            } else if (line.trim() === '') {
                // Empty line, just show prompt again
            } else {
                // Single line execution
                executeCode(line);
            }
        }
        prompt();
    });
    
    // Execute Python code
    function executeCode(code) {
        try {
            const outputPtr = mp._malloc(4);
            
            // Try to evaluate as expression first (for REPL echo)
            try {
                const evalCode = `
try:
    _result = ${code}
    if _result is not None:
        print(repr(_result))
except SyntaxError:
    exec('''${code}''')
`;
                mp._mp_js_do_exec(evalCode, outputPtr);
            } catch (e) {
                // Fall back to exec
                mp._mp_js_do_exec(code, outputPtr);
            }
            
            mp._free(outputPtr);
        } catch (error) {
            console.error('Error:', error.message);
        }
    }
    
    // Handle Ctrl+C
    rl.on('SIGINT', () => {
        if (inMultiline) {
            console.log('\nKeyboardInterrupt');
            inMultiline = false;
            multilineBuffer = '';
            prompt();
        } else {
            console.log('\nExiting...');
            process.exit(0);
        }
    });
    
    // Start the REPL
    prompt();
}

startREPL().catch(error => {
    console.error('Failed to start REPL:', error);
    process.exit(1);
});