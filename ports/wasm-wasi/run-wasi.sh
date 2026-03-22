#!/bin/bash
# Wrapper to run WASI CircuitPython via Node.js, compatible with run-tests.py
# Usage: ./run-wasi.sh [-X emit=bytecode] test_file.py
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WASM="$SCRIPT_DIR/build-standard/circuitpython.wasm"

# Skip -X flags (not supported by WASI port)
while [[ "$1" == -X ]]; do shift 2; done

TEST_FILE="$1"
if [ -z "$TEST_FILE" ]; then
    echo "Usage: $0 test_file.py" >&2
    exit 1
fi

# Resolve to absolute path
TEST_FILE="$(cd "$(dirname "$TEST_FILE")" && pwd)/$(basename "$TEST_FILE")"
TEST_DIR="$(dirname "$TEST_FILE")"

# Find the tests root (parent of basics/, io/, etc.) for a single preopen
# that covers both the test directory and testlib/
TESTS_ROOT="$TEST_DIR"
if [ -d "$TEST_DIR/../testlib" ]; then
    TESTS_ROOT="$(cd "$TEST_DIR/.." && pwd)"
fi

# Single broad preopen covers test dir + testlib + extmod subdirs.
# '.' maps to the test's own directory for relative path opens (data/file1).
PREOPENS_JSON="{\".\": \"${TEST_DIR}\", \"${TESTS_ROOT}\": \"${TESTS_ROOT}\", \"/tmp\": \"/tmp\"}"

# Pass MICROPYPATH as --micropypath arg instead of env var
MPYPATH_ARG="${MICROPYPATH:-}"

exec node --experimental-wasi-unstable-preview1 -e "
const fs = require('fs');
const { WASI } = require('wasi');
const preopens = JSON.parse(process.argv[2]);
const mpypath = process.argv[4] || '';
const args = ['circuitpython'];
if (mpypath) args.push('--micropypath', mpypath);
args.push(process.argv[1]);
const wasi = new WASI({
  version: 'preview1',
  args: args,
  preopens: preopens
});
const wasm = fs.readFileSync(process.argv[3]);
WebAssembly.compile(wasm).then(mod => {
  const instance = new WebAssembly.Instance(mod, wasi.getImportObject());
  wasi.start(instance);
}).catch(e => {
  process.stderr.write(e.message + '\n');
  process.exit(1);
});
" "$TEST_FILE" "$PREOPENS_JSON" "$WASM" "$MPYPATH_ARG" 2>/dev/null
