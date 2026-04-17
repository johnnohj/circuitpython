#!/usr/bin/env node
/**
 * test_node_board.mjs — Run CircuitPython board in Node.js.
 *
 * Usage:
 *   node test_node_board.mjs                    # interactive REPL
 *   node test_node_board.mjs --code 'print(1)'  # run code then REPL
 *   node test_node_board.mjs --run /path/to.py  # run file then REPL
 *   node test_node_board.mjs --run /path/to.py --exit  # run file and exit
 */

import { CircuitPython } from './js/circuitpython.mjs';
import { readFileSync } from 'node:fs';

// Parse args
let codePy = '';
let exitOnDone = false;
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
    if (args[i] === '--code' && args[i + 1]) {
        codePy = args[++i];
    } else if (args[i] === '--run' && args[i + 1]) {
        codePy = readFileSync(args[++i], 'utf-8');
    } else if (args[i] === '--exit') {
        exitOnDone = true;
    }
}

// If no TTY and code was given, default to exiting when done
if (!process.stdin.isTTY && codePy) {
    exitOnDone = true;
}

const board = await CircuitPython.create({
    wasmUrl: 'build-browser/circuitpython.wasm',
    codePy,
    onCodeDone: exitOnDone ? () => {
        board.destroy();
        process.exit(0);
    } : undefined,
});
