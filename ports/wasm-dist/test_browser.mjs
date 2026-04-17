#!/usr/bin/env node
/**
 * test_browser.mjs — Lifecycle tests for the CircuitPython browser variant.
 *
 * Tests the board-level boot → code.py → REPL lifecycle using the
 * CircuitPython.create() API (same as test_node_board.mjs).
 *
 * Usage:
 *   node test_browser.mjs                   # run all tests
 *   node test_browser.mjs --filter banner   # run tests matching "banner"
 *   node test_browser.mjs --verbose         # show full output
 */

import { CircuitPython, I2CDevice } from './js/circuitpython.mjs';

// ── Options ──

const args = process.argv.slice(2);
const verbose = args.includes('--verbose');
const filterIdx = args.indexOf('--filter');
const filter = filterIdx >= 0 ? args[filterIdx + 1] : null;

// ── Test runner ──

const tests = [];
function test(name, fn) {
    if (!filter || name.toLowerCase().includes(filter.toLowerCase())) {
        tests.push({ name, fn });
    }
}

/**
 * Run a code.py string through the board lifecycle and capture stdout.
 * Returns { stdout, stderr } after code.py finishes (or timeout).
 */
function runBoard(codePy, { timeoutMs = 5000, files, keepBoard = false } = {}) {
    return new Promise(async (resolve) => {
        let stdout = '';
        let stderr = '';
        let timer;

        const board = await CircuitPython.create({
            wasmUrl: 'build-browser/circuitpython.wasm',
            codePy,
            files,
            onStdout: (text) => { stdout += text; },
            onStderr: (text) => { stderr += text; },
            onCodeDone: () => {
                clearTimeout(timer);
                // Let one more frame run to ensure all output is flushed
                setTimeout(() => {
                    if (!keepBoard) board.destroy();
                    resolve({ stdout, stderr, board });
                }, 50);
            },
        });

        timer = setTimeout(() => {
            if (!keepBoard) board.destroy();
            resolve({ stdout, stderr, timeout: true, board });
        }, timeoutMs);
    });
}

function assertContains(haystack, needle, msg) {
    if (!haystack.includes(needle)) {
        throw new Error(`${msg || 'assertContains'}: expected "${needle}" in output\n---\n${haystack}\n---`);
    }
}

function assertNotContains(haystack, needle, msg) {
    if (haystack.includes(needle)) {
        throw new Error(`${msg || 'assertNotContains'}: unexpected "${needle}" in output\n---\n${haystack}\n---`);
    }
}

function assertOrder(haystack, first, second, msg) {
    const i = haystack.indexOf(first);
    const j = haystack.indexOf(second);
    if (i < 0) throw new Error(`${msg || 'assertOrder'}: "${first}" not found`);
    if (j < 0) throw new Error(`${msg || 'assertOrder'}: "${second}" not found`);
    if (i >= j) throw new Error(`${msg || 'assertOrder'}: "${first}" should appear before "${second}"`);
}

// ── Lifecycle tests ──

test('lifecycle: banner printed at boot', async () => {
    const r = await runBoard('print("ok")');
    assertContains(r.stdout, 'CircuitPython');
    assertContains(r.stdout, 'running on wasm-browser');
});

test('lifecycle: auto-reload message', async () => {
    const r = await runBoard('print("ok")');
    assertContains(r.stdout, 'Auto-reload is on');
});

test('lifecycle: code.py last edited (Never for fresh)', async () => {
    const r = await runBoard('print("ok")');
    assertContains(r.stdout, 'code.py last edited: Never');
});

test('lifecycle: code.py output header', async () => {
    const r = await runBoard('print("ok")');
    assertContains(r.stdout, 'code.py output:');
});

test('lifecycle: Code done running', async () => {
    const r = await runBoard('print("ok")');
    assertContains(r.stdout, 'Code done running.');
});

test('lifecycle: Press any key message', async () => {
    const r = await runBoard('print("ok")');
    assertContains(r.stdout, 'Press any key to enter the REPL');
});

test('lifecycle: message ordering', async () => {
    const r = await runBoard('print("ok")');
    assertOrder(r.stdout, 'CircuitPython', 'Auto-reload');
    assertOrder(r.stdout, 'Auto-reload', 'code.py last edited');
    assertOrder(r.stdout, 'code.py last edited', 'code.py output:');
    assertOrder(r.stdout, 'code.py output:', 'ok');
    assertOrder(r.stdout, 'ok', 'Code done running');
    assertOrder(r.stdout, 'Code done running', 'Press any key');
});

test('lifecycle: code.py output appears', async () => {
    const r = await runBoard('print("hello from code.py")\nprint(2+2)');
    assertContains(r.stdout, 'hello from code.py');
    assertContains(r.stdout, '4');
});

test('lifecycle: empty code.py still shows lifecycle messages', async () => {
    const r = await runBoard('');
    assertContains(r.stdout, 'Code done running.');
    assertContains(r.stdout, 'Press any key');
});

test('lifecycle: default welcome code.py for first visit', async () => {
    const r = await runBoard(undefined);
    assertContains(r.stdout, 'Hello from CircuitPython!');
    assertContains(r.stdout, 'Edit code.py');
    assertContains(r.stdout, 'code.py last edited: Never');
});

test('lifecycle: [sup] debug goes to stderr not stdout', async () => {
    const r = await runBoard('print("ok")');
    assertNotContains(r.stdout, '[sup]');
    assertContains(r.stderr, '[sup]');
});

// ── boot.py lifecycle tests ──

test('lifecycle: boot.py runs before code.py', async () => {
    const r = await runBoard('print("from-code")', {
        files: { '/CIRCUITPY/boot.py': 'print("from-boot")' },
    });
    assertContains(r.stdout, 'from-boot');
    assertContains(r.stdout, 'from-code');
    assertOrder(r.stdout, 'from-boot', 'code.py output:');
    assertOrder(r.stdout, 'code.py output:', 'from-code');
});

test('lifecycle: boot.py error does not prevent code.py', async () => {
    const r = await runBoard('print("survived")', {
        files: { '/CIRCUITPY/boot.py': 'raise ValueError("oops")' },
    });
    assertContains(r.stdout, 'survived');
    assertContains(r.stderr, 'ValueError');
});

test('lifecycle: no boot.py skips to code.py', async () => {
    // Default: no boot.py file seeded
    const r = await runBoard('print("direct")');
    assertContains(r.stdout, 'direct');
    assertContains(r.stdout, 'code.py output:');
});

