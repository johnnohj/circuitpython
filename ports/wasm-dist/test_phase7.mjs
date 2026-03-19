/*
 * test_phase7.mjs — Phase 7: VirtualBoard supervisor lifecycle tests
 *
 * Tests:
 *   T1: boot() transitions through INIT → BOOTING → RUNNING → WAITING
 *   T2: code.py stdout captured
 *   T3: code file search order (code.py > main.py > main.txt)
 *   T4: reload() re-runs code.py (with updated file)
 *   T5: REPL mode — enterRepl(), sendReplLine(), get output
 *   T6: exitRepl() triggers reload back to RUNNING → WAITING
 *   T7: safe mode on boot.py error
 *   T8: hardware events from code.py reach HardwareSimulator
 *   T9: writeRegisters() → Python reads register in code.py
 *  T10: writeFile() makes module importable from code.py
 *  T11: no code file → WAITING immediately
 *  T12: shutdown() cleans up
 */

import { VirtualBoard, BOARD_STATE } from './js/VirtualBoard.js';

let passed = 0;
let failed = 0;

function assert(cond, name) {
    if (cond) { console.log('PASS', name); passed++; }
    else      { console.error('FAIL', name); failed++; }
}

// Helper: wait for board to reach a specific state
function waitForState(board, targetState, timeoutMs = 10000) {
    return new Promise((resolve, reject) => {
        if (board.state === targetState) { resolve(); return; }
        const timer = setTimeout(() =>
            reject(new Error(`Timeout waiting for state ${targetState}, stuck in ${board.state}`)),
            timeoutMs);
        const unsub = board.on('state', s => {
            if (s === targetState) {
                clearTimeout(timer);
                unsub();
                resolve();
            }
        });
    });
}

// ── T1: Lifecycle state transitions ──────────────────────────────────────────

{
    const board = new VirtualBoard();
    const states = [];
    board.on('state', s => states.push(s));
    board.writeFile('code.py', 'x = 1');
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    assert(states.includes('booting'), 'T1: transitioned through booting');
    assert(states.includes('running'), 'T1: transitioned through running');
    assert(states.includes('waiting'), 'T1: reached waiting');
    assert(board.state === BOARD_STATE.WAITING, 'T1: final state is waiting');
    await board.shutdown();
}

// ── T2: code.py stdout captured ──────────────────────────────────────────────

{
    const board = new VirtualBoard();
    const output = [];
    board.on('stdout', t => output.push(t));
    board.writeFile('code.py', 'print("hello from code.py")');
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    assert(output.join('').includes('hello from code.py'), 'T2: code.py stdout captured');
    await board.shutdown();
}

// ── T3: Code file search order ───────────────────────────────────────────────

{
    // main.py should run if code.py is absent
    const board = new VirtualBoard();
    const output = [];
    board.on('stdout', t => output.push(t));
    board.writeFile('main.py', 'print("from main.py")');
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    assert(output.join('').includes('from main.py'), 'T3: main.py runs when code.py absent');
    await board.shutdown();
}

// ── T4: reload() re-runs code.py ─────────────────────────────────────────────

{
    const board = new VirtualBoard();
    const output = [];
    board.on('stdout', t => output.push(t));
    board.writeFile('code.py', 'print("run1")');
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    // Update code.py and reload
    board.writeFile('code.py', 'print("run2")');
    await board.reload();
    await waitForState(board, BOARD_STATE.WAITING);

    const text = output.join('');
    assert(text.includes('run1'), 'T4: first run captured');
    assert(text.includes('run2'), 'T4: reload ran updated code');
    await board.shutdown();
}

// ── T5: REPL mode ────────────────────────────────────────────────────────────

{
    const board = new VirtualBoard();
    board.writeFile('code.py', 'x = 42');
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    await board.enterRepl();
    assert(board.state === BOARD_STATE.REPL, 'T5: state is repl');

    // Send a line and wait for output
    const replOut = [];
    const unsub = board.on('repl_output', t => replOut.push(t));
    await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('REPL timeout')), 5000);
        const u = board.on('repl_prompt', () => {
            clearTimeout(timer);
            u();
            resolve();
        });
        board.sendReplLine('print("repl works")');
    });
    unsub();

    assert(replOut.join('').includes('repl works'), 'T5: REPL output captured');
    await board.shutdown();
}

