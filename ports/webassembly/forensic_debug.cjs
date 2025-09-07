#!/usr/bin/env node

/**
 * Forensic debug script to trace REPL execution failures
 * This creates an environment to specifically test Python execution
 */

const fs = require('fs');

// Track all output to identify where execution flow breaks
let outputLog = [];
let executionTrace = [];

console.log("=== CircuitPython WebAssembly REPL Forensic Analysis ===\n");

// Load the WebAssembly module
const Module = {
    // Override stdout to capture everything
    stdout: function(charCode) {
        const char = String.fromCharCode(charCode);
        outputLog.push({type: 'stdout', char: char, timestamp: Date.now()});
        process.stdout.write(char);
    },
    
    // Override print for fallback
    print: function(text) {
        outputLog.push({type: 'print', text: text, timestamp: Date.now()});
        console.log('[FALLBACK PRINT]', text);
    },
    
    // Override stderr 
    printErr: function(text) {
        outputLog.push({type: 'stderr', text: text, timestamp: Date.now()});
        console.error('[STDERR]', text);
    },
    
    // Monitor function calls
    postRun: []
};

// Add execution tracing
const originalCcall = Module.ccall;
Module.ccall = function(funcname, returnType, argTypes, args) {
    executionTrace.push({
        function: funcname,
        args: args,
        timestamp: Date.now(),
        type: 'ccall'
    });
    
    if (funcname === 'mp_js_repl_process_char') {
        console.log(`\n[TRACE] Processing char: ${args[0]} (${String.fromCharCode(args[0])})`);
    }
    
    const result = originalCcall.call(this, funcname, returnType, argTypes, args);
    
    if (funcname === 'mp_js_repl_process_char') {
        console.log(`[TRACE] Process char result: ${result}`);
    }
    
    return result;
};

// Load CircuitPython - check which file exists
let Module;
try {
    Module = require('./build-standard/circuitpython.js');
} catch (e) {
    try {
        Module = require('./build-standard/circuitpython.mjs');
    } catch (e2) {
        console.error("Cannot find CircuitPython module (tried .js and .mjs)");
        console.error("Build directory contains:", require('fs').readdirSync('./build-standard/'));
        process.exit(1);
    }
}

console.log("\n=== Initializing CircuitPython ===");
const heapSize = 1024 * 1024; // 1MB
Module.ccall('mp_js_init_with_heap', null, ['number'], [heapSize]);

console.log("\n=== Initializing REPL ===");
Module.ccall('mp_js_repl_init', null, [], []);

console.log("\n=== Testing Basic Output Infrastructure ===");
// Test that our output callbacks work
console.log("Testing direct stdout write...");
Module.ccall('mp_hal_stdout_tx_strn', null, ['string', 'number'], ['[DIRECT TEST]\n', 14]);

console.log("\n=== Starting Python Expression Tests ===");

// Test cases with detailed tracing
const testCases = [
    { name: "Simple expression", input: "1+1\r" },
    { name: "Print statement", input: "print('hello')\r" },
    { name: "Variable assignment", input: "x = 42\r" },
    { name: "Variable access", input: "x\r" }
];

function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function runTest(testCase) {
    console.log(`\n--- Testing: ${testCase.name} ---`);
    console.log(`Input: ${JSON.stringify(testCase.input)}`);
    
    // Clear logs for this test
    const startLogIndex = outputLog.length;
    const startTraceIndex = executionTrace.length;
    
    // Send each character
    for (let i = 0; i < testCase.input.length; i++) {
        const char = testCase.input.charCodeAt(i);
        console.log(`Sending char ${i}: ${char} ('${testCase.input[i]}')`);
        
        const result = Module.ccall('mp_js_repl_process_char', 'number', ['number'], [char]);
        console.log(`  Result: ${result}`);
        
        await delay(10); // Small delay to let output flush
    }
    
    // Wait for any async output
    await delay(100);
    
    // Analyze output for this test
    const testOutput = outputLog.slice(startLogIndex);
    const testTrace = executionTrace.slice(startTraceIndex);
    
    console.log(`\nOutput captured (${testOutput.length} items):`);
    testOutput.forEach((item, i) => {
        console.log(`  ${i}: ${item.type} = ${JSON.stringify(item.char || item.text)}`);
    });
    
    console.log(`\nExecution trace (${testTrace.length} items):`);
    testTrace.forEach((item, i) => {
        console.log(`  ${i}: ${item.function} (${JSON.stringify(item.args)})`);
    });
    
    // Check if we got Python execution output vs just echoing
    const hasExecutionOutput = testOutput.some(item => 
        item.type === 'stdout' && 
        item.char !== undefined && 
        !testCase.input.includes(item.char) && // Not just echo
        item.char.match(/[0-9]/) // Looks like execution result
    );
    
    console.log(`Execution output detected: ${hasExecutionOutput}`);
}

async function main() {
    try {
        for (const testCase of testCases) {
            await runTest(testCase);
        }
        
        console.log("\n=== Final Analysis ===");
        console.log(`Total output items: ${outputLog.length}`);
        console.log(`Total function calls: ${executionTrace.length}`);
        
        // Look for patterns in the execution failure
        const replProcessCalls = executionTrace.filter(t => t.function === 'mp_js_repl_process_char');
        console.log(`REPL process char calls: ${replProcessCalls.length}`);
        
        // Check if any Python execution was attempted
        const executionResults = outputLog.filter(item => 
            item.type === 'stdout' && 
            item.char && 
            item.char.match(/[0-9]/) 
        );
        
        if (executionResults.length === 0) {
            console.log("\n❌ FAILURE CONFIRMED: No Python execution output detected");
            console.log("The REPL is receiving input and echoing, but not executing Python code");
        } else {
            console.log("\n✅ Python execution detected");
        }
        
    } catch (error) {
        console.error("Error during forensic analysis:", error);
        console.error("Stack:", error.stack);
    }
}

main();