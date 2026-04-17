CircuitPython WASM port
=====================

The "wasm" (or "wasi") port runs in standard browser environments, like Chrome, Firefox, and Safari, as well as Node.js runtimes. This port is offered for rapid prototyping, general testing, and experimentation with the primary intention to allow users to see whether some code 'works' even when they happen not to have the hardware at hand.

The goal of the wasm port is to provide a faithful simulation of a generic CircuitPython board in the browser; users should be able to run code using both this port and real hardware without needing to make edits. Differences between browsers, user hardware, and this port's reliance upon a JavaScript runtime mean there is no guarantee execution will match exactly the behavior found on real hardware.

For a fuller discussion of features and project goals, please see `DEV_NOTES.md`

Building
--------

### Dependencies

To build the wasm port locally then you will need:

* git command line executable
* wasi-sdk
* Python 3.x

To build the default "standard" variant and configuration, then you will also
need:

* `pkg-config` tool
* `libffi` library and headers

On Debian/Ubuntu/Mint and related Linux distros, you can install all these
dependencies with a command like:

```
# apt install build-essential git python3 pkg-config libffi-dev
```

(See below for steps to build either a standalone or minimal MicroPython
executable that doesn't require system `libffi` or `pkg-config`.)

### Default build steps

To set up the environment for building (not needed every time), starting from
the top-level CircuitPython directory:

    $ cd ports/wasm
    $ make -C ../../mpy-cross
    $ make submodules

The `mpy-cross` step builds the [MicroPython
cross-compiler](https://github.com/micropython/micropython/?tab=readme-ov-file#the-micropython-cross-compiler-mpy-cross).
The `make submodules` step can be skipped if you didn't clone the CircuitPython
source from git.

Next, to build the actual executable (still in the `ports/wasm` directory):

    $ make

Then to give it a try:

    $ node run ./build-standard/circuitpython
    >>> list(5 * x + y for x in range(10) for y in [4, 2, 1])

Use `CTRL-D` (i.e. EOF) to exit the shell.

Learn about command-line options (in particular, how to increase heap size
which may be needed for larger applications):

    $ node run ./build-standard/circuitpython -h

To run the complete testsuite, use:

    $ make test

The wasm port comes with a built-in package manager called `fwip`, e.g.:

    $ node run ./build-standard/circuitpython -m fwip install hmac

or

    $ node run ./build-standard/circuitpython
    >>> import fwip
    >>> fwip.install("hmac")

`fwip` uses `fetch()` to import libraries from the bundle repo into the virtual board's `CIRCUITPY/lib` directory. In web browsers, this is persistent Virtual Filesystem storage backed by an Indexed Database. In Node.js, the board's directory is created within the same parent directory of the binary itself. Browse available modules at
[Adafruit CircuitPython Bundle](https://github.com/adafruit/Adafruit_CircuitPython_Bundle).

### Browser Variant

The "standard" variant of CircuitPython is the default. It enables most features and is intended largely for testing in a Node.js runtime. To instead build the
"browser" variant, which adds several modules and features for use in a browser environment:

    $ cd ports/wasm
    $ make submodules
    $ make VARIANT=browser

The binary will be built at `build-browser/circuitpython`.

### Other dependencies

To actually enable/disable use of dependencies, edit the
`ports/wasm/mpconfigport.mk` file, which has inline descriptions of the
options. For example, to build the SSL module, `MICROPY_PY_SSL` should be
set to 1.

### Debug Symbols

By default, builds are stripped of symbols and debug information to save size.

To build a debuggable version of the port, there are two options:

1. Run `make [other arguments] DEBUG=1`. Note setting `DEBUG` also reduces the
   optimisation level and enables assertions, so it's not a good option for
   builds that also want the best performance.
2. Run `make [other arguments] STRIP=`. Note that the value of `STRIP` is
   empty. This will skip the build step that strips symbols and debug
   information, but changes nothing else in the build configuration.

### Optimisation Level

The default compiler optimisation level is -Os, or -Og if `DEBUG=1` is set.

Setting the variable `COPT` will explicitly set the optimisation level. For
example `make [other arguments] COPT=-O0 DEBUG=1` will build a binary with no
optimisations, assertions enabled, and debug symbols.
