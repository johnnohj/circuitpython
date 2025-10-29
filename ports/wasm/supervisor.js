// CircuitPython WASM Supervisor
// JavaScript-based supervisor that drives Python execution without blocking

class CircuitPythonSupervisor {
    constructor(wasmModule) {
        this.wasm = wasmModule;
        this.running = false;
        this.currentCode = null;
        this.setupCode = null;
        this.loopCode = null;
        this.fs = new VirtualFilesystem();
        this.currentFile = null;
        this.iterationCount = 0;
        this.fps = 60; // Target iterations per second
    }

    // ========================================================================
    // Code Loading and Parsing
    // ========================================================================

    async loadCodeFromString(pythonCode) {
        this.currentCode = pythonCode;
        console.log('Loading code:', pythonCode.substring(0, 100) + (pythonCode.length > 100 ? '...' : ''));
        const parsed = this.parseCode(pythonCode);

        this.setupCode = parsed.setup;
        this.loopCode = parsed.loop;

        return parsed;
    }

    async loadCodeFromFile(filename) {
        console.log(`Loading ${filename}...`);
        const code = await this.fs.read(filename);
        this.currentFile = filename;
        return this.loadCodeFromString(code);
    }

    parseCode(code) {
        const lines = code.split('\n');

        console.log(`Parsing ${lines.length} lines of code...`);
        console.log('First few lines:', lines.slice(0, 5).map((l, i) => `  ${i+1}: ${l}`).join('\n'));

        // Find main loop - support multiple patterns
        // while True:, while 1:, while true:
        let loopStartIdx = -1;
        let loopPattern = null;

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            // Match: while True:, while 1:, while true: (with any indentation)
            if (line.match(/^\s*while\s+(True|1|true)\s*:/)) {
                loopStartIdx = i;
                loopPattern = line.trim();
                console.log(`✓ Found loop at line ${i + 1}: ${loopPattern}`);
                break;
            }
        }

        if (loopStartIdx === -1) {
            // No loop - all setup code (valid for run-once scripts)
            console.log('✗ No main loop found - will execute code once');
            console.log('All lines checked:', lines.map((l, i) => `  ${i+1}: ${JSON.stringify(l)}`).join('\n'));
            return {
                setup: code,
                loop: null,
                hasLoop: false,
                loopPattern: null
            };
        }

        // Split into setup (before loop) and loop body
        const setupLines = lines.slice(0, loopStartIdx);
        const loopBody = this.extractLoopBody(lines, loopStartIdx);

        console.log(`Parsed: ${setupLines.length} setup lines, ${loopBody.split('\n').length} loop lines`);

        return {
            setup: setupLines.join('\n'),
            loop: loopBody,
            hasLoop: true,
            loopPattern: loopPattern
        };
    }

    extractLoopBody(lines, loopStartIdx) {
        // Get indentation of first line in loop
        const firstLoopLine = lines[loopStartIdx + 1];
        if (!firstLoopLine) return '';

        const baseIndent = this.getIndentation(firstLoopLine);
        const bodyLines = [];

        for (let i = loopStartIdx + 1; i < lines.length; i++) {
            const line = lines[i];

            // Skip empty lines
            if (line.trim() === '') {
                bodyLines.push('');
                continue;
            }

            const indent = this.getIndentation(line);

            // If dedented back to loop level or less, we're done
            if (indent < baseIndent) {
                break;
            }

            // Remove the base indentation to make it top-level code
            const dedentedLine = line.substring(baseIndent);
            bodyLines.push(dedentedLine);
        }

        return bodyLines.join('\n');
    }

    getIndentation(line) {
        const match = line.match(/^(\s*)/);
        return match ? match[1].length : 0;
    }

    // ========================================================================
    // Execution Control
    // ========================================================================

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
            this.handleError(e);
        }
    }

    stop() {
        console.log('Stopping supervisor...');
        this.running = false;
    }

    async reload() {
        console.log('Reloading...');
        this.stop();

        // Small delay to allow cleanup
        await new Promise(resolve => setTimeout(resolve, 100));

        if (this.currentCode) {
            await this.loadCodeFromString(this.currentCode);
            await this.start();
        } else if (this.currentFile) {
            await this.loadCodeFromFile(this.currentFile);
            await this.start();
        }
    }

    // ========================================================================
    // Loop Execution with Non-Blocking Sleep
    // ========================================================================

    scheduleNextIteration() {
        if (!this.running) return;

        // Use requestAnimationFrame for smooth execution
        requestAnimationFrame(() => {
            this.runIteration();
        });
    }

    runIteration() {
        if (!this.running || !this.loopCode) return;

        try {
            this.iterationCount++;

            // Execute loop body with sleep handling
            this.executeWithSleep(this.loopCode, () => {
                // After iteration completes, schedule next one
                this.scheduleNextIteration();
            });

        } catch (e) {
            console.error('Error in iteration:', e);
            this.handleError(e);
            this.stop();
        }
    }

    executeWithSleep(code, callback) {
        // Parse code into statements
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

                console.log(`Sleep ${seconds}s (${ms}ms)`);

                // Non-blocking delay!
                setTimeout(executeNext, ms);
                return;
            }

            // Execute non-sleep statement immediately
            try {
                this.wasm.runPython(stmt);
            } catch (e) {
                console.error('Error executing statement:', stmt, e);
            }

            // Continue to next statement
            executeNext();
        };

        executeNext();
    }

    splitStatements(code) {
        // Simple statement splitter
        // In production, would use proper Python parser
        const lines = code.split('\n');
        const statements = [];
        let currentStmt = '';

        for (const line of lines) {
            const trimmed = line.trim();
            if (trimmed === '') continue;

            currentStmt = line;
            statements.push(currentStmt);
        }

        return statements;
    }

    // ========================================================================
    // Auto-Run (like CircuitPython behavior)
    // ========================================================================

    async autorun() {
        console.log('Auto-run: Looking for code.py...');

        // Try standard filenames in order
        const filenames = ['code.py', 'main.py'];

        for (const filename of filenames) {
            if (await this.fs.exists(filename)) {
                console.log(`Found ${filename}, running...`);
                await this.loadCodeFromFile(filename);
                await this.start();
                return true;
            }
        }

        console.log('No code.py found, ready for manual execution');
        return false;
    }

    // ========================================================================
    // Error Handling
    // ========================================================================

    handleError(error) {
        console.error('Python Error:', error);

        // Could display in UI, enter safe mode, etc.
        if (this.onError) {
            this.onError(error);
        }
    }

    // ========================================================================
    // State Inspection (for debugging/UI)
    // ========================================================================

    getHardwareState(module) {
        // Read state from shared WASM memory
        // Access the underlying Emscripten module
        const mod = this.wasm._module || this.wasm;

        const getters = {
            gpio: () => mod._get_gpio_state_ptr?.(),
            analog: () => mod._get_analog_state_ptr?.(),
            pwm: () => mod._get_pwm_state_ptr?.(),
            rotaryio: () => mod._get_rotaryio_state_ptr?.(),
        };

        const ptr = getters[module]?.();
        if (!ptr) return null;

        return {
            ptr,
            view: new DataView(mod.HEAPU8.buffer, ptr)
        };
    }

    // ========================================================================
    // Statistics
    // ========================================================================

    getStats() {
        return {
            running: this.running,
            iterations: this.iterationCount,
            currentFile: this.currentFile,
            hasLoop: this.loopCode !== null
        };
    }
}

