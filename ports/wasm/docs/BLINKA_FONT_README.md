# Blinka Font Guide for CircuitPython WASM

The CircuitPython WASM port includes the Blinka mascot character () in the REPL banner. This character is Unicode U+E000 (Private Use Area) and requires the `FreeMono-Terminal-Blinka.ttf` font to display correctly.

## What You'll See

**With the font:**
```
 Adafruit CircuitPython 10.1.0-alpha.0 on 2025-10-22; WebAssembly with Emscripten
```

**Without the font:**
```
 Adafruit CircuitPython 10.1.0-alpha.0 on 2025-10-22; WebAssembly with Emscripten
```
(The  is the fallback for U+E000)

## Font Location

After building, the font is available at:
```
./build-standard/FreeMono-Terminal-Blinka.ttf
```

## Usage in Different Environments

### 1. Web Browser (Recommended)

Use the provided example:
```bash
# Serve the example with a simple HTTP server
cd circuitpython/ports/wasm
python3 -m http.server 8000

# Open in browser:
# http://localhost:8000/examples/repl-with-blinka-font.html
```

The HTML file automatically loads the font using CSS `@font-face`:

```css
@font-face {
    font-family: 'FreeMono-Blinka';
    src: url('../build-standard/FreeMono-Terminal-Blinka.ttf') format('truetype');
}

#terminal {
    font-family: 'FreeMono-Blinka', 'Courier New', monospace;
}
```

### 2. Terminal Emulators

To see the Blinka character in Node.js output, configure your terminal to use the font:

#### Windows Terminal
1. Install the font (double-click `.ttf` file)
2. Open Settings → Profiles → Defaults → Appearance
3. Set Font face: `FreeMono-Blinka`

#### macOS Terminal
1. Install the font (double-click `.ttf` file)
2. Terminal → Preferences → Profiles → Font
3. Select `FreeMono Terminal Blinka`

#### Linux (GNOME Terminal, Konsole, etc.)
1. Install font:
   ```bash
   mkdir -p ~/.local/share/fonts
   cp build-standard/FreeMono-Terminal-Blinka.ttf ~/.local/share/fonts/
   fc-cache -f -v
   ```
2. Terminal Preferences → Font → Select `FreeMono Terminal Blinka`

#### VS Code Terminal
Add to `settings.json`:
```json
{
    "terminal.integrated.fontFamily": "FreeMono-Blinka, monospace"
}
```

### 3. Custom Terminal Integration

If you're building a custom REPL/terminal:

```javascript
import { loadCircuitPython } from './build-standard/circuitpython.mjs';

const terminal = document.getElementById('my-terminal');

// 1. Load the font via CSS
const style = document.createElement('style');
style.textContent = `
    @font-face {
        font-family: 'FreeMono-Blinka';
        src: url('./build-standard/FreeMono-Terminal-Blinka.ttf') format('truetype');
    }
`;
document.head.appendChild(style);

// 2. Apply font to terminal element
terminal.style.fontFamily = "'FreeMono-Blinka', monospace";

// 3. Load CircuitPython
const mp = await loadCircuitPython({
    stdout: (line) => {
        terminal.textContent += line;
    }
});

// 4. The Blinka character will display correctly!
mp.runPython('import sys; print(sys.version)');
```

## The Blinka Character

- **Unicode Code Point**: U+E000 (Private Use Area)
- **Character in JavaScript**: `'\uE000'` or `String.fromCharCode(0xE000)`
- **Character in Python**: `'\uE000'`
- **Glyph**: The Adafruit Blinka mascot (purple circuit board bird)

## Testing the Font

### Quick Test in Browser

Open browser console and paste:
```javascript
const style = document.createElement('style');
style.textContent = `
    @font-face {
        font-family: 'FreeMono-Blinka';
        src: url('http://localhost:8000/build-standard/FreeMono-Terminal-Blinka.ttf') format('truetype');
    }
`;
document.head.appendChild(style);

const div = document.createElement('div');
div.style.fontFamily = "'FreeMono-Blinka', monospace";
div.style.fontSize = '48px';
div.textContent = '\uE000 Blinka!';
document.body.appendChild(div);
```

### Node.js Test

```javascript
import { loadCircuitPython } from './build-standard/circuitpython.mjs';

const mp = await loadCircuitPython();
mp.runPython('import sys; print(sys.version)');
// Output: 3.4.0;  CircuitPython 10.1.0-alpha.0 on 2025-10-22; WebAssembly with Emscripten
//             ^ U+E000 character (renders as Blinka with the font)
```

## Troubleshooting

### Character shows as  or □
- Font is not loaded or not applied to the terminal/element
- Check browser dev tools Network tab to ensure font loads successfully
- Verify CSS `font-family` is set correctly

### Font file not found (404)
- Ensure you're serving the `build-standard` directory
- Check the path in your HTML/CSS matches the actual file location
- Use relative paths: `url('../build-standard/FreeMono-Terminal-Blinka.ttf')`

### Font doesn't load in browser
- Check CORS if serving from different domain
- Verify font format is supported: `format('truetype')`
- Check browser console for errors

## Future Enhancements

The font currently contains:
- ✅ Blinka mascot character at U+E000
- ✅ Standard FreeMono glyphs

Potential additions:
- [ ] Additional Adafruit/CircuitPython icons
- [ ] Emoji alternatives at standard Unicode points
- [ ] Terminal box-drawing variants

## Resources

- [Font File](./build-standard/FreeMono-Terminal-Blinka.ttf)
- [Example REPL](./examples/repl-with-blinka-font.html)
- [Adafruit Blinka](https://circuitpython.org/blinka)
