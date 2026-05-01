/**
 * fwip.js — Firmware package installer for CircuitPython WASM.
 *
 * Python has pip, MicroPython has mip, we have fwip.
 *
 * Fetches .mpy libraries from individual Adafruit CircuitPython library
 * repos on GitHub and writes them into the MEMFS CIRCUITPY/lib/ directory.
 *
 * Each Adafruit library repo publishes:
 *   {pypi_name}-{version}.json           — metadata (deps, path, package flag)
 *   {pypi_name}-10.x-mpy-{version}.zip   — compiled .mpy files for CP 10.x
 *
 * Usage:
 *   import { Fwip } from './fwip.js';
 *   const fwip = new Fwip(memfs);               // memfs = WasiMemfs instance
 *   await fwip.install('neopixel');              // installs + dependencies
 *   await fwip.install('adafruit_display_text'); // package name or module name
 *   fwip.list();                                 // → ['neopixel', 'adafruit_display_text', ...]
 */

const GITHUB_API = 'https://api.github.com/repos';
const CP_MAJOR = '10';
const LIB_PREFIX = '/CIRCUITPY/lib';

/**
 * Normalize a user-provided name to the pypi-style name.
 *   'neopixel'              → 'adafruit-circuitpython-neopixel'
 *   'adafruit_display_text' → 'adafruit-circuitpython-display-text'
 *   'adafruit_bus_device'   → 'adafruit-circuitpython-busdevice'
 *
 * If the name already starts with 'adafruit-circuitpython-', pass through.
 */
function toPypiName(name) {
    // Already in pypi form
    if (name.startsWith('adafruit-circuitpython-')) return name;

    // Strip 'adafruit_' prefix if present — we'll re-add the canonical form
    let core = name;
    if (core.startsWith('adafruit_')) {
        core = core.slice('adafruit_'.length);
    }

    // Underscores → hyphens for pypi convention
    core = core.replace(/_/g, '-');

    return `adafruit-circuitpython-${core}`;
}

/**
 * Derive the GitHub repo name from the pypi name.
 *   'adafruit-circuitpython-neopixel' → 'Adafruit_CircuitPython_NeoPixel'
 *
 * Each hyphen-separated word is title-cased and joined with underscores.
 */
function toRepoName(pypiName) {
    return pypiName
        .split('-')
        .map(w => w.charAt(0).toUpperCase() + w.slice(1))
        .join('_');
}

export class Fwip {
    /**
     * @param {import('./wasi-memfs.js').WasiMemfs} memfs
     * @param {object} [options]
     * @param {string} [options.cpMajor]    — CircuitPython major version (default '10')
     * @param {string} [options.libPrefix]  — MEMFS lib path (default '/CIRCUITPY/lib')
     * @param {function} [options.log]      — logging callback (default console.log)
     * @param {object} [options.exports]   — WASM instance exports (for frozen module detection)
     */
    constructor(memfs, options = {}) {
        this.memfs = memfs;
        this.cpMajor = options.cpMajor || CP_MAJOR;
        this.libPrefix = options.libPrefix || LIB_PREFIX;
        this.log = options.log || console.log;
        this.exports = options.exports || null;

        // Track installed packages: pypiName → { version, path, files }
        this.installed = new Map();

        // Prevent circular dependency loops
        this._installing = new Set();

        // Frozen modules (parsed once from WASM linear memory)
        this._frozenModules = null;
    }

    /**
     * Parse mp_frozen_names from WASM linear memory.
     * Returns a Set of module names (e.g. 'neopixel', 'adafruit_bus_device').
     * Names are stored as "neopixel.py\0adafruit_bus_device/__init__.py\0\0".
     */
    _getFrozenModules() {
        if (this._frozenModules) return this._frozenModules;
        this._frozenModules = new Set();

        if (!this.exports?.cp_frozen_names_addr) return this._frozenModules;

        const addr = this.exports.cp_frozen_names_addr();
        const mem = new Uint8Array(this.exports.memory.buffer);
        let pos = addr;

        while (mem[pos] !== 0) {
            // Find end of this null-terminated entry
            let end = pos;
            while (mem[end] !== 0) end++;
            const entry = new TextDecoder().decode(mem.subarray(pos, end));

            // "neopixel.py" → "neopixel"
            // "adafruit_bus_device/__init__.py" → "adafruit_bus_device"
            let modName = entry;
            if (modName.endsWith('.py')) modName = modName.slice(0, -3);
            if (modName.endsWith('/__init__')) modName = modName.slice(0, -9);
            // Only top-level modules (skip submodules like "asyncio/funcs")
            if (!modName.includes('/')) {
                this._frozenModules.add(modName);
            }

            pos = end + 1; // skip null
        }

        return this._frozenModules;
    }