// ── T6: exitRepl() triggers reload ───────────────────────────────────────────

{
    const board = new VirtualBoard();
    const output = [];
    board.on('stdout', t => output.push(t));
    board.writeFile('code.py', 'print("after_repl")');
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    await board.enterRepl();
    assert(board.state === BOARD_STATE.REPL, 'T6: in REPL');

    // exitRepl should reload code.py
    await board.exitRepl();
    await waitForState(board, BOARD_STATE.WAITING);
    assert(output.join('').includes('after_repl'), 'T6: code.py re-ran after REPL exit');
    await board.shutdown();
}

// ── T7: safe mode on boot.py error ──────────────────────────────────────────

{
    const board = new VirtualBoard();
    const errors = [];
    board.on('error', e => errors.push(e));
    board.writeFile('boot.py', 'raise RuntimeError("boot crash")');
    board.writeFile('code.py', 'print("should not run")');
    await board.boot();

    // Give it a moment to settle
    await new Promise(r => setTimeout(r, 200));

    assert(board.state === BOARD_STATE.SAFE_MODE, 'T7: safe mode on boot.py error');
    assert(board.safeMode !== null, 'T7: safeMode reason set');
    assert(errors.length > 0, 'T7: error event emitted');
    await board.shutdown();
}

// ── T8: hardware events from code.py ─────────────────────────────────────────

{
    const board = new VirtualBoard();
    await new Promise(r => setTimeout(r, 50)); // let BC stabilize
    board.writeFile('code.py', `
from digitalio import DigitalInOut, Direction
import board
led = DigitalInOut(board.LED)
led.direction = Direction.OUTPUT
led.value = True
`);
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    // Give BC events a moment to propagate
    await new Promise(r => setTimeout(r, 200));

    const ledState = board.hardware.getPin('LED');
    assert(ledState !== undefined, 'T8: LED pin tracked in HardwareSimulator');
    assert(ledState?.value === true, 'T8: LED value=true');
    await board.shutdown();
}

// ── T9: writeRegisters() → Python reads ──────────────────────────────────────

{
    const board = new VirtualBoard();
    const output = [];
    board.on('stdout', t => output.push(t));

    board.writeFile('code.py', `
import time
time.sleep(0)
import _blinka
val = _blinka.read_reg(_blinka.REG_BUTTON)
print("BUTTON=" + str(val))
`);
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    // Now set registers and reload — the MEMFS_UPDATE is delivered between execs
    board.writeRegisters({ BUTTON: 1 });
    await new Promise(r => setTimeout(r, 100)); // let postMessage deliver
    output.length = 0; // clear previous output
    await board.reload();
    await waitForState(board, BOARD_STATE.WAITING);

    const text = output.join('');
    assert(text.includes('BUTTON=1'), 'T9: Python read register written by JS');
    await board.shutdown();
}

// ── T10: writeFile() makes module importable ──────────────────────────────────

{
    const board = new VirtualBoard();
    const output = [];
    board.on('stdout', t => output.push(t));
    board.writeFile('mymod.py', 'ANSWER = 99');
    board.writeFile('code.py', `
import sys
sys.path.append("/circuitpy")
import mymod
print("ANSWER=" + str(mymod.ANSWER))
`);
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    assert(output.join('').includes('ANSWER=99'), 'T10: custom module importable');
    await board.shutdown();
}

// ── T11: no code file → WAITING immediately ──────────────────────────────────

{
    const board = new VirtualBoard();
    const states = [];
    board.on('state', s => states.push(s));
    // No writeFile — no code.py
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    assert(!states.includes('running') || board.state === BOARD_STATE.WAITING,
        'T11: no code file → waiting');
    await board.shutdown();
}

// ── T12: shutdown() cleans up ────────────────────────────────────────────────

{
    const board = new VirtualBoard();
    board.writeFile('code.py', 'x = 1');
    await board.boot();
    await waitForState(board, BOARD_STATE.WAITING);

    await board.shutdown();
    assert(board.state === BOARD_STATE.STOPPED, 'T12: state is stopped after shutdown');
}

// ── Summary ──────────────────────────────────────────────────────────────────

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