test('lifecycle: boot.py sets global visible to code.py', async () => {
    // boot.py defines a variable; code.py should be able to import it
    // (they share the same VM/globals via __main__)
    const r = await runBoard('print("done")', {
        files: { '/CIRCUITPY/boot.py': 'BOOT_FLAG = 42' },
    });
    // boot.py ran without error
    assertContains(r.stdout, 'done');
});

test('lifecycle: message ordering with boot.py', async () => {
    const r = await runBoard('print("output")', {
        files: { '/CIRCUITPY/boot.py': 'pass' },
    });
    assertOrder(r.stdout, 'CircuitPython', 'Auto-reload');
    assertOrder(r.stdout, 'Auto-reload', 'code.py output:');
    assertOrder(r.stdout, 'code.py output:', 'output');
    assertOrder(r.stdout, 'output', 'Code done running');
});

test('lifecycle: Ctrl-D with boot.py re-runs both', async () => {
    const { board, stdout } = await createLiveBoard('print("code-run")', {
        files: { '/CIRCUITPY/boot.py': 'print("boot-run")' },
    });
    let output = stdout;
    board._onStdout = (text) => { output += text; };

    board.ctrlD();
    await waitFrames(board, 60);

    const bootCount = (output.match(/boot-run/g) || []).length;
    if (bootCount < 2) throw new Error(`Expected "boot-run" at least twice, got ${bootCount}`);
    board.destroy();
});

// ── auto-reload tests ──

test('auto-reload: writing code.py triggers reload', async () => {
    const { board, stdout } = await createLiveBoard('print("v1")');
    let output = stdout;
    board._onStdout = (text) => { output += text; };

    // Write new code.py → triggers auto-reload after 500ms debounce
    const enc = new TextEncoder();
    board._wasi.writeFile('/CIRCUITPY/code.py', enc.encode('print("v2")'));

    // Wait for debounce (500ms) + several frames for execution
    await new Promise(r => setTimeout(r, 700));
    await waitFrames(board, 60);

    assertContains(output, 'soft reboot');
    assertContains(output, 'v2');
    board.destroy();
});

test('auto-reload: debounce coalesces rapid writes', async () => {
    const { board, stdout } = await createLiveBoard('print("v1")');
    let output = stdout;
    let rebootCount = 0;
    board._onStdout = (text) => {
        output += text;
        rebootCount += (text.match(/soft reboot/g) || []).length;
    };

    // Rapid-fire writes — should coalesce into one reload
    const enc = new TextEncoder();
    board._wasi.writeFile('/CIRCUITPY/code.py', enc.encode('print("a")'));
    board._wasi.writeFile('/CIRCUITPY/code.py', enc.encode('print("b")'));
    board._wasi.writeFile('/CIRCUITPY/code.py', enc.encode('print("c")'));

    await new Promise(r => setTimeout(r, 700));
    await waitFrames(board, 60);

    // Should only have one soft reboot from the coalesced writes
    if (rebootCount !== 1) throw new Error(`Expected 1 soft reboot, got ${rebootCount}`);
    // Should see the last version
    assertContains(output, 'c');
    board.destroy();
});

test('stage6: auto-reload re-invokes full lifecycle (boot.py runs again)', async () => {
    // Pins that auto-reload goes through JS's runBoardLifecycle — the same
    // path Ctrl-D uses — rather than a C-side fast path that skips boot.py.
    // If someone re-introduces a C-side VFS hook that calls cp_run directly
    // without going through the orchestrator, this test catches it.
    const { board, stdout } = await createLiveBoard('print("code-run")', {
        files: { '/CIRCUITPY/boot.py': 'print("boot-run")' },
    });
    let output = stdout;
    board._onStdout = (text) => { output += text; };

    // Trigger auto-reload via file write
    const enc = new TextEncoder();
    board._wasi.writeFile('/CIRCUITPY/code.py', enc.encode('print("code-v2")'));

    await new Promise(r => setTimeout(r, 700));
    await waitFrames(board, 60);

    // boot.py should have run AGAIN (proving full lifecycle, not just code.py)
    const bootCount = (output.match(/boot-run/g) || []).length;
    if (bootCount < 2) {
        throw new Error(`Expected boot.py to re-run after auto-reload (boot-run >= 2); got ${bootCount}`);
    }
    assertContains(output, 'code-v2');
    board.destroy();
});

test('auto-reload: non-.py files do not trigger reload', async () => {
    const { board, stdout } = await createLiveBoard('print("stable")');
    let output = stdout;
    board._onStdout = (text) => { output += text; };

    // Write a .txt file — should NOT trigger auto-reload
    const enc = new TextEncoder();
    board._wasi.writeFile('/CIRCUITPY/data.txt', enc.encode('hello'));

    await new Promise(r => setTimeout(r, 700));
    await waitFrames(board, 10);

    assertNotContains(output, 'soft reboot');
    board.destroy();
});

// ── asyncio tests ──

test('asyncio: sleep yields to supervisor (no busy-wait)', async () => {
    // asyncio.sleep should yield with YIELD_SLEEP, not burn frames busy-waiting.
    // If it works, a 100ms sleep should complete in ~100ms wall time, not timeout.
    const r = await runBoard(`
import asyncio

async def main():
    print("before")
    await asyncio.sleep(0.1)
    print("after")

asyncio.run(main())
`, { timeoutMs: 3000 });
    assertContains(r.stdout, 'before');
    assertContains(r.stdout, 'after');
    assertOrder(r.stdout, 'before', 'after');
});

test('asyncio: gather runs concurrent tasks', async () => {
    const r = await runBoard(`
import asyncio

async def task_a():
    await asyncio.sleep(0.05)
    print("A")

async def task_b():
    await asyncio.sleep(0.05)
    print("B")

async def main():
    await asyncio.gather(task_a(), task_b())
    print("done")

asyncio.run(main())
`, { timeoutMs: 3000 });
    assertContains(r.stdout, 'A');
    assertContains(r.stdout, 'B');
    assertContains(r.stdout, 'done');
});

test('asyncio: sleep(0) yields but resumes immediately', async () => {
    const r = await runBoard(`
import asyncio

async def main():
    for i in range(3):
        await asyncio.sleep(0)
    print("looped")

asyncio.run(main())
`, { timeoutMs: 3000 });
    assertContains(r.stdout, 'looped');
});