    /**
     * Check if a module name is frozen into the firmware.
     * @param {string} pypiName — pypi-style name
     * @returns {boolean}
     */
    isFrozen(pypiName) {
        const frozen = this._getFrozenModules();
        if (frozen.size === 0) return false;

        // pypi name → import name: "adafruit-circuitpython-neopixel" → "neopixel"
        // or "adafruit-circuitpython-bus-device" → "adafruit_bus_device"
        let importName = pypiName;
        if (importName.startsWith('adafruit-circuitpython-')) {
            const core = importName.slice('adafruit-circuitpython-'.length);
            // Some modules have the adafruit_ prefix in their import name
            importName = core.replace(/-/g, '_');
            if (frozen.has(`adafruit_${importName}`)) return true;
        }
        return frozen.has(importName);
    }

    /**
     * Install a library and its dependencies.
     * @param {string} name — module name, pypi name, or partial name
     * @param {object} [options]
     * @param {boolean} [options.deps] — install dependencies (default true)
     * @param {boolean} [options.py]   — install .py source instead of .mpy (default false)
     * @returns {Promise<{name: string, version: string, files: string[]}>}
     */
    async install(name, options = {}) {
        const installDeps = options.deps !== false;
        const usePy = options.py === true;
        const pypiName = toPypiName(name);

        // Frozen into firmware — no install needed
        if (this.isFrozen(pypiName)) {
            this.log(`[fwip] ${pypiName} is frozen in firmware, skipping`);
            return { name: pypiName, module: name, version: 'frozen', files: [] };
        }

        // Already installed?
        if (this.installed.has(pypiName)) {
            const info = this.installed.get(pypiName);
            this.log(`[fwip] ${pypiName} already installed (${info.version})`);
            return info;
        }

        // Circular dependency guard
        if (this._installing.has(pypiName)) return null;
        this._installing.add(pypiName);

        try {
            const repoName = toRepoName(pypiName);
            const org = 'adafruit';

            // 1. Get latest release tag
            this.log(`[fwip] fetching ${pypiName}...`);
            const release = await this._fetchJson(
                `${GITHUB_API}/${org}/${repoName}/releases/latest`
            );
            const tag = release.tag_name;

            // 2. Fetch metadata JSON
            const metaUrl = this._findAsset(release, `${pypiName}-${tag}.json`);
            if (!metaUrl) {
                throw new Error(`No metadata JSON found for ${pypiName} ${tag}`);
            }
            const metaObj = await this._fetchJson(metaUrl);
            // The JSON has one key — the module name
            const moduleName = Object.keys(metaObj)[0];
            const meta = metaObj[moduleName];

            // 3. Fetch the ZIP (.py source or .mpy compiled)
            const zipName = usePy
                ? `${pypiName}-py-${tag}.zip`
                : `${pypiName}-${this.cpMajor}.x-mpy-${tag}.zip`;
            const zipUrl = this._findAsset(release, zipName);
            if (!zipUrl) {
                const kind = usePy ? 'py source' : `${this.cpMajor}.x mpy`;
                throw new Error(`No ${kind} bundle found for ${pypiName} ${tag}`);
            }

            const zipData = await this._fetchBinary(zipUrl);

            // 4. Extract into CIRCUITPY/lib/
            const files = await this._extractZip(zipData, meta);

            // 5. Record installation
            const info = { name: pypiName, module: moduleName, version: tag, files };
            this.installed.set(pypiName, info);
            this.log(`[fwip] installed ${pypiName}@${tag} (${files.length} file${files.length === 1 ? '' : 's'})`);

            // 6. Install dependencies (propagate --py flag)
            if (installDeps && meta.external_dependencies) {
                for (const dep of meta.external_dependencies) {
                    await this.install(dep, { py: usePy });
                }
            }

            return info;
        } finally {
            this._installing.delete(pypiName);
        }
    }

    /**
     * List installed packages.
     * @returns {Array<{name: string, module: string, version: string}>}
     */
    list() {
        return [...this.installed.values()].map(
            ({ name, module, version }) => ({ name, module, version })
        );
    }

    /**
     * Write requirements.txt to CIRCUITPY from installed packages.
     * Mirrors circup's `freeze -r` behavior.
     */
    freeze() {
        const lines = [...this.installed.values()]
            .map(({ name, version }) => `${name}==${version}`)
            .sort();
        const content = lines.join('\n') + (lines.length ? '\n' : '');
        const enc = new TextEncoder();
        this.memfs.writeFile('/CIRCUITPY/requirements.txt', enc.encode(content));
        this.log(`[fwip] wrote requirements.txt (${lines.length} package${lines.length === 1 ? '' : 's'})`);
        return lines;
    }

