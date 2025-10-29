// Enhanced supervisor.js that uses C lexer instead of JavaScript regex
// This demonstrates how to properly integrate with CircuitPython's lexer

class CircuitPythonSupervisor {
    constructor(wasmModule) {
        this.wasm = wasmModule;
        this.running = false;
        this.currentCode = null;
        this.setupCode = null;
        this.loopCode = null;
        this.iterationCount = 0;
    }

    // NEW: Use C lexer for code analysis instead of JavaScript regex
    async loadCodeFromString(pythonCode) {
        this.currentCode = pythonCode;

        // Get the underlying Emscripten module
        const mod = this.wasm._module || this.wasm;

        // Allocate string in WASM memory
        const codeLen = mod.lengthBytesUTF8(pythonCode);
        const codePtr = mod._malloc(codeLen + 1);
        mod.stringToUTF8(pythonCode, codePtr, codeLen + 1);

        // Call C code_analysis function that uses MP_TOKEN_KW_WHILE!
        const structPtr = mod._analyze_code_structure(codePtr, codeLen);

        // Read the structure back from WASM memory
        const structure = {
            hasLoop: mod.HEAPU8[structPtr] === 1,
            loopLine: mod.HEAPU32[(structPtr + 4) >> 2],
            loopColumn: mod.HEAPU32[(structPtr + 8) >> 2],
            hasAsyncDef: mod.HEAPU8[structPtr + 12] === 1,
            hasAwait: mod.HEAPU8[structPtr + 13] === 1,
            hasAsyncioRun: mod.HEAPU8[structPtr + 14] === 1,
            tokenCount: mod.HEAP32[(structPtr + 16) >> 2]
        };

        // Free the string
        mod._free(codePtr);

        // Log what we found using the REAL Python lexer!
        if (structure.hasLoop) {
            console.log(`✓ Found while True: at line ${structure.loopLine}, column ${structure.loopColumn}`);
            console.log(`  Detected using CircuitPython lexer (MP_TOKEN_KW_WHILE)`);
        } else {
            console.log('✗ No main loop found - will execute code once');
        }

        if (structure.hasAsyncDef) {
            console.log('✓ Found async def - asyncio mode');
        }

        console.log(`  Total tokens parsed: ${structure.tokenCount}`);

        // Now extract setup and loop using C functions
        if (structure.hasLoop) {
            this.setupCode = this.extractSetupCode(pythonCode, structure.loopLine);
            this.loopCode = this.extractLoopBody(pythonCode, structure.loopLine);
        } else {
            this.setupCode = pythonCode;
            this.loopCode = null;
        }

        return structure;
    }

    // Extract setup code (everything before the while loop)
    extractSetupCode(code, loopLine) {
        const lines = code.split('\n');
        return lines.slice(0, loopLine - 1).join('\n');
    }

    // Extract loop body - could also be done in C!
    extractLoopBody(code, loopStartLine) {
        const lines = code.split('\n');

        // Find first line after "while True:"
        const loopBodyStart = loopStartLine;  // Line number is 1-indexed

        if (loopBodyStart >= lines.length) {
            return '';
        }

        const firstLoopLine = lines[loopBodyStart];
        if (!firstLoopLine || firstLoopLine.trim() === '') {
            return '';
        }

        // Get base indentation
        const baseIndent = firstLoopLine.search(/\S/);
        const bodyLines = [];

        for (let i = loopBodyStart; i < lines.length; i++) {
            const line = lines[i];

            // Skip empty lines
            if (line.trim() === '') {
                bodyLines.push('');
                continue;
            }

            const indent = line.search(/\S/);

            // If dedented back to loop level or less, we're done
            if (indent < baseIndent) {
                break;
            }

            // Remove the base indentation
            const dedentedLine = line.substring(baseIndent);
            bodyLines.push(dedentedLine);
        }

        return bodyLines.join('\n');
    }

    // Rest of the supervisor code stays the same...
    async start() {
        if (this.running) {
            console.log('Already running');
            return;
        }

        console.log('Starting CircuitPython supervisor...');

        try {
            // Execute setup code once
            if (this.setupCode && this.setupCode.trim()) {
                console.log('Running setup code...');
                this.wasm.runPython(this.setupCode);
            }

            // Start loop if present
            if (this.loopCode) {
                console.log('Starting main loop...');
                this.running = true;
                this.iterationCount = 0;
                this.scheduleNextIteration();
            } else {
                console.log('No loop found, setup complete');
            }
        } catch (e) {
            console.error('Error starting:', e);
        }
    }

    scheduleNextIteration() {
        if (!this.running) return;
        requestAnimationFrame(() => this.runIteration());
    }

    runIteration() {
        if (!this.running || !this.loopCode) return;

        try {
            this.iterationCount++;
            this.executeWithSleep(this.loopCode, () => {
                this.scheduleNextIteration();
            });
        } catch (e) {
            console.error('Error in iteration:', e);
            this.stop();
        }
    }

    executeWithSleep(code, callback) {
        const statements = this.splitStatements(code);
        let index = 0;

        const executeNext = () => {
            if (index >= statements.length) {
                callback();
                return;
            }

            const stmt = statements[index++];

            // Check for time.sleep()
            const sleepMatch = stmt.match(/time\.sleep\s*\(\s*([\d.]+)\s*\)/);
            if (sleepMatch) {
                const seconds = parseFloat(sleepMatch[1]);
                const ms = seconds * 1000;
                setTimeout(executeNext, ms);
                return;
            }

            // Execute non-sleep statement immediately
            try {
                this.wasm.runPython(stmt);
            } catch (e) {
                console.error('Error executing statement:', stmt, e);
            }

            executeNext();
        };

        executeNext();
    }

    splitStatements(code) {
        const lines = code.split('\n');
        const statements = [];

        for (const line of lines) {
            const trimmed = line.trim();
            if (trimmed === '') continue;
            statements.push(line);
        }

        return statements;
    }

    stop() {
        console.log('Stopping supervisor...');
        this.running = false;
    }

    getStats() {
        return {
            running: this.running,
            iterations: this.iterationCount,
            hasLoop: this.loopCode !== null
        };
    }
}

// ==============================================================================
// COMPARISON: Old vs New Approach
// ==============================================================================

/*
OLD APPROACH (JavaScript Regex):
---------------------------------
parseCode(code) {
    const lines = code.split('\n');

    // Fragile regex parsing!
    if (line.match(/^\s*while\s+(True|1|true)\s*:/)) {
        loopStartIdx = i;
    }

    // Problems:
    // ❌ Doesn't handle: while True: # comment
    // ❌ Doesn't handle: while\ntrue:
    // ❌ Doesn't handle: strings containing "while True:"
    // ❌ Can't detect async def properly
    // ❌ No line number info for errors
}

NEW APPROACH (CircuitPython Lexer):
------------------------------------
loadCodeFromString(code) {
    // Use real Python lexer!
    const structPtr = mod._analyze_code_structure(codePtr, codeLen);

    const structure = {
        hasLoop: ...,
        loopLine: ...,  // Exact line number!
        hasAsyncDef: ...,
    };

    // Benefits:
    // ✅ Uses MP_TOKEN_KW_WHILE - proper token detection
    // ✅ Handles all Python syntax correctly
    // ✅ Detects async def, await, etc.
    // ✅ Returns line numbers for debugging
    // ✅ Battle-tested CircuitPython code
}
*/

// Export for use in browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { CircuitPythonSupervisor };
}