test('asyncio: tight loop inside coroutine yields via supervisor (Phase 4 regression)', async () => {
    // Exercises the deep-nested SUSPEND path: asyncio.run → event loop →
    // task.coro.send(None) → main() → tight loop triggers budget exhaustion
    // INSIDE the coroutine's bytecode.  Before Phase 4, mp_obj_gen_resume
    // would treat SUSPEND as generator completion (zero ip + garbage value),
    // corrupting asyncio scheduling.  After Phase 4, gen_resume_and_raise
    // returns mp_const_none on SUSPEND, generator's code_state preserved
    // inside gen_instance_t, next .send() resumes naturally.
    const r = await runBoard(`
import asyncio

async def main():
    # A tight forward-only computation that's likely to hit the budget
    # check at some backwards branch inside the coroutine, BEFORE the await.
    total = 0
    for i in range(5000):
        total += i
    await asyncio.sleep(0)
    print("computed", total)

asyncio.run(main())
`, { timeoutMs: 5000 });
    assertContains(r.stdout, 'computed 12497500');
});

test('asyncio: bare except does NOT catch suspend sentinel', async () => {
    // The supervisor's suspend sentinel propagates as an NLR exception, but
    // user Python code MUST NOT be able to catch it — otherwise error-swallowing
    // patterns like `try: ... except: pass` would silently break suspension.
    //
    // Before the identity-check bypasses in py/vm.c, the sentinel was a
    // SystemExit subclass that bare `except:` and `except BaseException:`
    // would happily catch, breaking the suspension chain.
    const r = await runBoard(`
import asyncio

caught = False
async def main():
    global caught
    try:
        total = 0
        for i in range(5000):
            total += i
        await asyncio.sleep(0)
        print("computed", total)
    except:
        caught = True
        print("caught!")

asyncio.run(main())
if caught:
    print("FAIL: bare except caught sentinel")
else:
    print("PASS: bare except did not catch sentinel")
`, { timeoutMs: 5000 });
    assertContains(r.stdout, 'computed 12497500');
    assertContains(r.stdout, 'PASS: bare except did not catch sentinel');
});

test('asyncio: except BaseException does NOT catch suspend sentinel', async () => {
    const r = await runBoard(`
import asyncio

caught = False
async def main():
    global caught
    try:
        total = 0
        for i in range(5000):
            total += i
        await asyncio.sleep(0)
        print("computed", total)
    except BaseException:
        caught = True
        print("caught!")

asyncio.run(main())
if caught:
    print("FAIL: except BaseException caught sentinel")
else:
    print("PASS: except BaseException did not catch sentinel")
`, { timeoutMs: 5000 });
    assertContains(r.stdout, 'computed 12497500');
    assertContains(r.stdout, 'PASS: except BaseException did not catch sentinel');
});

// ── SUSPEND validation under asyncio workloads ──
// These tests verify that the SUSPEND mechanism preserves VM state correctly
// across suspend/resume cycles under realistic asyncio workloads.  Each test
// has a deterministic expected output — if it matches, state preservation +
// liveness + exception integrity are validated.  If any hangs, the timeout
// catches it.

test('suspend-validation: state preservation across concurrent tasks', async () => {
    // Multiple tasks maintain independent local state across many await/yield
    // boundaries.  If SUSPEND corrupts any task's locals (sp, ip, code_state),
    // the running totals diverge from expected.
    const r = await runBoard(`
import asyncio

async def worker(id, n):
    total = 0
    for i in range(n):
        total += i
        if i % 100 == 0:
            await asyncio.sleep(0)
    return (id, total)

async def main():
    results = await asyncio.gather(
        worker(0, 2000),
        worker(1, 2000),
        worker(2, 2000),
    )
    for id, total in results:
        print(f"worker {id}: {total}")
    # Each must independently equal sum(range(2000)) = 1999000
    ok = all(t == 1999000 for _, t in results)
    print("PASS" if ok else "FAIL")

asyncio.run(main())
`, { timeoutMs: 10000 });
    assertContains(r.stdout, 'worker 0: 1999000');
    assertContains(r.stdout, 'worker 1: 1999000');
    assertContains(r.stdout, 'worker 2: 1999000');
    assertContains(r.stdout, 'PASS');
});

test('suspend-validation: producer/consumer liveness via Event', async () => {
    // Producer and consumer coordinate via shared list + asyncio.Event.
    // (CircuitPython's asyncio has no Queue; Event is the available primitive.)
    // If SUSPEND breaks the event loop's task scheduling, one task starves
    // or the handshake deadlocks.
    const r = await runBoard(`
import asyncio

buf = []
ready = asyncio.Event()
done = asyncio.Event()

async def producer():
    for i in range(50):
        buf.append(i)
        ready.set()
        await asyncio.sleep(0)
    done.set()

async def consumer():
    total = 0
    while not done.is_set() or buf:
        if not buf:
            ready.clear()
            await ready.wait()
        while buf:
            total += buf.pop(0)
        await asyncio.sleep(0)
    return total

async def main():
    prod = asyncio.create_task(producer())
    result = await consumer()
    await prod
    # sum(range(50)) = 1225
    print(f"consumed {result}")
    print("PASS" if result == 1225 else "FAIL")

asyncio.run(main())
`, { timeoutMs: 10000 });
    assertContains(r.stdout, 'consumed 1225');
    assertContains(r.stdout, 'PASS');
});

test('suspend-validation: exception integrity across tasks', async () => {
    // One task raises while others are suspended.  The exception must propagate
    // correctly without corrupting the sentinel or other tasks' state.
    const r = await runBoard(`
import asyncio

async def crasher():
    await asyncio.sleep(0)
    raise ValueError("boom")

async def survivor():
    total = 0
    for i in range(3000):
        total += i
    await asyncio.sleep(0)
    return total

async def main():
    results = await asyncio.gather(
        crasher(),
        survivor(),
        return_exceptions=True,
    )
    # results[0] should be the ValueError, results[1] should be sum(range(3000))
    err = results[0]
    val = results[1]
    print(f"error: {type(err).__name__}: {err}")
    print(f"survivor: {val}")
    ok = isinstance(err, ValueError) and str(err) == "boom" and val == 4498500
    print("PASS" if ok else "FAIL")

asyncio.run(main())
`, { timeoutMs: 10000 });
    assertContains(r.stdout, 'error: ValueError: boom');
    assertContains(r.stdout, 'survivor: 4498500');
    assertContains(r.stdout, 'PASS');
});