// ============================================================================
// Virtual Filesystem (IndexedDB + in-memory)
// ============================================================================

class VirtualFilesystem {
    constructor() {
        this.files = new Map();
        this.db = null;
        this.ready = false;
        this.initPromise = this.init();
    }

    async init() {
        // Initialize IndexedDB for persistent storage
        return new Promise((resolve, reject) => {
            const request = indexedDB.open('CircuitPythonFS', 1);

            request.onerror = () => reject(request.error);

            request.onsuccess = () => {
                this.db = request.result;
                this.loadFromDB().then(() => {
                    this.ready = true;
                    console.log('Filesystem ready');
                    resolve();
                });
            };

            request.onupgradeneeded = (event) => {
                const db = event.target.result;
                if (!db.objectStoreNames.contains('files')) {
                    db.createObjectStore('files', { keyPath: 'path' });
                }
            };
        });
    }

    async loadFromDB() {
        if (!this.db) return;

        return new Promise((resolve, reject) => {
            const tx = this.db.transaction('files', 'readonly');
            const store = tx.objectStore('files');
            const request = store.getAll();

            request.onsuccess = () => {
                for (const file of request.result) {
                    this.files.set(file.path, file.content);
                }
                console.log(`Loaded ${request.result.length} files from storage`);
                resolve();
            };

            request.onerror = () => reject(request.error);
        });
    }

    async ensureReady() {
        if (!this.ready) {
            await this.initPromise;
        }
    }

    async read(path) {
        await this.ensureReady();

        if (this.files.has(path)) {
            return this.files.get(path);
        }

        throw new Error(`File not found: ${path}`);
    }

    async write(path, content) {
        await this.ensureReady();

        // Update in-memory
        this.files.set(path, content);

        // Persist to IndexedDB
        if (this.db) {
            return new Promise((resolve, reject) => {
                const tx = this.db.transaction('files', 'readwrite');
                const store = tx.objectStore('files');
                const request = store.put({ path, content });

                request.onsuccess = () => resolve();
                request.onerror = () => reject(request.error);
            });
        }
    }

    async exists(path) {
        await this.ensureReady();
        return this.files.has(path);
    }

    async delete(path) {
        await this.ensureReady();

        this.files.delete(path);

        if (this.db) {
            return new Promise((resolve, reject) => {
                const tx = this.db.transaction('files', 'readwrite');
                const store = tx.objectStore('files');
                const request = store.delete(path);

                request.onsuccess = () => resolve();
                request.onerror = () => reject(request.error);
            });
        }
    }

    async list(directory = '/') {
        await this.ensureReady();

        const files = Array.from(this.files.keys());

        if (directory === '/') {
            return files;
        }

        // Filter by directory
        const prefix = directory.endsWith('/') ? directory : directory + '/';
        return files.filter(path => path.startsWith(prefix));
    }

    async import(file) {
        // Import from browser File API
        const content = await file.text();
        await this.write(file.name, content);
        return file.name;
    }
}

// ============================================================================
// Export for use in browser
// ============================================================================

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { CircuitPythonSupervisor, VirtualFilesystem };
}
