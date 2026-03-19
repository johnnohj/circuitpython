/*
 * test_phase6b.mjs — Phase 6b: displayio shim + Canvas2D rendering pipeline
 *
 * Tests:
 *   T1: display_init event emitted on Display creation
 *   T2: display_refresh event with pixel data attached
 *   T3: Red pixel at (0,0) on a 4×4 display — correct RGB bytes
 *   T4: Transparency — transparent palette index is skipped (black background)
 *   T5: TileGrid positioning — pixel at correct offset
 *   T6: Group nesting — nested group offset accumulates
 *   T7: HardwareSimulator tracks display state and getDisplayPixel works
 *   T8: flip_x — bitmap is horizontally mirrored
 */

import { PythonHost }        from './js/PythonHost.js';
import { HardwareSimulator } from './js/HardwareSimulator.js';
import { CHANNEL }           from './js/BroadcastBus.js';

let passed = 0;
let failed = 0;

function assert(cond, name, detail) {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name, detail ?? ''); failed++; }
}

// ── Setup ────────────────────────────────────────────────────────────────────

const python = new PythonHost({ executors: 1, timeout: 5000 });
await python.init();
python.installBlinka();
await new Promise(r => setTimeout(r, 300));

// Collect hw events via BroadcastChannel (separate rx — sender doesn't get its own)
function collectHW(timeoutMs = 500) {
    const events = [];
    const bc = new BroadcastChannel(CHANNEL);
    bc.onmessage = (e) => { if (e.data.type === 'hw') events.push(e.data); };
    return {
        events,
        stop() { bc.close(); },
        async waitForExec(code) {
            const result = await python.exec(code);
            // Give BC a tick to deliver trailing events
            await new Promise(r => setTimeout(r, 100));
            bc.close();
            return { events, result };
        },
    };
}

// ── T1: display_init event ──────────────────────────────────────────────────

{
    const c = collectHW();
    const { events } = await c.waitForExec(`
import displayio
d = displayio.Display(width=16, height=16, auto_refresh=False)
`);
    const init = events.find(e => e.cmd === 'display_init');
    assert(init !== undefined,      'T1: display_init event emitted');
    assert(init?.width === 16,      'T1: width=16');
    assert(init?.height === 16,     'T1: height=16');
}

// ── T2: display_refresh with pixels ─────────────────────────────────────────

{
    const c = collectHW();
    const { events } = await c.waitForExec(`
import displayio
d = displayio.Display(width=4, height=4, auto_refresh=False)
bm = displayio.Bitmap(4, 4, 2)
pal = displayio.Palette(2)
pal[0] = 0x000000
pal[1] = 0xFF0000
bm[0, 0] = 1
tg = displayio.TileGrid(bm, pixel_shader=pal)
g = displayio.Group()
g.append(tg)
d.root_group = g
d.refresh()
`);
    const refresh = events.find(e => e.cmd === 'display_refresh');
    assert(refresh !== undefined,           'T2: display_refresh event emitted');
    assert(refresh?.pixels instanceof Uint8Array, 'T2: pixels is Uint8Array');
    assert(refresh?.pixels?.length === 4 * 4 * 3, 'T2: pixel data size = 48 bytes');
}

// ── T3: Red pixel at (0,0) ─────────────────────────────────────────────────

{
    const c = collectHW();
    const { events } = await c.waitForExec(`
import displayio
d = displayio.Display(width=4, height=4, auto_refresh=False)
bm = displayio.Bitmap(4, 4, 2)
pal = displayio.Palette(2)
pal[0] = 0x000000
pal[1] = 0xFF0000
bm[0, 0] = 1
tg = displayio.TileGrid(bm, pixel_shader=pal)
g = displayio.Group()
g.append(tg)
d.root_group = g
d.refresh()
`);
    const refresh = events.find(e => e.cmd === 'display_refresh');
    const px = refresh?.pixels;
    // Pixel (0,0) = index 0 in framebuffer = bytes [0,1,2] = RGB
    assert(px?.[0] === 0xFF, 'T3: pixel(0,0) R=255', `got ${px?.[0]}`);
    assert(px?.[1] === 0x00, 'T3: pixel(0,0) G=0',   `got ${px?.[1]}`);
    assert(px?.[2] === 0x00, 'T3: pixel(0,0) B=0',   `got ${px?.[2]}`);
    // Pixel (1,0) should be black
    assert(px?.[3] === 0x00, 'T3: pixel(1,0) R=0 (black)', `got ${px?.[3]}`);
}

// ── T4: Transparency ────────────────────────────────────────────────────────

{
    const c = collectHW();
    const { events } = await c.waitForExec(`
import displayio
d = displayio.Display(width=4, height=4, auto_refresh=False)
bm = displayio.Bitmap(4, 4, 3)
pal = displayio.Palette(3)
pal[0] = 0x000000
pal[1] = 0xFF0000
pal[2] = 0x00FF00
pal.make_transparent(0)  # index 0 is transparent
bm.fill(0)               # all transparent
bm[1, 1] = 1             # red at (1,1)
bm[2, 1] = 2             # green at (2,1)
tg = displayio.TileGrid(bm, pixel_shader=pal)
g = displayio.Group()
g.append(tg)
d.root_group = g
d.refresh()
`);
    const px = events.find(e => e.cmd === 'display_refresh')?.pixels;
    // (0,0) should be black (transparent → background)
    assert(px?.[0] === 0 && px?.[1] === 0 && px?.[2] === 0,
        'T4: transparent pixel stays black');
    // (1,1) = row 1, col 1 → offset = (1*4+1)*3 = 15
    assert(px?.[15] === 0xFF && px?.[16] === 0 && px?.[17] === 0,
        'T4: red at (1,1)', `got [${px?.[15]},${px?.[16]},${px?.[17]}]`);
    // (2,1) = row 1, col 2 → offset = (1*4+2)*3 = 18
    assert(px?.[18] === 0 && px?.[19] === 0xFF && px?.[20] === 0,
        'T4: green at (2,1)', `got [${px?.[18]},${px?.[19]},${px?.[20]}]`);
}