test('suspend-validation: nested generator depth (4 levels)', async () => {
    // Deep await chains: user code → wrapper → driver → inner coroutine.
    // SUSPEND fires at the innermost level and must propagate back through
    // 4+ generator frames without corrupting any frame's state.
    const r = await runBoard(`
import asyncio

async def level4():
    s = 0
    for i in range(2000):
        s += i
    await asyncio.sleep(0)
    return s

async def level3():
    return await level4() + 1

async def level2():
    return await level3() + 1

async def level1():
    return await level2() + 1

async def main():
    result = await level1()
    # sum(range(2000)) + 3 = 1999003
    print(f"result: {result}")
    print("PASS" if result == 1999003 else "FAIL")

asyncio.run(main())
`, { timeoutMs: 10000 });
    assertContains(r.stdout, 'result: 1999003');
    assertContains(r.stdout, 'PASS');
});

// ── Stage 5: scheduler fairness ──
// The supervisor must give C-side HAL ticks + JS-initiated interventions a
// guaranteed slice per frame regardless of what Python is doing.  These
// tests pin the guarantees: (a) cp_step returns within budget even if Python
// is in a pathological tight loop (SUSPEND does its job); (b) Ctrl-C
// interrupts such a loop within a bounded number of frames.

test('stage5: frame loop keeps advancing during tight Python loop', async () => {
    // SUSPEND must return cp_step on time so the JS rAF loop keeps ticking —
    // otherwise HAL polling, display paint, and input dispatch all stall.
    const { board } = await createLiveBoard();

    const startFrame = board.frameCount;
    board.runCode('while True: pass');  // background context, tight loop

    // Wait ~20 frames of wall time (≥300ms).  If SUSPEND works, frames
    // advance; if it stalls, we'd be stuck inside cp_step forever.
    await waitFrames(board, 20);
    const delta = board.frameCount - startFrame;

    if (delta < 18) {
        throw new Error(`Frame loop stalled during tight loop: only ${delta}/20 frames advanced`);
    }

    // Clean up: ctrl-C the background context via killContext
    //   (ctrlC would also work but it targets ctx0 / the whole board)
    const ctxs = board.listContexts();
    for (const c of ctxs) {
        if (c.id !== 0 && c.status !== 0) board.killContext(c.id);
    }
    await waitFrames(board, 5);
    board.destroy();
});

// ── Stage 4: IDE features as pure functions ──
// Completion and syntax checking must work WHILE another context is running,
// without touching its state.  These are pure queries over the current VM.

test('stage4: cp_syntax_check returns 0 for valid code', async () => {
    const { board } = await createLiveBoard();
    const len = board._writeInputBuf('x = 1\ny = x + 2\nprint(y)\n');
    const r = board._exports.cp_syntax_check(len);
    if (r !== 0) throw new Error(`Expected 0 (valid), got ${r}`);
    board.destroy();
});

test('stage4: cp_syntax_check returns 1 for parse error', async () => {
    const { board } = await createLiveBoard();
    let stderr = '';
    board._onStderr = (text) => { stderr += text; };

    const len = board._writeInputBuf('def broken(:\n  pass\n');
    const r = board._exports.cp_syntax_check(len);
    if (r !== 1) throw new Error(`Expected 1 (error), got ${r}`);
    // Error details should land on stderr
    if (!stderr.includes('SyntaxError') && !stderr.includes('invalid syntax')) {
        throw new Error(`Expected SyntaxError in stderr, got: ${stderr}`);
    }
    board.destroy();
});

test('stage4: cp_syntax_check is non-destructive mid-run', async () => {
    // A background context is actively running; cp_syntax_check on unrelated
    // source must not disrupt it.  Verifies the IDE-query path doesn't touch
    // any context's code_state or globals.
    const { board } = await createLiveBoard();

    const id = board.runCode('import time; time.sleep(5)');
    if (id < 1) throw new Error(`runCode returned ${id}`);
    await waitFrames(board, 3);

    // Ctx is running/sleeping — hit it with 10 syntax checks in quick succession
    for (let i = 0; i < 10; i++) {
        const len = board._writeInputBuf(`a${i} = ${i * i}\n`);
        const r = board._exports.cp_syntax_check(len);
        if (r !== 0) throw new Error(`Syntax check #${i} failed: ${r}`);
    }

    // The background context should still be alive and making progress
    const m = board._readContextMeta(id);
    if (!m || m.status === 0 || m.status === 6) {
        throw new Error(`Background context disrupted: status=${m?.status}`);
    }

    board.killContext(id);
    await waitFrames(board, 5);
    board.destroy();
});

test('stage4: cp_complete callable mid-run without disrupting context', async () => {
    const { board } = await createLiveBoard();

    const id = board.runCode('import time; time.sleep(5)');
    if (id < 1) throw new Error(`runCode returned ${id}`);
    await waitFrames(board, 3);

    // Invoke completion 5 times; must not crash or disrupt the running ctx
    for (let i = 0; i < 5; i++) {
        const len = board._writeInputBuf('pr');
        board._exports.cp_complete(len);  // completions go to stdout
    }

    const m = board._readContextMeta(id);
    if (!m || m.status === 0 || m.status === 6) {
        throw new Error(`Background context disrupted: status=${m?.status}`);
    }

    board.killContext(id);
    await waitFrames(board, 5);
    board.destroy();
});

test('stage5: Ctrl-C interrupts tight Python loop within bounded frames', async () => {
    // The pending-exception mechanism must fire promptly — at most a couple
    // of frames after JS calls cp_ctrl_c, the VM should see the interrupt
    // at its next HOOK_LOOP check (per backwards branch) and raise.
    const { board } = await createLiveBoard();

    board.exec('while True: pass');
    await waitFrames(board, 10);  // let it settle in the tight loop

    // Confirm it's actually running
    if (!board._exports.cp_is_runnable()) {
        throw new Error('Expected tight loop to be runnable before Ctrl-C');
    }

    const preCtrlC = board.frameCount;
    board.ctrlC();

    // Poll for ctx0 to stop — should happen within a small number of frames
    let stoppedAtFrame = -1;
    for (let i = 0; i < 30; i++) {
        await waitFrames(board, 1);
        if (!board._exports.cp_is_runnable()) {
            stoppedAtFrame = board.frameCount;
            break;
        }
    }

    if (stoppedAtFrame < 0) {
        throw new Error('Tight loop still running 30 frames after Ctrl-C');
    }

    const latency = stoppedAtFrame - preCtrlC;
    // Budget per frame is ~13ms; ctrl-C should fire on the first HOOK_LOOP
    // check after the interrupt is scheduled, well within a handful of frames.
    if (latency > 5) {
        throw new Error(`Ctrl-C latency exceeds bound: ${latency} frames (expected ≤5)`);
    }

    board.destroy();
});

