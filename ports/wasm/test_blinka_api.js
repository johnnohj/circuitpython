// Test Blinka glyph API integration
import createCircuitPythonModule from './build/circuitpython.mjs';

createCircuitPythonModule().then(CP => {
  console.log("CircuitPython module loaded");
  console.log("Available functions:", Object.keys(CP).filter(k => k.includes('mp_js')));
  
  // Test if the blinka glyph function is available
  if (typeof CP._mp_js_get_blinka_glyph_path === 'function') {
    const glyphPath = CP._mp_js_get_blinka_glyph_path();
    console.log("✅ Blinka glyph API working! Path:", glyphPath);
    
    // Test if the file exists
    import('fs').then(fs => {
      if (fs.existsSync(glyphPath)) {
        console.log("✅ Blinka glyph file exists and ready for @xterm/addon-image");
      } else {
        console.log("❌ Blinka glyph file not found");
      }
    }).catch(() => {
      console.log("ℹ️  File check not available in browser context");
    });
    
  } else {
    console.log("❌ Blinka glyph API not available");
    console.log("Available functions:", Object.keys(CP));
  }
  
}).catch(error => {
  console.error("Failed to load CircuitPython module:", error);
});