// Test Blinka glyph API with proper string conversion
import createCircuitPythonModule from './build/circuitpython.mjs';

createCircuitPythonModule().then(CP => {
  console.log("CircuitPython module loaded");
  
  // Test if the blinka glyph function is available
  if (typeof CP._mp_js_get_blinka_glyph_path === 'function') {
    const glyphPtr = CP._mp_js_get_blinka_glyph_path();
    console.log("Glyph pointer:", glyphPtr);
    
    // Convert the pointer to string using UTF8ToString
    if (CP.UTF8ToString) {
      const glyphPath = CP.UTF8ToString(glyphPtr);
      console.log("✅ Blinka glyph API working! Path:", glyphPath);
      
      // Test if the file exists
      import('fs').then(fs => {
        if (fs.existsSync(glyphPath)) {
          console.log("✅ Blinka glyph file exists and ready for @xterm/addon-image");
          console.log("📋 Integration example:");
          console.log(`   imageAddon.showImage('${glyphPath}', { width: 16, height: 16 });`);
        } else {
          console.log("❌ Blinka glyph file not found at:", glyphPath);
        }
      }).catch(() => {
        console.log("ℹ️  File check not available in browser context");
        console.log("📋 Browser integration example:");
        console.log(`   imageAddon.showImage('${glyphPath}', { width: 16, height: 16 });`);
      });
      
    } else {
      console.log("❌ UTF8ToString not available");
    }
    
  } else {
    console.log("❌ Blinka glyph API not available");
  }
  
}).catch(error => {
  console.error("Failed to load CircuitPython module:", error);
});