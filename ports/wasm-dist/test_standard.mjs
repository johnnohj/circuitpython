#!/usr/bin/env node
/**
 * test_standard.mjs — Automated test suite for the WASM standard variant.
 *
 * Runs CircuitPython REPL expressions via Node.js WASI and validates output.
 * Each test pipes Python code to stdin and checks stdout for expected results.
 *
 * Usage:
 *   node test_standard.mjs                    # run all tests
 *   node test_standard.mjs --filter math      # run tests matching "math"
 *   node test_standard.mjs --verbose          # show full output
 *
 * Exit code: 0 if all pass, 1 if any fail.
 */

import { spawn } from 'node:child_process';

// ── Options ──

const args = process.argv.slice(2);
const verbose = args.includes('--verbose');
const filterIdx = args.indexOf('--filter');
const filter = filterIdx >= 0 ? args[filterIdx + 1] : null;

// ── Test runner ──

/**
 * Run Python code in a fresh WASI instance by spawning a child process.
 * Pipes code to stdin, captures stdout/stderr.
 */
function runRepl(code, timeoutMs = 5000) {
    return new Promise((resolve) => {
        const child = spawn('node', [
            '--experimental-wasi-unstable-preview1',
            'test_node.mjs',
        ], {
            stdio: ['pipe', 'pipe', 'pipe'],
            env: { ...process.env, NODE_NO_WARNINGS: '1' },
        });

        let stdout = '';
        let stderr = '';
        let done = false;

        child.stdout.on('data', (d) => { stdout += d.toString(); });
        child.stderr.on('data', (d) => { stderr += d.toString(); });

        const timer = setTimeout(() => {
            if (!done) {
                done = true;
                child.kill('SIGTERM');
                resolve({ stdout, stderr, exitCode: -1, timedOut: true });
            }
        }, timeoutMs);

        child.on('close', (exitCode) => {
            if (!done) {
                done = true;
                clearTimeout(timer);
                resolve({ stdout, stderr, exitCode: exitCode || 0 });
            }
        });

        // Write code to stdin and close it (EOF → Ctrl-D → exit)
        child.stdin.write(code);
        child.stdin.end();
    });
}

/**
 * Run Python code via the board variant (circuitpython.mjs).
 */
function runBoard(code, timeoutMs = 8000) {
    return new Promise((resolve) => {
        const child = spawn('node', [
            'test_node_board.mjs', '--code', code, '--exit',
        ], {
            stdio: ['pipe', 'pipe', 'pipe'],
            env: { ...process.env, NODE_NO_WARNINGS: '1' },
        });

        let stdout = '';
        let stderr = '';
        let done = false;

        child.stdout.on('data', (d) => { stdout += d.toString(); });
        child.stderr.on('data', (d) => { stderr += d.toString(); });

        const timer = setTimeout(() => {
            if (!done) {
                done = true;
                child.kill('SIGTERM');
                resolve({ stdout, stderr, exitCode: -1, timedOut: true });
            }
        }, timeoutMs);

        child.on('close', (exitCode) => {
            if (!done) {
                done = true;
                clearTimeout(timer);
                resolve({ stdout, stderr, exitCode: exitCode || 0 });
            }
        });

        child.stdin.end();
    });
}

// ── Test definitions ──

const tests = [];

function test(name, fn) {
    tests.push({ name, fn });
}

function assertContains(haystack, needle, msg) {
    if (!haystack.includes(needle)) {
        throw new Error(
            `${msg || 'assertContains failed'}\n  expected to contain: ${JSON.stringify(needle)}\n  in: ${JSON.stringify(haystack.slice(0, 300))}`
        );
    }
}

function assertNotContains(haystack, needle, msg) {
    if (haystack.includes(needle)) {
        throw new Error(
            `${msg || 'assertNotContains failed'}\n  expected NOT to contain: ${JSON.stringify(needle)}\n  in: ${JSON.stringify(haystack.slice(0, 300))}`
        );
    }
}

function assertNoTimeout(result) {
    if (result.timedOut) {
        throw new Error(`Timed out. stdout so far: ${JSON.stringify(result.stdout.slice(0, 200))}`);
    }
}

// ── REPL basics ──

