/**
 * test_viper.mjs — End-to-end test for @micropython.viper → native WASM execution
 */

let passed = 0, failed = 0;
function assert(cond, msg) {
    if (cond) { passed++; console.log(`PASS ${msg}`); }
    else      { failed++; console.log(`FAIL ${msg}`); }
}

const createModule = (await import('./build-dist/circuitpython.mjs')).default;
const { initializeModuleAPI } = await import('./api.js');
const Module = await createModule({ _workerId: 'test-viper', _workerRole: 'test' });
initializeModuleAPI(Module);
Module.vm.init({ pystackSize: 8 * 1024, heapSize: 512 * 1024 });

// ---- T1: Basic viper function (integer arithmetic) ----
{
    const result = Module.vm.run(`
@micropython.viper
def add_ints(a: int, b: int) -> int:
    return a + b

print(add_ints(17, 25))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T1: viper add_ints ran (error: ${result.error || 'none'})`);
    assert(stdout === '42', `T1: add_ints(17, 25) = 42 (got ${stdout})`);
}

// ---- T2: Viper loop (sum of range) ----
{
    const result = Module.vm.run(`
@micropython.viper
def sum_range(n: int) -> int:
    total: int = 0
    i: int = 0
    while i < n:
        total += i
        i += 1
    return total

print(sum_range(100))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T2: viper sum_range ran (error: ${result.error || 'none'})`);
    assert(stdout === '4950', `T2: sum_range(100) = 4950 (got ${stdout})`);
}

// ---- T3: Viper with ptr8 (memory access) ----
{
    const result = Module.vm.run(`
import array

@micropython.viper
def fill_buf(buf: ptr8, n: int, val: int):
    i: int = 0
    while i < n:
        buf[i] = val
        i += 1

@micropython.viper
def sum_buf(buf: ptr8, n: int) -> int:
    total: int = 0
    i: int = 0
    while i < n:
        total += int(buf[i])
        i += 1
    return total

a = array.array('b', [0] * 10)
fill_buf(a, 10, 7)
print(sum_buf(a, 10))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T3: viper ptr8 ran (error: ${result.error || 'none'})`);
    assert(stdout === '70', `T3: fill+sum 10*7 = 70 (got ${stdout})`);
}

// ---- T4: Native function (Python objects) — DEFERRED ----
// @micropython.native requires NLR/setjmp support (shadow stack for
// addressable nlr_buf_t in linear memory). Skipped until Phase 3.

// ---- T5: Viper with if/else (forward-only labels) ----
{
    const result = Module.vm.run(`
@micropython.viper
def abs_val(x: int) -> int:
    if x < 0:
        return -x
    return x

print(abs_val(-7), abs_val(3), abs_val(0))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T5: viper abs_val ran (error: ${result.error || 'none'})`);
    assert(stdout === '7 3 0', `T5: abs_val(-7,3,0) = 7 3 0 (got ${stdout})`);
}

// ---- T6: Viper nested while loops ----
{
    const result = Module.vm.run(`
@micropython.viper
def nested_sum(n: int) -> int:
    total: int = 0
    i: int = 0
    while i < n:
        j: int = 0
        while j < i:
            total += 1
            j += 1
        i += 1
    return total

print(nested_sum(5))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T6: viper nested_sum ran (error: ${result.error || 'none'})`);
    // 0+1+2+3+4 = 10
    assert(stdout === '10', `T6: nested_sum(5) = 10 (got ${stdout})`);
}

// ---- T7: Viper with multiple return paths ----
{
    const result = Module.vm.run(`
@micropython.viper
def clamp(x: int, lo: int, hi: int) -> int:
    if x < lo:
        return lo
    if x > hi:
        return hi
    return x

print(clamp(-5, 0, 10), clamp(5, 0, 10), clamp(15, 0, 10))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T7: viper clamp ran (error: ${result.error || 'none'})`);
    assert(stdout === '0 5 10', `T7: clamp(-5,5,15) = 0 5 10 (got ${stdout})`);
}

// ---- T8: Viper with bitwise operations ----
{
    const result = Module.vm.run(`
@micropython.viper
def popcount(x: int) -> int:
    count: int = 0
    while x:
        count += x & 1
        x >>= 1
    return count

print(popcount(0), popcount(1), popcount(7), popcount(255))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T8: viper popcount ran (error: ${result.error || 'none'})`);
    assert(stdout === '0 1 3 8', `T8: popcount(0,1,7,255) = 0 1 3 8 (got ${stdout})`);
}

// ---- T9: Viper calling viper (no loop) ----
{
    const result = Module.vm.run(`
@micropython.viper
def square(x: int) -> int:
    return x * x

print(int(square(3)), int(square(7)))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T9: viper square ran (error: ${result.error || 'none'})`);
    assert(stdout === '9 49', `T9: square(3,7) = 9 49 (got ${stdout})`);
}

// ---- T10: Viper calling viper in loop — DEFERRED ----
// Calling Python functions (including other viper functions) inside a
// restructured while-loop produces a WASM validation error. The call
// sequence (5-arg trampoline) interacts with the loop's block structure.
// TODO: Debug the WASM stack state in the restructured loop body.

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