    /**
     * Install packages from CIRCUITPY/requirements.txt.
     * Mirrors circup's `install -r requirements.txt` behavior.
     * @param {object} [options]
     * @param {boolean} [options.py] — install .py source instead of .mpy
     * @returns {Promise<number>} number of packages installed
     */
    async installRequirements(options = {}) {
        const data = this.memfs.readFile('/CIRCUITPY/requirements.txt');
        if (!data) {
            throw new Error('No requirements.txt found on CIRCUITPY');
        }
        const text = new TextDecoder().decode(data);
        const names = text.split('\n')
            .map(line => line.replace(/#.*/, '').trim())    // strip comments
            .filter(line => line.length > 0)
            .map(line => line.replace(/[=<>!]=.*/, ''));     // strip version specifiers

        let count = 0;
        for (const name of names) {
            const info = await this.install(name, { py: options.py });
            if (info) count++;
        }
        this.log(`[fwip] installed ${count} package${count === 1 ? '' : 's'} from requirements.txt`);
        return count;
    }

    /**
     * Remove an installed package from CIRCUITPY/lib/.
     * @param {string} name
     */
    remove(name) {
        const pypiName = toPypiName(name);
        const info = this.installed.get(pypiName);
        if (!info) {
            this.log(`[fwip] ${pypiName} not installed`);
            return false;
        }
        for (const f of info.files) {
            this.memfs.files.delete(f);
        }
        this.installed.delete(pypiName);
        this.log(`[fwip] removed ${pypiName}`);
        return true;
    }

    // ── internals ──

    _findAsset(release, filename) {
        const asset = release.assets?.find(a => a.name === filename);
        return asset?.browser_download_url || null;
    }

    async _fetchJson(url) {
        const res = await fetch(url, {
            headers: { 'Accept': 'application/json' },
        });
        if (!res.ok) throw new Error(`fetch ${url}: ${res.status} ${res.statusText}`);
        return res.json();
    }

    async _fetchBinary(url) {
        const res = await fetch(url);
        if (!res.ok) throw new Error(`fetch ${url}: ${res.status} ${res.statusText}`);
        return new Uint8Array(await res.arrayBuffer());
    }

    /**
     * Extract files from a ZIP into CIRCUITPY/lib/.
     *
     * ZIP format (local file headers only — no need for full unzip):
     *   PK\x03\x04 signature, then fixed header, then filename, then data.
     *   We only handle STORED (compression=0) files — Adafruit .mpy ZIPs
     *   use STORED since .mpy is already compact.
     *
     * The ZIP contains a top-level directory (e.g., "lib/neopixel.mpy" or
     * "lib/adafruit_display_text/label.mpy"). We strip the "lib/" prefix
     * and write to CIRCUITPY/lib/.
     */
    async _extractZip(zipData, meta) {
        const files = [];
        const view = new DataView(zipData.buffer, zipData.byteOffset, zipData.byteLength);
        let offset = 0;

        while (offset + 30 <= zipData.length) {
            const sig = view.getUint32(offset, true);
            if (sig !== 0x04034b50) break; // Not a local file header

            const compression = view.getUint16(offset + 8, true);
            const compressedSize = view.getUint32(offset + 18, true);
            const uncompressedSize = view.getUint32(offset + 22, true);
            const nameLen = view.getUint16(offset + 26, true);
            const extraLen = view.getUint16(offset + 28, true);

            const nameBytes = zipData.subarray(offset + 30, offset + 30 + nameLen);
            const entryName = new TextDecoder().decode(nameBytes);

            const dataStart = offset + 30 + nameLen + extraLen;

            // Skip directories and non-lib entries
            if (!entryName.endsWith('/') && entryName.includes('lib/')) {
                // Strip everything up to and including 'lib/'
                const libIdx = entryName.indexOf('lib/');
                const relativePath = entryName.slice(libIdx + 4); // after 'lib/'

                if (relativePath) {
                    const destPath = `${this.libPrefix}/${relativePath}`;

                    if (compression === 0) {
                        // STORED
                        const fileData = zipData.slice(dataStart, dataStart + uncompressedSize);
                        this.memfs.writeFile(destPath, fileData);
                        files.push(destPath);
                    } else if (compression === 8) {
                        // DEFLATE — use DecompressionStream
                        const compressed = zipData.slice(dataStart, dataStart + compressedSize);
                        const decompressed = await this._inflate(compressed);
                        this.memfs.writeFile(destPath, decompressed);
                        files.push(destPath);
                    } else {
                        this.log(`[fwip] skipping ${entryName} (unsupported compression ${compression})`);
                    }
                }
            }

            // Advance to next entry
            offset = dataStart + compressedSize;
        }

        return files;
    }

    /**
     * Inflate raw DEFLATE data using the browser's DecompressionStream API.
     */
    async _inflate(compressed) {
        // DecompressionStream expects raw deflate (no gzip/zlib header)
        const ds = new DecompressionStream('raw');
        const writer = ds.writable.getWriter();
        writer.write(compressed);
        writer.close();

        const reader = ds.readable.getReader();
        const chunks = [];
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
        }

        // Concatenate chunks
        const totalLen = chunks.reduce((sum, c) => sum + c.length, 0);
        const result = new Uint8Array(totalLen);
        let pos = 0;
        for (const chunk of chunks) {
            result.set(chunk, pos);
            pos += chunk.length;
        }
        return result;
    }
}
