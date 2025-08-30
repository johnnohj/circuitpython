/*
 * CircuitPython CLI API - Simple Node.js REPL Interface
 *
 * This file gets appended to the generated circuitpython.mjs during build.
 * It provides a minimal CLI interface without conflicting with Emscripten's generated code.
 */

// Add helper functions to Module for programmatic use
if (typeof Module !== 'undefined') {
    Module.runPython = Module.runPython || function(code) {
        const len = code.length;
        const buf = Module._malloc(len + 1);
        Module.stringToUTF8(code, buf, len + 1);
        const result_ptr = Module._malloc(3 * 4);

        try {
            Module._mp_js_do_exec(buf, len, result_ptr);
        } finally {
            Module._free(buf);
            Module._free(result_ptr);
        }
    };

    Module.initCircuitPython = Module.initCircuitPython || function(heapSize = 1024 * 1024) {
        Module._mp_js_init_with_heap(heapSize);
    };
        // Check if Node is running (equivalent to ENVIRONMENT_IS_NODE).


    if (
        typeof process === "object" &&
        typeof process.versions === "object" &&
        typeof process.versions.node === "string"
    ) {
        // Check if this module is run from the command line via `node micropython.mjs`.
        //
        // See https://stackoverflow.com/questions/6398196/detect-if-called-through-require-or-directly-by-command-line/66309132#66309132
        //
        // Note:
        // - `resolve()` is used to handle symlinks
        // - `includes()` is used to handle cases where the file extension was omitted when passed to node

        if ( process.argv.length > 1 ) {
            const path = await import( "path" )
            const url = await import( "url" )

            const pathToThisFile = path.resolve( url.fileURLToPath( import.meta.url ) )
            const pathPassedToNode = path.resolve( process.argv[ 1 ] )
            const isThisFileBeingRunViaCLI =
                pathToThisFile.includes( pathPassedToNode )


            if ( isThisFileBeingRunViaCLI ) {
                runCircuitPythonCLI()
            }
        }
    }
}

// CLI function for Node.js
export async function runCircuitPythonCLI () {
    const fs = await import( "fs" )
    let heapSize = 2 * 1024 * 1024 // 2MB default
    let scriptFile = null
    let replMode = true

    for ( let i = 2; i < process.argv.length; i++ ) {
        if ( process.argv[ i ] === "-X" && i < process.argv.length - 1 ) {
            if ( process.argv[ i + 1 ].includes( "heapsize=" ) ) {
                heapSize = parseInt( process.argv[ i + 1 ].split( "heapsize=" )[ 1 ] )
                const suffix = process.argv[ i + 1 ].substr( -1 ).toLowerCase()
                if ( suffix === "k" ) {
                    heapSize *= 1024
                } else if ( suffix === "m" ) {
                    heapSize *= 1024 * 1024
                }
                ++i
            }
        } else {
            contents += fs.readFileSync( process.argv[ i ], "utf8" )
            replMode = false
        }
    }

    if ( process.stdin.isTTY === false ) {
        contents = fs.readFileSync( 0, "utf8" )
        replMode = false
    }


    // Load CircuitPython using the simple API
    const cp = await loadCircuitPython( {
        heapsize: heapSize,
        stdout: ( line ) => process.stdout.write( line ),
        linebuffer: false,
    } )

    if ( replMode ) {

        cp.replInit()

        process.stdin.setRawMode( true )
        process.stdin.on( 'data', async ( data ) => {
            for ( let i = 0; i < data.length; i++ ) {
                const result = await cp.replProcessCharAsync( data[ i ] )
                if ( result ) {
                    process.exit( 0 )
                }
            }
        } )
    } else if ( contents.endsWith( "asyncio.run(main())\n" ) )
        // If the script to run ends with a running of the asyncio main loop, then inject
        // a simple `asyncio.run` hook that starts the main task.  This is primarily to
        // support running the standard asyncio tests.
        {
            const asyncio = cp.pyimport( "asyncio" )
            asyncio.run = async ( task ) => {
                await asyncio.create_task( task )
            }
        } try {
            cp.runPython( contents )
        }

        catch ( error ) {
            if ( error.name === "PythonError" ) {
                if ( error.type === "SystemExit" ) {
                    // SystemExit, this is a valid exception to successfully end a script.
                } else {
                    // An unhandled Python exception, print in out.
                    console.error( error.message )
                }
            } else {
                // A non-Python exception.  Re-raise it.
                throw error
            }
        }
    }