test('repl: print integer', async () => {
    const r = await runRepl('print(42)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '42');
});

test('repl: expression result', async () => {
    const r = await runRepl('1+1\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '2\r\n');
});

test('repl: string print', async () => {
    const r = await runRepl('print("hello world")\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'hello world');
});

test('repl: multi-line assignment', async () => {
    const r = await runRepl('x = 10\nprint(x * 3)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '30');
});

test('repl: None not printed for assignment', async () => {
    const r = await runRepl('x = 5\n');
    assertNoTimeout(r);
    assertNotContains(r.stdout, 'None');
});

// ── Built-in types ──

test('types: list', async () => {
    const r = await runRepl('print([1,2,3])\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '[1, 2, 3]');
});

test('types: dict', async () => {
    const r = await runRepl("print({'a': 1})\n");
    assertNoTimeout(r);
    assertContains(r.stdout, "'a': 1");
});

test('types: tuple', async () => {
    const r = await runRepl('print((1, 2))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '(1, 2)');
});

test('types: bytes', async () => {
    const r = await runRepl('print(b"abc")\n');
    assertNoTimeout(r);
    assertContains(r.stdout, "b'abc'");
});

test('types: bool', async () => {
    const r = await runRepl('print(True, False)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'True False');
});

// ── Math ──

test('math: integer arithmetic', async () => {
    const r = await runRepl('print(2 ** 10)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '1024');
});

test('math: float', async () => {
    const r = await runRepl('print(3.14 * 2)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '6.28');
});

test('math: divmod', async () => {
    const r = await runRepl('print(divmod(17, 5))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '(3, 2)');
});

test('math: import math', async () => {
    const r = await runRepl('import math\nprint(math.pi)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '3.14159');
});

// ── String operations ──

test('string: format', async () => {
    const r = await runRepl('print("x={}, y={}".format(1, 2))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'x=1, y=2');
});

test('string: f-string', async () => {
    const r = await runRepl('x = 42\nprint(f"val={x}")\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'val=42');
});

test('string: split/join', async () => {
    const r = await runRepl('print("-".join("abc"))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'a-b-c');
});

// ── Control flow ──

test('control: ternary', async () => {
    const r = await runRepl('print("yes" if 3 > 2 else "no")\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'yes');
});

test('control: list comprehension', async () => {
    const r = await runRepl('print([x*x for x in range(5)])\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '[0, 1, 4, 9, 16]');
});

test('control: sum + range', async () => {
    const r = await runRepl('print(sum(range(10)))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '45');
});

// ── Modules ──

test('module: json', async () => {
    const r = await runRepl('import json\nprint(json.dumps({"a": [1,2]}))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '"a"');
});

test('module: os.uname', async () => {
    const r = await runRepl('import os\nprint(os.uname().sysname)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'wasi');
});

test('module: struct', async () => {
    const r = await runRepl('import struct\nprint(len(struct.pack("HH", 1, 2)))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '4');
});

test('module: collections.OrderedDict', async () => {
    const r = await runRepl('from collections import OrderedDict\nd = OrderedDict()\nd["b"] = 2\nd["a"] = 1\nprint(list(d.keys()))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, "['b', 'a']");
});

test('module: re', async () => {
    const r = await runRepl('import re\nm = re.match(r"(\\d+)", "abc123")\nprint(m.group(1))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '123');
});

test('module: random', async () => {
    const r = await runRepl('import random\nrandom.seed(42)\nprint(type(random.randint(0, 100)))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, "<class 'int'>");
});

test('module: binascii', async () => {
    const r = await runRepl('import binascii\nprint(binascii.hexlify(b"\\x01\\x02\\x03"))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '010203');
});

// ── Newly enabled modules ──

test('module: warnings', async () => {
    const r = await runRepl('import warnings\nprint(type(warnings.warn))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'function');
});

test('module: getpass', async () => {
    const r = await runRepl('import getpass\nprint(type(getpass))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'module');
});

test('module: aesio', async () => {
    const r = await runRepl('import aesio\nprint(type(aesio.AES))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'type');
});

test('module: locale', async () => {
    const r = await runRepl('import locale\nprint(type(locale))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'module');
});

test('module: adafruit_pixelbuf', async () => {
    const r = await runRepl('import adafruit_pixelbuf\nprint(type(adafruit_pixelbuf.PixelBuf))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'type');
});

test('board: vectorio', async () => {
    const r = await runBoard('import vectorio\nprint(type(vectorio.Circle))');
    assertNoTimeout(r);
    assertContains(r.stdout, 'type');
});

test('board: bitmaptools', async () => {
    const r = await runBoard('import bitmaptools\nprint(type(bitmaptools))');
    assertNoTimeout(r);
    assertContains(r.stdout, 'module');
});

// ── Error handling ──

test('error: NameError', async () => {
    const r = await runRepl('undefined_var\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'NameError');
});

test('error: TypeError', async () => {
    const r = await runRepl('"a" + 1\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'TypeError');
});

test('error: ZeroDivisionError', async () => {
    const r = await runRepl('1/0\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'ZeroDivisionError');
});

test('error: SyntaxError', async () => {
    const r = await runRepl('def\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'SyntaxError');
});

// ── Functions ──

test('function: lambda', async () => {
    const r = await runRepl('f = lambda x: x + 1\nprint(f(41))\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '42');
});

// ── CircuitPython specifics ──

test('circuitpython: board module (browser only)', async () => {
    // board module requires common-hal, only in browser variant.
    // standard variant should give ImportError.
    const r = await runRepl('import board\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'ImportError');
});

test('circuitpython: sys.implementation', async () => {
    const r = await runRepl('import sys\nprint(sys.implementation.name)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, 'circuitpython');
});

test('circuitpython: sys.path', async () => {
    const r = await runRepl('import sys\nprint(sys.path)\n');
    assertNoTimeout(r);
    assertContains(r.stdout, '.frozen');
});

// ── jsffi (board variant only — browser variant has JS imports) ──

test('jsffi: import and global_this type', async () => {
    const r = await runBoard('import jsffi\nprint(type(jsffi.global_this))');
    assertNoTimeout(r);
    assertContains(r.stdout, "JsProxy");
});

test('jsffi: Math.sqrt', async () => {
    const r = await runBoard('import jsffi\nprint(jsffi.global_this.Math.sqrt(144))');
    assertNoTimeout(r);
    assertContains(r.stdout, '12');
});

test('jsffi: Math.PI', async () => {
    const r = await runBoard('import jsffi\nprint(jsffi.global_this.Math.PI)');
    assertNoTimeout(r);
    assertContains(r.stdout, '3.14159');
});

test('jsffi: parseInt', async () => {
    const r = await runBoard('import jsffi\nprint(jsffi.global_this.parseInt("0xff", 16))');
    assertNoTimeout(r);
    assertContains(r.stdout, '255');
});

test('jsffi: JSON.parse attribute access', async () => {
    const r = await runBoard('import jsffi\nobj = jsffi.global_this.JSON.parse(\'{"a": 42}\')\nprint(obj.a)');
    assertNoTimeout(r);
    assertContains(r.stdout, '42');
});

test('jsffi: Array.length', async () => {
    const r = await runBoard('import jsffi\narr = jsffi.global_this.Array(1,2,3)\nprint(arr.length)');
    assertNoTimeout(r);
    assertContains(r.stdout, '3');
});

test('jsffi: shorthand global access', async () => {
    const r = await runBoard('import jsffi\nprint(jsffi.Math.floor(3.7))');
    assertNoTimeout(r);
    assertContains(r.stdout, '3');
});

test('jsffi: create_proxy callback', async () => {
    const r = await runBoard('import jsffi\ndef double(x):\n    return x * 2\nproxy = jsffi.create_proxy(double)\njsffi.global_this.myFn = proxy\nprint(jsffi.global_this.myFn(21))');
    assertNoTimeout(r);
    assertContains(r.stdout, '42');
});

test('jsffi: store and load string', async () => {
    const r = await runBoard('import jsffi\njsffi.global_this.testVal = "hello"\nprint(jsffi.global_this.testVal)');
    assertNoTimeout(r);
    assertContains(r.stdout, 'hello');
});

test('jsffi: mem_info returns tuple', async () => {
    const r = await runBoard('import jsffi\ninfo = jsffi.mem_info()\nprint(len(info))');
    assertNoTimeout(r);
    assertContains(r.stdout, '5');
});

// ── Board variant (circuitpython.mjs) ──

test('board: code.py execution', async () => {
    const r = await runBoard('print("from code.py")');
    assertNoTimeout(r);
    assertContains(r.stdout, 'from code.py');
});

test('board: expression via code.py', async () => {
    const r = await runBoard('import sys\nprint(sys.implementation.name)');
    assertNoTimeout(r);
    assertContains(r.stdout, 'circuitpython');
});

// ── Run tests ──

async function main() {
    const filtered = filter
        ? tests.filter(t => t.name.toLowerCase().includes(filter.toLowerCase()))
        : tests;

    let passed = 0;
    let failed = 0;
    const failures = [];

    console.log(`Running ${filtered.length} tests...\n`);

    for (const t of filtered) {
        const start = performance.now();
        try {
            await t.fn();
            const ms = (performance.now() - start).toFixed(0);
            passed++;
            if (verbose) {
                console.log(`  \u2713 ${t.name} (${ms}ms)`);
            } else {
                process.stdout.write('.');
            }
        } catch (e) {
            const ms = (performance.now() - start).toFixed(0);
            failed++;
            failures.push({ name: t.name, error: e });
            if (verbose) {
                console.log(`  \u2717 ${t.name} (${ms}ms)`);
                console.log(`    ${e.message.split('\n').join('\n    ')}`);
            } else {
                process.stdout.write('F');
            }
        }
    }

    if (!verbose) console.log();
    console.log(`\n${passed} passed, ${failed} failed`);

    if (failures.length > 0) {
        console.log('\n-- Failures --\n');
        for (const f of failures) {
            console.log(`  ${f.name}:`);
            console.log(`    ${f.error.message.split('\n').join('\n    ')}`);
            console.log();
        }
    }

    process.exit(failed > 0 ? 1 : 0);
}

main();