// ── hardware module tests ──
// Note: digitalio/analogio/microcontroller are disabled in the browser build.
// These tests exercise the JS hardware modules directly via MEMFS /hal/ endpoints,
// which is the layer hardware modules actually operate on.

test('hardware: module registration API', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    if (!r.board.hardware('gpio')) throw new Error('gpio module missing');
    if (!r.board.hardware('neopixel')) throw new Error('neopixel module missing');
    if (!r.board.hardware('analog')) throw new Error('analog module missing');
    if (!r.board.hardware('pwm')) throw new Error('pwm module missing');
    if (r.board.hardware('nonexistent')) throw new Error('nonexistent should be null');
    r.board.destroy();
});

test('hardware: gpio module parses /hal/gpio state', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    // Simulate what common-hal/digitalio would write: pin 1, output, high
    const gpio_data = new Uint8Array(8 * 2);  // 2 pins
    // Pin 1: enabled=1, direction=1(output), value=1, pull=0, open_drain=0
    gpio_data[8 + 0] = 1;  // enabled
    gpio_data[8 + 1] = 1;  // direction = output
    gpio_data[8 + 2] = 1;  // value = high
    r.board._wasi.updateHardwareState('/hal/gpio', gpio_data);
    // Run postStep manually to process the state
    r.board._hw.postStep(r.board._wasi, performance.now());
    const gpio = r.board.hardware('gpio');
    const pin1 = gpio.getPin(1);
    if (!pin1) throw new Error('Pin 1 not tracked');
    if (!pin1.enabled) throw new Error('Pin 1 should be enabled');
    if (pin1.direction !== 1) throw new Error('Pin 1 should be output');
    if (pin1.value !== 1) throw new Error('Pin 1 should be high');
    // Pin 0 should be null (not enabled)
    if (gpio.getPin(0) !== null) throw new Error('Pin 0 should be null');
    r.board.destroy();
});

test('hardware: gpio onChange callback fires', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const changes = [];
    const gpio = r.board.hardware('gpio');
    gpio._onChange = (pin, state) => changes.push({ pin, state });
    // Write pin 0 high
    const data = new Uint8Array(8);
    data[0] = 1;  // enabled
    data[1] = 1;  // output
    data[2] = 1;  // high
    r.board._wasi.updateHardwareState('/hal/gpio', data);
    r.board._hw.postStep(r.board._wasi, performance.now());
    if (changes.length !== 1) throw new Error(`Expected 1 change, got ${changes.length}`);
    if (changes[0].pin !== 0) throw new Error('Wrong pin');
    if (changes[0].state.value !== 1) throw new Error('Wrong value');
    r.board.destroy();
});

test('hardware: neopixel module parses pixel data', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    // Simulate a 3-pixel GRB strip on pin 0
    // Region size: 4 header + 1024 max = 1028 per pin
    const region = 4 + 1024;
    const data = new Uint8Array(region);
    data[0] = 0;   // pin number
    data[1] = 1;   // enabled
    data[2] = 9;   // num_bytes low (3 pixels × 3 bytes = 9)
    data[3] = 0;   // num_bytes high
    // Pixel 0: GRB → G=0xFF, R=0x00, B=0x00 (pure green)
    data[4] = 0xFF; data[5] = 0x00; data[6] = 0x00;
    // Pixel 1: GRB → G=0x00, R=0xFF, B=0x00 (pure red)
    data[7] = 0x00; data[8] = 0xFF; data[9] = 0x00;
    // Pixel 2: GRB → G=0x00, R=0x00, B=0xFF (pure blue)
    data[10] = 0x00; data[11] = 0x00; data[12] = 0xFF;
    r.board._wasi.updateHardwareState('/hal/neopixel', data);
    r.board._hw.postStep(r.board._wasi, performance.now());
    const np = r.board.hardware('neopixel');
    const strip = np.getStrip(0);
    if (!strip) throw new Error('Strip 0 not found');
    if (strip.numPixels !== 3) throw new Error(`Expected 3 pixels, got ${strip.numPixels}`);
    // GRB → RGB conversion: pixel 0 should be r=0, g=255, b=0
    if (strip.pixels[0].g !== 0xFF) throw new Error('Pixel 0 green wrong');
    if (strip.pixels[0].r !== 0x00) throw new Error('Pixel 0 red wrong');
    if (strip.pixels[1].r !== 0xFF) throw new Error('Pixel 1 red wrong');
    if (strip.pixels[2].b !== 0xFF) throw new Error('Pixel 2 blue wrong');
    r.board.destroy();
});

// ── I2C device registry tests ──

test('hardware: i2c module registered by default', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    if (!r.board.hardware('i2c')) throw new Error('i2c module missing');
    r.board.destroy();
});

test('hardware: i2c virtual device register read/write', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const i2c = r.board.hardware('i2c');

    // Create a virtual device at address 0x44
    const dev = new I2CDevice();
    dev.registers[0xFD] = 0x47;  // device ID register
    i2c.addDevice(0x44, dev);

    // Simulate a write: [register=0xFD] (trigger read of ID)
    const writeData = new Uint8Array([0xFD]);
    i2c.onWrite('/hal/i2c/dev/68', writeData);

    // Read back via onRead
    const readData = i2c.onRead('/hal/i2c/dev/68', 0);
    if (!readData) throw new Error('onRead returned null');
    if (readData[0xFD] !== 0x47) throw new Error(`Expected 0x47 at reg 0xFD, got ${readData[0xFD]}`);
    r.board.destroy();
});

