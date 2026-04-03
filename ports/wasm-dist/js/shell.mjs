/**
 * shell.mjs — Shell command dispatch for CircuitPython WASM REPL.
 *
 * Intercepts lines that match shell builtins (fwip, ctx) before they
 * reach cp_exec.  Pure logic — no DOM dependencies.
 *
 * @param {string} line — the raw input line
 * @param {object} ctx — context object with exports, fwip, readline, ctxMax, readContextMeta
 * @returns {boolean} true if the line was handled as a shell command
 */

const CTX_STATUSES = ['FREE', 'IDLE', 'RUNNABLE', 'RUNNING', 'YIELDED', 'SLEEPING', 'DONE'];

export function tryShellCommand(line, { exports, fwip, readline, ctxMax, readContextMeta }) {
    const trimmed = line.trim();
    if (!trimmed) return false;

    const parts = trimmed.split(/\s+/);
    const write = (text) => readline.termWrite(text);
    const prompt = () => readline.showPrompt();

    // ── ctx — context management ──
    if (parts[0] === 'ctx') {
        const sub = parts[1] || 'help';

        if (sub === 'run') {
            const code = trimmed.slice(trimmed.indexOf('run') + 4);
            if (!code.trim()) {
                write('Usage: ctx run <python code>\r\n');
                prompt();
                return true;
            }
            const len = readline.writeInputBuf(code);
            const id = exports.cp_context_exec(len, 200);
            if (id >= 0) write(`[ctx] started context ${id}\r\n`);
            else if (id === -1) write('[ctx] error: no free context slots\r\n');
            else write('[ctx] error: compile failed\r\n');
            prompt();
            return true;
        }

        if (sub === 'file') {
            const path = parts[2];
            if (!path) {
                write('Usage: ctx file <path>\r\n');
                prompt();
                return true;
            }
            const len = readline.writeInputBuf(path);
            const id = exports.cp_context_exec_file(len, 200);
            if (id >= 0) write(`[ctx] started context ${id} from ${path}\r\n`);
            else if (id === -1) write('[ctx] error: no free context slots\r\n');
            else write(`[ctx] error: compile failed for ${path}\r\n`);
            prompt();
            return true;
        }

        if (sub === 'list') {
            let found = false;
            for (let i = 0; i < ctxMax; i++) {
                const m = readContextMeta(i);
                if (m && m.status > 0) {
                    write(`  ctx ${i}: ${CTX_STATUSES[m.status]} pri=${m.priority}\r\n`);
                    found = true;
                }
            }
            if (!found) write('  No active contexts.\r\n');
            prompt();
            return true;
        }

        if (sub === 'kill') {
            const id = parseInt(parts[2]);
            if (id > 0 && id < ctxMax) {
                exports.cp_context_destroy(id);
                write(`[ctx] killed context ${id}\r\n`);
            } else {
                write('Usage: ctx kill <id>  (id > 0)\r\n');
            }
            prompt();
            return true;
        }

        write('ctx — execution context manager\r\n');
        write('  ctx run <code>       run code in background context\r\n');
        write('  ctx file <path>      run .py file in background context\r\n');
        write('  ctx list             list active contexts\r\n');
        write('  ctx kill <id>        destroy a context\r\n');
        prompt();
        return true;
    }

    // ── fwip — firmware package installer ──
    if (parts[0] !== 'fwip') return false;

    const flags = new Set(parts.filter(p => p.startsWith('--')));
    const words = parts.filter(p => !p.startsWith('--'));
    const usePy = flags.has('--py');
    const sub = words[1] || 'help';
    const args = words.slice(2);

    if (sub === 'install' || (!['remove', 'list', 'freeze', 'frozen', 'help', '-r'].includes(sub))) {
        const name = sub === 'install' ? args[0] : sub;
        if (!name) {
            write('Usage: fwip install <package> [--py]\r\n');
            prompt();
            return true;
        }
        const kind = usePy ? ' (source)' : '';
        write(`[fwip] installing ${name}${kind}...\r\n`);
        fwip.install(name, { py: usePy }).then((info) => {
            if (info) {
                write(`[fwip] installed ${info.name}@${info.version} (${info.files.length} file${info.files.length === 1 ? '' : 's'})\r\n`);
            }
            prompt();
        }).catch((err) => {
            write(`[fwip] error: ${err.message}\r\n`);
            prompt();
        });
        return true;
    }

    if (sub === '-r') {
        write('[fwip] installing from requirements.txt...\r\n');
        fwip.installRequirements({ py: usePy }).then(() => prompt())
            .catch((err) => { write(`[fwip] error: ${err.message}\r\n`); prompt(); });
        return true;
    }

    if (sub === 'freeze') {
        try {
            const lines = fwip.freeze();
            for (const l of lines) write(`  ${l}\r\n`);
        } catch (err) { write(`[fwip] error: ${err.message}\r\n`); }
        prompt();
        return true;
    }

    if (sub === 'remove') {
        const name = args[0];
        if (!name) { write('Usage: fwip remove <package>\r\n'); prompt(); return true; }
        try { fwip.remove(name); write(`[fwip] removed ${name}\r\n`); }
        catch (err) { write(`[fwip] error: ${err.message}\r\n`); }
        prompt();
        return true;
    }

    if (sub === 'frozen') {
        const frozen = fwip._getFrozenModules();
        if (frozen.size === 0) { write('No frozen modules detected.\r\n'); }
        else { for (const m of [...frozen].sort()) write(`  ${m} (frozen)\r\n`); }
        prompt();
        return true;
    }

    if (sub === 'list') {
        const pkgs = fwip.list();
        if (pkgs.length === 0) write('No packages installed.\r\n');
        else for (const pkg of pkgs) write(`  ${pkg.module} (${pkg.version})\r\n`);
        prompt();
        return true;
    }

    write('fwip — firmware package installer\r\n');
    write('  fwip <package>           install a package\r\n');
    write('  fwip install <pkg> [--py] install (--py for source)\r\n');
    write('  fwip remove <package>    remove a package\r\n');
    write('  fwip list                list installed packages\r\n');
    write('  fwip freeze              write requirements.txt\r\n');
    write('  fwip frozen              list frozen (built-in) modules\r\n');
    write('  fwip -r [--py]           install from requirements.txt\r\n');
    prompt();
    return true;
}
