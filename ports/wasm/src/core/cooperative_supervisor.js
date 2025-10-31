// Cooperative Yielding Supervisor for CircuitPython WASM
// Instruments Python code with infinite loops to yield back to JavaScript

export class CooperativeSupervisor {
    constructor(circuitPython) {
        this.cp = circuitPython;
        this.yieldCounter = 0;
        this.maxIterationsPerYield = 100; // Yield after this many loop iterations
    }

    /**
     * Analyze and potentially instrument code with yield points
     * @param {string} code - Python code to analyze
     * @returns {object} - { instrumented: boolean, code: string, analysis: object }
     */
    analyzeAndInstrument(code) {
        // Analyze the code structure
        const analysis = this.cp.analyzeCode(code);

        if (!analysis) {
            return { instrumented: false, code, analysis: null };
        }

        // Check if any loops need instrumentation
        const loopsToInstrument = analysis.loops?.filter(loop => loop.needsInstrumentation) || [];

        if (loopsToInstrument.length === 0) {
            return { instrumented: false, code, analysis };
        }

        // Instrument the code with a yield mechanism
        const instrumentedCode = this.instrumentCode(code, analysis);

        return {
            instrumented: true,
            code: instrumentedCode,
            analysis
        };
    }

    /**
     * Instrument code with yield counter checks
     * @param {string} code - Original Python code
     * @param {object} analysis - Code analysis results
     * @returns {string} - Instrumented code
     */
    instrumentCode(code, analysis) {
        const lines = code.split('\n');

        // Get all loops that need instrumentation
        const loopsToInstrument = analysis.loops?.filter(loop => loop.needsInstrumentation) || [];

        if (loopsToInstrument.length === 0) {
            return code;
        }

        // Sort loops by line number in REVERSE order (bottom to top)
        // This way, inserting instrumentation doesn't change line numbers of earlier loops
        const sortedLoops = [...loopsToInstrument].sort((a, b) => b.line - a.line);

        // Instrument each loop
        for (const loop of sortedLoops) {
            const loopLineIndex = loop.line - 1; // Convert to 0-indexed

            // Find the first non-empty line after the loop header (the loop body)
            let bodyLineIndex = loopLineIndex + 1;
            while (bodyLineIndex < lines.length && !lines[bodyLineIndex].trim()) {
                bodyLineIndex++; // Skip empty lines
            }

            if (bodyLineIndex >= lines.length) {
                // No body found, skip this loop
                continue;
            }

            const bodyLine = lines[bodyLineIndex];
            const indent = bodyLine.match(/^(\s*)/)[1];

            // Create the yield check code
            // Use a unique counter per loop to avoid interference
            const loopId = `L${loop.line}_${loop.column}`;
            const yieldCheck = `${indent}__yield_counter_${loopId}__ = globals().get('__yield_counter_${loopId}__', 0) + 1
${indent}if __yield_counter_${loopId}__ >= ${this.maxIterationsPerYield}:
${indent}    globals()['__yield_counter_${loopId}__'] = 0
${indent}    raise StopIteration("__YIELD__")`;

            // Insert the yield check at the start of the loop body
            lines.splice(bodyLineIndex, 0, yieldCheck);
        }

        return lines.join('\n');
    }

    /**
     * Execute code with cooperative yielding support
     * @param {string} code - Python code to execute
     * @param {object} options - Execution options
     */
    async runWithYielding(code, options = {}) {
        const {
            onYield = null,       // Callback called on each yield
            onComplete = null,    // Callback called when execution completes
            onError = null,       // Callback called on error
            timeout = null        // Max execution time in ms
        } = options;

        const result = this.analyzeAndInstrument(code);

        if (!result.instrumented) {
            // No infinite loop detected, run normally
            try {
                this.cp.runPython(result.code);
                if (onComplete) onComplete();
            } catch (e) {
                if (onError) onError(e);
                else throw e;
            }
            return;
        }

        // Code has infinite loop - run with yielding
        const startTime = Date.now();
        let iterations = 0;
        let shouldContinue = true;

        const executeIteration = async () => {
            try {
                // Wrap in try-catch to catch our yield signal
                this.cp.runPython(result.code);

                // If we got here, the loop completed naturally (shouldn't happen with while True)
                shouldContinue = false;
                if (onComplete) onComplete();

            } catch (e) {
                // Check if this is our yield signal
                if (e.message && e.message.includes('__YIELD__')) {
                    // This is a yield point
                    iterations++;
                    this.yieldCounter++;

                    if (onYield) {
                        onYield({ iterations, yieldCount: this.yieldCounter });
                    }

                    // Check timeout
                    if (timeout && (Date.now() - startTime) > timeout) {
                        shouldContinue = false;
                        if (onComplete) onComplete({ timedOut: true });
                        return;
                    }

                    // Yield to JavaScript event loop
                    await new Promise(resolve => setTimeout(resolve, 0));

                    // Continue execution if not stopped
                    if (shouldContinue) {
                        await executeIteration();
                    }
                } else {
                    // Real error
                    shouldContinue = false;
                    if (onError) onError(e);
                    else throw e;
                }
            }
        };

        await executeIteration();
    }

    /**
     * Simple synchronous execution with automatic instrumentation
     * @param {string} code - Python code
     */
    runSafe(code) {
        const result = this.analyzeAndInstrument(code);

        if (!result.instrumented) {
            return this.cp.runPython(result.code);
        } else {
            throw new Error('Code contains infinite loop. Use runWithYielding() instead.');
        }
    }
}

// Export a factory function
export function createSupervisor(circuitPython) {
    return new CooperativeSupervisor(circuitPython);
}