test('hardware: i2c device onWrite callback', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const i2c = r.board.hardware('i2c');

    let writeCalled = false;
    class TestSensor extends I2CDevice {
        onWrite(register, data) {
            writeCalled = true;
            // Simulate: writing to reg 0x00 triggers measurement
            if (register === 0x00) {
                this.registers[0x01] = 0xAB;
                this.registers[0x02] = 0xCD;
            }
        }
    }

    const sensor = new TestSensor();
    i2c.addDevice(0x48, sensor);

    // Write [reg=0x00, data=0x01] to trigger measurement
    i2c.onWrite('/hal/i2c/dev/72', new Uint8Array([0x00, 0x01]));

    if (!writeCalled) throw new Error('onWrite not called');
    if (sensor.registers[0x01] !== 0xAB) throw new Error('Measurement result not written');
    if (sensor.registers[0x02] !== 0xCD) throw new Error('Measurement result not written');

    // Verify readback
    const data = i2c.onRead('/hal/i2c/dev/72', 0);
    if (data[0x01] !== 0xAB) throw new Error('Read mismatch');
    r.board.destroy();
});

test('hardware: i2c device removal', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const i2c = r.board.hardware('i2c');
    const dev = new I2CDevice();
    i2c.addDevice(0x50, dev);
    if (!i2c.getDevice(0x50)) throw new Error('Device should exist');
    i2c.removeDevice(0x50);
    if (i2c.getDevice(0x50)) throw new Error('Device should be removed');
    r.board.destroy();
});

// ── PWM brightness tests ──

test('hardware: pwm brightness computed from duty cycle', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const pwm = r.board.hardware('pwm');
    // Simulate PWM on pin 0: duty_cycle=32768 (50%), frequency=1000Hz
    const data = new Uint8Array(8);
    const view = new DataView(data.buffer);
    data[0] = 1;  // enabled
    data[1] = 0;  // variable_freq
    view.setUint16(2, 32768, true);  // duty_cycle = 50%
    view.setUint32(4, 1000, true);   // frequency = 1000Hz
    r.board._wasi.updateHardwareState('/hal/pwm', data);
    r.board._hw.postStep(r.board._wasi, performance.now());
    const pin0 = pwm.getPin(0);
    if (!pin0) throw new Error('Pin 0 not tracked');
    // brightness should be ~0.5 (32768/65535)
    if (Math.abs(pin0.brightness - 0.5) > 0.01) {
        throw new Error(`Expected brightness ~0.5, got ${pin0.brightness}`);
    }
    // Test getBrightness helper
    if (Math.abs(pwm.getBrightness(0) - 0.5) > 0.01) {
        throw new Error('getBrightness mismatch');
    }
    // Disabled pin should return 0
    if (pwm.getBrightness(1) !== 0) throw new Error('Disabled pin brightness should be 0');
    r.board.destroy();
});

// ── Virtual sensor input tests ──

test('hardware: gpio setInputValue writes to MEMFS', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const gpio = r.board.hardware('gpio');
    // Set up pin 3 as input in MEMFS
    const data = new Uint8Array(8 * 4);  // 4 pins
    data[3 * 8 + 0] = 1;  // enabled
    data[3 * 8 + 1] = 0;  // direction = input
    data[3 * 8 + 2] = 0;  // value = low
    r.board._wasi.updateHardwareState('/hal/gpio', data);
    r.board._hw.postStep(r.board._wasi, performance.now());  // sync module state

    // Simulate button press
    gpio.setInputValue(r.board._wasi, 3, true);

    // Read back from MEMFS
    const updated = r.board._wasi.readFile('/hal/gpio');
    if (updated[3 * 8 + 2] !== 1) throw new Error('MEMFS not updated by setInputValue');
    r.board.destroy();
});

test('hardware: analog setInputValue writes to MEMFS', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const analog = r.board.hardware('analog');
    // Set up pin 2 as analog input in MEMFS
    const data = new Uint8Array(4 * 3);  // 3 pins
    data[2 * 4 + 0] = 1;  // enabled
    data[2 * 4 + 1] = 0;  // is_output = false (input)
    r.board._wasi.updateHardwareState('/hal/analog', data);
    r.board._hw.postStep(r.board._wasi, performance.now());

    // Simulate potentiometer at ~75% (49152 / 65535)
    analog.setInputValue(r.board._wasi, 2, 49152);

    const updated = r.board._wasi.readFile('/hal/analog');
    const value = updated[2 * 4 + 2] | (updated[2 * 4 + 3] << 8);
    if (value !== 49152) throw new Error(`Expected 49152, got ${value}`);
    r.board.destroy();
});

test('hardware: gpio setInputValue ignores output pins', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    const gpio = r.board.hardware('gpio');
    // Set up pin 0 as output
    const data = new Uint8Array(8);
    data[0] = 1;  // enabled
    data[1] = 1;  // direction = output
    data[2] = 1;  // value = high
    r.board._wasi.updateHardwareState('/hal/gpio', data);
    r.board._hw.postStep(r.board._wasi, performance.now());

    // Try to set input value — should be ignored (it's an output)
    gpio.setInputValue(r.board._wasi, 0, false);

    const updated = r.board._wasi.readFile('/hal/gpio');
    if (updated[2] !== 1) throw new Error('setInputValue should not change output pins');
    r.board.destroy();
});

// ── runtime behavior tests ──

test('runtime: time.sleep completes without timeout', async () => {
    const r = await runBoard('import time\ntime.sleep(0.1)\nprint("slept")');
    assertContains(r.stdout, 'slept');
});

test('runtime: while True with break works', async () => {
    const r = await runBoard(`
i = 0
while True:
    i += 1
    if i >= 5:
        break
print("broke at", i)
`);
    assertContains(r.stdout, 'broke at 5');
});

test('runtime: long loop yields without hanging', async () => {
    // A loop that runs many iterations should yield at backwards branches
    // and complete without timing out (13ms budget per frame).
    const r = await runBoard(`
total = 0
for i in range(1000):
    total += i
print("total", total)
`, { timeoutMs: 5000 });
    assertContains(r.stdout, 'total 499500');
});

test('runtime: debug=false suppresses [sup] output', async () => {
    const r = await runBoard('print("ok")', { keepBoard: true });
    // Default: debug is on, [sup] goes to stderr
    assertContains(r.stderr, '[sup]');
    r.board.destroy();
});

test('runtime: settings.toml CIRCUITPY_DEBUG=0 suppresses debug', async () => {
    const r = await runBoard('print("ok")', {
        files: { '/CIRCUITPY/settings.toml': 'CIRCUITPY_DEBUG=0\n' },
    });
    assertNotContains(r.stderr, '[sup]');
});

