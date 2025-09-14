// Module resolution for both browser and Node.js platforms  
// Phase 2: Dynamic Module System implementation
//
// IMPORT RESOLUTION ORDER (Browser):
// 1. Browser Local Storage (user's saved modules)
// 2. Local Filesystem Access API (if granted by web-editor)
// 3. USB Storage Access (File System Access API)  
// 4. Local HTTP paths (./modules/, ./lib/, etc.)
// 5. GitHub Raw API (individual Adafruit libraries fallback)

// Platform detection
const isBrowser = typeof window !== 'undefined';
const isNode = typeof process !== 'undefined' && process.versions && process.versions.node;

// Module cache for loaded source code
const moduleCache = new Map();

/**
 * Platform-agnostic module fetching
 * @param {string} moduleName - Name of the module to load
 * @returns {Promise<string>} - Module source code
 */
async function fetchModuleSource(moduleName) {
    // Check cache first
    if (moduleCache.has(moduleName)) {
        return moduleCache.get(moduleName);
    }
    
    let source;
    
    if (isBrowser) {
        source = await fetchModuleBrowser(moduleName);
    } else if (isNode) {
        source = await fetchModuleNode(moduleName);
    } else {
        throw new Error('Unsupported platform for module loading');
    }
    
    // Cache the source
    if (source) {
        moduleCache.set(moduleName, source);
    }
    
    return source;
}

/**
 * Browser-based module fetching
 * @param {string} moduleName 
 * @returns {Promise<string>}
 */
async function fetchModuleBrowser(moduleName) {
    // 1. Try Browser Local Storage first (user's saved modules)
    try {
        const localStorageModule = await fetchFromLocalStorage(moduleName);
        if (localStorageModule) return localStorageModule;
    } catch (e) { /* Continue */ }

    // 2. Try File System Access API (local filesystem, USB) 
    try {
        const fileSystemModule = await fetchFromFileSystemAPI(moduleName);
        if (fileSystemModule) return fileSystemModule;
    } catch (e) { /* Continue */ }

    // 3. Try local HTTP paths (served by web server)
    const localPaths = [
        `./modules/${moduleName}.py`,
        `./lib/${moduleName}.py`, 
        `/modules/${moduleName}.py`,
        `/lib/${moduleName}.py`,
        `./${moduleName}.py`
    ];
    
    for (const path of localPaths) {
        try {
            const response = await fetch(path);
            if (response.ok) {
                return await response.text();
            }
        } catch (e) { continue; }
    }

    // 4. Try GitHub Raw API fallback for Adafruit libraries
    try {
        const githubModule = await fetchFromAdafruitGitHub(moduleName);
        if (githubModule) return githubModule;
    } catch (e) { /* Continue */ }
    
    throw new Error(`Module '${moduleName}' not found in any location (localStorage, filesystem, HTTP paths, or GitHub)`);
}

// Browser Local Storage module fetching
async function fetchFromLocalStorage(moduleName) {
    if (!window.localStorage) return null;
    
    const storageKeys = [
        `circuitpython_module_${moduleName}`,
        `py_module_${moduleName}`,
        `module_${moduleName}`
    ];
    
    for (const key of storageKeys) {
        const source = localStorage.getItem(key);
        if (source) {
            console.log(`📦 Loaded '${moduleName}' from localStorage`);
            return source;
        }
    }
    return null;
}

// File System Access API module fetching  
async function fetchFromFileSystemAPI(moduleName) {
    // Check if File System Access API is supported
    if (!('showOpenFilePicker' in window)) return null;
    
    // This would typically be integrated with a web-editor's file manager
    // For now, we just indicate the capability exists
    console.log(`📁 File System Access API available for '${moduleName}' (requires web-editor integration)`);
    return null;
}

// GitHub Raw API fetching for Adafruit libraries
async function fetchFromAdafruitGitHub(moduleName) {
    const githubPaths = [
        `https://raw.githubusercontent.com/adafruit/Adafruit_CircuitPython_${moduleName}/main/${moduleName}.py`,
        `https://raw.githubusercontent.com/adafruit/Adafruit_CircuitPython_${moduleName}/main/${moduleName}/__init__.py`,
        `https://raw.githubusercontent.com/adafruit/CircuitPython_Community_Bundle/main/libraries/circuitpython_${moduleName}/${moduleName}.py`,
        `https://raw.githubusercontent.com/adafruit/CircuitPython_Community_Bundle/main/libraries/circuitpython_${moduleName}/__init__.py`
    ];
    
    for (const url of githubPaths) {
        try {
            const response = await fetch(url);
            if (response.ok) {
                console.log(`🌐 Loaded '${moduleName}' from GitHub: ${url}`);
                return await response.text();
            }
        } catch (e) { continue; }
    }
    return null;
}

/**
 * Node.js-based module fetching
 * @param {string} moduleName 
 * @returns {Promise<string>}
 */
async function fetchModuleNode(moduleName) {
    const { promises: fs } = await import('fs');
    const path = await import('path');
    
    // Try multiple possible paths
    const possiblePaths = [
        path.default.join(process.cwd(), 'modules', `${moduleName}.py`),
        path.default.join(process.cwd(), 'lib', `${moduleName}.py`),
        path.default.join(process.cwd(), `${moduleName}.py`),
        `./modules/${moduleName}.py`,
        `./lib/${moduleName}.py`,
        `./${moduleName}.py`
    ];
    
    for (const filepath of possiblePaths) {
        try {
            const source = await fs.readFile(filepath, 'utf8');
            return source;
        } catch (e) {
            // Continue to next path
            continue;
        }
    }
    
    throw new Error(`Module '${moduleName}' not found in Node.js`);
}

/**
 * Clear module cache for hot-reload support
 * @param {string} [moduleName] - Specific module to clear, or all if not specified
 */
function clearModuleCache(moduleName) {
    if (moduleName) {
        moduleCache.delete(moduleName);
    } else {
        moduleCache.clear();
    }
}

/**
 * Get all cached module names
 * @returns {string[]}
 */
function getCachedModules() {
    return Array.from(moduleCache.keys());
}

/**
 * Setup the module resolver for the CircuitPython WebAssembly instance
 * @param {object} cpModule - The CircuitPython WebAssembly module
 */
function setupModuleResolver(cpModule) {
    // Provide the fetchModuleSource function to the WebAssembly module
    cpModule.fetchModuleSource = (moduleName) => {
        return fetchModuleSource(moduleName).catch(error => {
            console.error(`Failed to load module '${moduleName}':`, error);
            return null;
        });
    };
    
    // Provide cache management functions
    cpModule.clearModuleCache = clearModuleCache;
    cpModule.getCachedModules = getCachedModules;
    
    console.log(`Module resolver initialized for ${isBrowser ? 'browser' : 'Node.js'}`);
}

// ES module exports
export {
    fetchModuleSource,
    clearModuleCache, 
    getCachedModules,
    setupModuleResolver
};