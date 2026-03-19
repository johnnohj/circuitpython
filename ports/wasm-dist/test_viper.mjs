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

// ---- T4: Native function (Python objects) ----
{
    const result = Module.vm.run(`
@micropython.native
def greet(name):
    return "Hello, " + name + "!"

print(greet("WASM"))
`);
    const stdout = (result.stdout || '').trim();
    assert(!result.error, `T4: native greet ran (error: ${result.error || 'none'})`);
    assert(stdout === 'Hello, WASM!', `T4: greet = "Hello, WASM!" (got ${stdout})`);
}

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