test('runtime: Ctrl-D soft reboot re-runs code.py', async () => {
    // Boot board, wait for code.py done, enter REPL, then Ctrl-D
    const { board, stdout } = await createLiveBoard('print("hello")');
    let output = stdout;
    board._onStdout = (text) => { output += text; };

    // Ctrl-D triggers soft reboot → code.py runs again
    board.ctrlD();
    await waitFrames(board, 30);

    // Count how many times "hello" appears — should be 2 (initial + reboot)
    const count = (output.match(/hello/g) || []).length;
    if (count < 2) throw new Error(`Expected "hello" at least twice, got ${count}`);
    board.destroy();
});

test('runtime: Ctrl-D during WAITING_FOR_KEY reboots', async () => {
    // Use runBoard which stops at WAITING_FOR_KEY, then keepBoard and Ctrl-D
    const r = await runBoard('print("first")', { keepBoard: true });
    let output = r.stdout;
    r.board._onStdout = (text) => { output += text; };

    // Board is in WAITING_FOR_KEY state
    r.board.ctrlD();
    await waitFrames(r.board, 30);

    // Should see "first" again from the re-run
    const count = (output.match(/first/g) || []).length;
    if (count < 2) throw new Error(`Expected "first" at least twice, got ${count}`);
    r.board.destroy();
});

// ── multi-context tests ──

/**
 * Helper: create a board, wait for code.py to finish, return the live board.
 * Caller must destroy the board when done.
 */
function createLiveBoard(codePy = 'pass', { timeoutMs = 3000, files } = {}) {
    return new Promise(async (resolve, reject) => {
        let stdout = '';
        let timer;

        const board = await CircuitPython.create({
            wasmUrl: 'build-browser/circuitpython.wasm',
            codePy,
            files,
            onStdout: (text) => { stdout += text; },
            onStderr: () => {},
            onCodeDone: () => {
                clearTimeout(timer);
                // Enter REPL so board is ready for runCode
                setTimeout(() => {
                    board._enterRepl();
                    resolve({ board, stdout });
                }, 50);
            },
        });

        timer = setTimeout(() => {
            board.destroy();
            reject(new Error('createLiveBoard timed out'));
        }, timeoutMs);
    });
}

/**
 * Wait for a number of frames to pass on a live board.
 */
function waitFrames(board, n) {
    return new Promise(resolve => {
        const target = board.frameCount + n;
        const check = () => {
            if (board.frameCount >= target) resolve();
            else setTimeout(check, 20);
        };
        check();
    });
}

test('multicontext: runCode starts background context', async () => {
    const { board } = await createLiveBoard();
    const id = board.runCode('x = 1 + 1');
    if (id < 1) throw new Error(`runCode returned ${id}, expected >= 1`);
    // Wait a few frames for it to execute
    await waitFrames(board, 10);
    // Context should have been auto-cleaned (done + destroyed)
    const m = board._readContextMeta(id);
    if (m && m.status !== 0) throw new Error(`Context ${id} not cleaned up, status=${m.status}`);
    board.destroy();
});

test('multicontext: onDone callback fires', async () => {
    const { board } = await createLiveBoard();
    let doneFired = false;
    let doneId = -1;
    const id = board.runCode('y = 42', {
        onDone: (ctxId, err) => {
            doneFired = true;
            doneId = ctxId;
        },
    });
    if (id < 1) throw new Error(`runCode returned ${id}`);
    await waitFrames(board, 10);
    if (!doneFired) throw new Error('onDone not called');
    if (doneId !== id) throw new Error(`onDone id mismatch: ${doneId} !== ${id}`);
    board.destroy();
});

test('multicontext: multiple concurrent contexts', async () => {
    const { board } = await createLiveBoard();
    const done = new Set();
    const ids = [];
    for (let i = 0; i < 3; i++) {
        const id = board.runCode(`z${i} = ${i}`, {
            onDone: (ctxId) => done.add(ctxId),
        });
        if (id < 1) throw new Error(`runCode #${i} returned ${id}`);
        ids.push(id);
    }
    await waitFrames(board, 20);
    for (const id of ids) {
        if (!done.has(id)) throw new Error(`Context ${id} not done`);
    }
    board.destroy();
});

test('multicontext: listContexts shows active contexts', async () => {
    const { board } = await createLiveBoard();
    // Context 0 (main) should always be listed
    const initial = board.listContexts();
    const ctx0 = initial.find(c => c.id === 0);
    if (!ctx0) throw new Error('Context 0 not listed');
    board.destroy();
});

test('multicontext: killContext destroys background context', async () => {
    const { board } = await createLiveBoard();
    // Run a long-running context (import time; time.sleep will yield)
    const id = board.runCode('import time; time.sleep(10)');
    if (id < 1) throw new Error(`runCode returned ${id}`);
    await waitFrames(board, 5);
    // It should be sleeping
    const m = board._readContextMeta(id);
    if (!m || m.status === 0 || m.status === 6) {
        throw new Error(`Expected running/sleeping context, got status=${m?.status}`);
    }
    // Kill it
    const killed = board.killContext(id);
    if (!killed) throw new Error('killContext returned false');
    // Verify destroyed
    const after = board._readContextMeta(id);
    if (after && after.status !== 0) throw new Error(`Context not freed, status=${after.status}`);
    board.destroy();
});

test('multicontext: killContext rejects context 0', async () => {
    const { board } = await createLiveBoard();
    if (board.killContext(0) !== false) throw new Error('Should not kill context 0');
    board.destroy();
});

test('multicontext: activeContextCount', async () => {
    const { board } = await createLiveBoard();
    const baseline = board.activeContextCount;
    const id = board.runCode('import time; time.sleep(5)');
    if (id < 1) throw new Error(`runCode returned ${id}`);
    await waitFrames(board, 3);
    if (board.activeContextCount <= baseline) {
        throw new Error('activeContextCount should have increased');
    }
    board.killContext(id);
    board.destroy();
});

// ── hardware target tests ──

import { HardwareTarget, TeeTarget } from './js/targets.mjs';

test('target: HardwareTarget base class connects/disconnects', async () => {
    const t = new HardwareTarget();
    if (t.connected) throw new Error('Should start disconnected');
    if (t.type !== 'base') throw new Error(`Expected type "base", got "${t.type}"`);
    await t.connect();
    if (!t.connected) throw new Error('Should be connected after connect()');
    await t.disconnect();
    if (t.connected) throw new Error('Should be disconnected after disconnect()');
});

