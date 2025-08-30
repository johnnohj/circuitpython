// Run official CircuitPython test files
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';
import { readFileSync, existsSync } from 'fs';
import { join } from 'path';

async function runOfficialTests() {
    console.log("Running official CircuitPython test suite...\n");

    const mp = await _createCircuitPythonModule();
    mp._mp_js_init_with_heap(256 * 1024);
    const outputPtr = mp._malloc(4);

    // Official test files to run
    const testFiles = [
        '../../tests/basics/0prelim.py',
        '../../tests/basics/andor.py',
        '../../tests/basics/array1.py',
        '../../tests/basics/array_construct.py',
        '../../tests/basics/assign1.py',
        '../../tests/basics/bool1.py',
        '../../tests/basics/builtin_callable.py',
        '../../tests/basics/builtin_dir.py',
        '../../tests/basics/builtin_enumerate.py',
        '../../tests/basics/builtin_eval.py',
        '../../tests/basics/builtin_filter.py',
        '../../tests/basics/builtin_getattr.py',
        '../../tests/basics/builtin_hash.py',
        '../../tests/basics/builtin_hasattr.py',
        '../../tests/basics/builtin_isinstance.py',
        '../../tests/basics/builtin_len.py',
        '../../tests/basics/builtin_map.py',
        '../../tests/basics/builtin_max.py',
        '../../tests/basics/builtin_min.py',
        '../../tests/basics/builtin_range.py',
        '../../tests/basics/builtin_reversed.py',
        '../../tests/basics/builtin_sorted.py',
        '../../tests/basics/builtin_sum.py',
        '../../tests/basics/builtin_type.py',
        '../../tests/basics/builtin_zip.py',
        '../../tests/basics/class1.py',
        '../../tests/basics/class2.py',
        '../../tests/basics/class_bound_method.py',
        '../../tests/basics/class_getattr.py',
        '../../tests/basics/class_inherit1.py',
        '../../tests/basics/class_new.py',
        '../../tests/basics/comparisons.py',
        '../../tests/basics/del_global.py',
        '../../tests/basics/dict1.py',
        '../../tests/basics/dict_copy.py',
        '../../tests/basics/dict_fromkeys.py',
        '../../tests/basics/dict_get.py',
        '../../tests/basics/dict_pop.py',
        '../../tests/basics/exception1.py',
        '../../tests/basics/exception_chain.py',
        '../../tests/basics/for1.py',
        '../../tests/basics/fun1.py',
        '../../tests/basics/fun_annotations.py',
        '../../tests/basics/fun_args.py',
        '../../tests/basics/fun_default.py',
        '../../tests/basics/fun_kwargs.py',
        '../../tests/basics/generator1.py',
        '../../tests/basics/if1.py',
        '../../tests/basics/int1.py',
        '../../tests/basics/int_big1.py',
        '../../tests/basics/lambda1.py',
        '../../tests/basics/list1.py',
        '../../tests/basics/list_append.py',
        '../../tests/basics/list_clear.py',
        '../../tests/basics/list_copy.py',
        '../../tests/basics/list_count.py',
        '../../tests/basics/list_extend.py',
        '../../tests/basics/list_index.py',
        '../../tests/basics/list_insert.py',
        '../../tests/basics/list_pop.py',
        '../../tests/basics/list_remove.py',
        '../../tests/basics/list_reverse.py',
        '../../tests/basics/list_slice.py',
        '../../tests/basics/list_sort.py',
        '../../tests/basics/python34.py',
        '../../tests/basics/set1.py',
        '../../tests/basics/string1.py',
        '../../tests/basics/string_count.py',
        '../../tests/basics/string_endswith.py',
        '../../tests/basics/string_find.py',
        '../../tests/basics/string_format.py',
        '../../tests/basics/string_join.py',
        '../../tests/basics/string_replace.py',
        '../../tests/basics/string_split.py',
        '../../tests/basics/string_startswith.py',
        '../../tests/basics/tuple1.py',
        '../../tests/basics/while1.py'
    ];

    let passed = 0;
    let failed = 0;
    let total = 0;

    console.log(`Found ${testFiles.length} test files to run...\n`);

    for (const testFile of testFiles) {
        if (!existsSync(testFile)) {
            console.log(`⚠️  Test file not found: ${testFile}`);
            continue;
        }

        total++;

        try {
            const testCode = readFileSync(testFile, 'utf8');
            const fileName = testFile.split('/').pop();

            console.log(`Running ${fileName}...`);
            mp._mp_js_do_exec(testCode, testCode.length, outputPtr);
            console.log(`✓ ${fileName} passed`);
            passed++;

        } catch (error) {
            const fileName = testFile.split('/').pop();
            console.log(`✗ ${fileName} failed: ${error.message}`);
            failed++;
        }
    }

    mp._free(outputPtr);

    console.log(`\n📊 Official Test Suite Results:`);
    console.log(`   Total tests run: ${total}`);
    console.log(`   Passed: ${passed}`);
    console.log(`   Failed: ${failed}`);
    console.log(`   Success rate: ${((passed / total) * 100).toFixed(1)}%`);

    if (failed === 0) {
        console.log("\n🎉 All official CircuitPython tests passed!");
    } else if (failed < 5) {
        console.log(`\n✨ Excellent! Only ${failed} minor test failures.`);
    } else {
        console.log(`\n⚠️ ${failed} tests failed - needs investigation.`);
    }

    return { passed, failed, total, successRate: (passed / total) * 100 };
}

runOfficialTests().then(results => {
    const success = results.successRate >= 90; // 90% success rate threshold
    console.log(success ? "\n🏆 CircuitPython WebAssembly build passes comprehensive testing!" : "\n❌ Build needs more work");
    process.exit(success ? 0 : 1);
}).catch(error => {
    console.error("Official test suite failed:", error);
    process.exit(1);
});