// ── T5: TileGrid positioning ────────────────────────────────────────────────

{
    const c = collectHW();
    const { events } = await c.waitForExec(`
import displayio
d = displayio.Display(width=8, height=8, auto_refresh=False)
bm = displayio.Bitmap(2, 2, 2)
pal = displayio.Palette(2)
pal[0] = 0x000000
pal[1] = 0x0000FF
bm[0, 0] = 1  # blue at bitmap origin
tg = displayio.TileGrid(bm, pixel_shader=pal, x=3, y=2)
g = displayio.Group()
g.append(tg)
d.root_group = g
d.refresh()
`);
    const px = events.find(e => e.cmd === 'display_refresh')?.pixels;
    // Blue pixel at display (3,2) → offset = (2*8+3)*3 = 57
    assert(px?.[57] === 0 && px?.[58] === 0 && px?.[59] === 0xFF,
        'T5: blue at (3,2)', `got [${px?.[57]},${px?.[58]},${px?.[59]}]`);
    // Origin (0,0) should be black
    assert(px?.[0] === 0, 'T5: origin is black');
}

// ── T6: Nested Group offset ─────────────────────────────────────────────────

{
    const c = collectHW();
    const { events } = await c.waitForExec(`
import displayio
d = displayio.Display(width=8, height=8, auto_refresh=False)
bm = displayio.Bitmap(1, 1, 2)
pal = displayio.Palette(2)
pal[0] = 0x000000
pal[1] = 0xFFFF00
bm[0, 0] = 1  # yellow
tg = displayio.TileGrid(bm, pixel_shader=pal, x=1, y=1)
inner = displayio.Group(x=2, y=3)
inner.append(tg)
outer = displayio.Group(x=1, y=1)
outer.append(inner)
d.root_group = outer
d.refresh()
`);
    const px = events.find(e => e.cmd === 'display_refresh')?.pixels;
    // Position: outer(1,1) + inner(2,3) + tg(1,1) = (4, 5)
    // Offset = (5*8+4)*3 = 132
    assert(px?.[132] === 0xFF && px?.[133] === 0xFF && px?.[134] === 0,
        'T6: yellow at (4,5)', `got [${px?.[132]},${px?.[133]},${px?.[134]}]`);
}

// ── T7: HardwareSimulator display tracking ──────────────────────────────────

{
    const hw = new HardwareSimulator();
    hw.start();

    await python.exec(`
import displayio
d = displayio.Display(width=4, height=4, auto_refresh=False)
bm = displayio.Bitmap(4, 4, 2)
pal = displayio.Palette(2)
pal[0] = 0x000000
pal[1] = 0x00FF00
bm[2, 3] = 1
tg = displayio.TileGrid(bm, pixel_shader=pal)
g = displayio.Group()
g.append(tg)
d.root_group = g
d.refresh()
`);
    await new Promise(r => setTimeout(r, 200));

    // Find the display by checking all tracked displays
    const ids = Object.keys(hw.displays);
    assert(ids.length >= 1, 'T7: at least one display tracked');
    const d = hw.displays[ids[ids.length - 1]];
    assert(d?.width === 4 && d?.height === 4, 'T7: display dimensions correct');
    // Check pixel (2,3) = green
    const p = hw.getDisplayPixel(ids[ids.length - 1], 2, 3);
    assert(p?.[0] === 0 && p?.[1] === 0xFF && p?.[2] === 0,
        'T7: getDisplayPixel(2,3) = green', `got [${p}]`);
    hw.stop();
}

// ── T8: flip_x ──────────────────────────────────────────────────────────────

{
    const c = collectHW();
    const { events } = await c.waitForExec(`
import displayio
d = displayio.Display(width=4, height=1, auto_refresh=False)
bm = displayio.Bitmap(4, 1, 3)
pal = displayio.Palette(3)
pal[0] = 0x000000
pal[1] = 0xFF0000
pal[2] = 0x00FF00
bm[0, 0] = 1  # red at left
bm[3, 0] = 2  # green at right
tg = displayio.TileGrid(bm, pixel_shader=pal)
tg.flip_x = True
g = displayio.Group()
g.append(tg)
d.root_group = g
d.refresh()
`);
    const px = events.find(e => e.cmd === 'display_refresh')?.pixels;
    // With flip_x, red (originally at x=0) should now be at x=3
    // and green (originally at x=3) should be at x=0
    assert(px?.[0] === 0 && px?.[1] === 0xFF && px?.[2] === 0,
        'T8: flip_x — green moved to x=0', `got [${px?.[0]},${px?.[1]},${px?.[2]}]`);
    assert(px?.[9] === 0xFF && px?.[10] === 0 && px?.[11] === 0,
        'T8: flip_x — red moved to x=3', `got [${px?.[9]},${px?.[10]},${px?.[11]}]`);
}

// ── Cleanup ──────────────────────────────────────────────────────────────────

await python.shutdown();

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