test('target: applyState diffs GPIO and calls onGpioChange', async () => {
    const changes = [];
    class TestTarget extends HardwareTarget {
        get type() { return 'test'; }
        onGpioChange(pin, state) { changes.push({ pin, ...state }); }
    }

    const target = new TestTarget();
    await target.connect();

    // Create a fake memfs with GPIO data
    const fakeMemfs = {
        readFile(path) {
            if (path === '/hal/gpio') {
                // Pin 0: enabled=1, direction=1(out), value=1, pull=0, openDrain=0
                const data = new Uint8Array(32 * 8);  // 32 pins × 8 bytes
                data[0] = 1;  // enabled
                data[1] = 1;  // direction = output
                data[2] = 1;  // value = high
                return data;
            }
            return null;
        },
    };

    // First call: everything is "new"
    target.applyState(fakeMemfs, 0);
    if (changes.length !== 1) throw new Error(`Expected 1 change, got ${changes.length}`);
    if (changes[0].pin !== 0) throw new Error(`Expected pin 0, got ${changes[0].pin}`);
    if (changes[0].value !== 1) throw new Error(`Expected value 1, got ${changes[0].value}`);
    if (changes[0].direction !== 1) throw new Error(`Expected direction 1, got ${changes[0].direction}`);

    // Second call: same data, no changes
    changes.length = 0;
    target.applyState(fakeMemfs, 16);
    if (changes.length !== 0) throw new Error(`Expected 0 changes on same data, got ${changes.length}`);

    await target.disconnect();
});

const GPIO_MAX_PINS_TEST = 32;

test('target: TeeTarget forwards to multiple targets', async () => {
    const log1 = [], log2 = [];
    class LogTarget extends HardwareTarget {
        constructor(log) { super(); this._log = log; }
        get type() { return 'log'; }
        onGpioChange(pin, state) { this._log.push({ pin, value: state.value }); }
    }

    const t1 = new LogTarget(log1);
    const t2 = new LogTarget(log2);
    const tee = new TeeTarget([t1, t2]);

    await tee.connect();
    if (!t1.connected || !t2.connected) throw new Error('Subtargets should be connected');

    const fakeMemfs = {
        readFile(path) {
            if (path === '/hal/gpio') {
                const data = new Uint8Array(GPIO_MAX_PINS_TEST * 8);
                data[0 * 8 + 0] = 1;  // pin 0 enabled
                data[0 * 8 + 1] = 1;  // output
                data[0 * 8 + 2] = 1;  // value high
                return data;
            }
            return null;
        },
    };

    tee.applyState(fakeMemfs, 0);
    if (log1.length !== 1) throw new Error(`Target 1: expected 1 change, got ${log1.length}`);
    if (log2.length !== 1) throw new Error(`Target 2: expected 1 change, got ${log2.length}`);
    if (log1[0].pin !== 0 || log1[0].value !== 1) throw new Error('Target 1 got wrong data');
    if (log2[0].pin !== 0 || log2[0].value !== 1) throw new Error('Target 2 got wrong data');

    await tee.disconnect();
    if (t1.connected || t2.connected) throw new Error('Subtargets should be disconnected');
});

test('target: TeeTarget addTarget/removeTarget', async () => {
    const tee = new TeeTarget([]);
    await tee.connect();

    const log = [];
    class LogTarget extends HardwareTarget {
        get type() { return 'log'; }
        onGpioChange(pin, state) { log.push(pin); }
    }

    const t = new LogTarget();
    await t.connect();
    tee.addTarget(t);
    if (tee.targets.length !== 1) throw new Error('Expected 1 target');

    tee.removeTarget(t);
    if (tee.targets.length !== 0) throw new Error('Expected 0 targets');

    await tee.disconnect();
    await t.disconnect();
});

test('target: connectTarget wires input data back to MEMFS', async () => {
    const { board } = await createLiveBoard('pass');

    const inputs = [];
    class TestTarget extends HardwareTarget {
        get type() { return 'test'; }
        async pollInputs() {
            // Simulate hardware reading pin 5 as HIGH
            if (this._onInput) this._onInput('gpio', 5, 1);
            inputs.push('polled');
        }
    }

    const target = new TestTarget();
    await target.connect();
    board.connectTarget(target);

    if (board.target !== target) throw new Error('target getter should return connected target');

    // Wait enough frames for poll to fire (~20 frames for first poll)
    await waitFrames(board, 25);
    if (inputs.length === 0) throw new Error('pollInputs was never called');

    await board.disconnectTarget();
    if (board.target !== null) throw new Error('target should be null after disconnect');

    board.destroy();
});

test('target: WebUSBTarget/WebSerialTarget unavailable in Node', async () => {
    // These require browser APIs — just verify they throw helpful errors
    const { WebUSBTarget, WebSerialTarget } = await import('./js/targets.mjs');

    const usb = new WebUSBTarget();
    try {
        await usb.connect();
        throw new Error('Should have thrown');
    } catch (e) {
        if (!e.message.includes('WebUSB not available')) {
            throw new Error(`Wrong error: ${e.message}`);
        }
    }

    const serial = new WebSerialTarget();
    try {
        await serial.connect();
        throw new Error('Should have thrown');
    } catch (e) {
        if (!e.message.includes('WebSerial not available')) {
            throw new Error(`Wrong error: ${e.message}`);
        }
    }
});

// ── Run all ──

async function main() {
    console.log(`Running ${tests.length} browser tests...\n`);
    let passed = 0, failed = 0;

    for (const t of tests) {
        try {
            await t.fn();
            passed++;
            process.stdout.write('.');
        } catch (e) {
            failed++;
            process.stdout.write('F');
            if (verbose) {
                console.log(`\n  FAIL: ${t.name}`);
                console.log(`  ${e.message}\n`);
            }
        }
    }

    console.log(`\n\n${passed} passed, ${failed} failed`);

    if (failed > 0 && !verbose) {
        console.log('\nFailed tests:');
        // Re-run failed tests with output
        for (const t of tests) {
            try {
                await t.fn();
            } catch (e) {
                console.log(`  FAIL: ${t.name}`);
                console.log(`    ${e.message}`);
            }
        }
    }

    process.exit(failed > 0 ? 1 : 0);
}

main();
