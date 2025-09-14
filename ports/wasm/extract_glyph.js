const { Jimp } = require('jimp');

async function extractGlyph() {
    try {
        // Load the BMP image
        const image = await Jimp.read('./blinka.bmp');
        
        // Find the actual content bounds by scanning for non-black pixels
        let minX = image.width, minY = image.height, maxX = 0, maxY = 0;
        let foundContent = false;
        
        image.scan(0, 0, image.width, image.height, function (x, y, idx) {
            // Check if pixel is not black (allowing for some tolerance)
            const r = this.bitmap.data[idx + 0];
            const g = this.bitmap.data[idx + 1];
            const b = this.bitmap.data[idx + 2];
            
            if (r > 10 || g > 10 || b > 10) { // Not pure black
                foundContent = true;
                minX = Math.min(minX, x);
                minY = Math.min(minY, y);
                maxX = Math.max(maxX, x);
                maxY = Math.max(maxY, y);
            }
        });
        
        let glyph;
        if (foundContent) {
            const cropWidth = maxX - minX + 1;
            const cropHeight = maxY - minY + 1;
            console.log(`Found content bounds: ${minX},${minY} to ${maxX},${maxY} (${cropWidth}x${cropHeight})`);
            
            // Crop to the actual content
            glyph = image.crop({ x: minX, y: minY, w: cropWidth, h: cropHeight });
        } else {
            console.log('No non-black content found, using original image');
            glyph = image;
        }
        
        // Save as PNG (better for glyphs, supports transparency)
        await glyph.write('blinka_glyph.png');
        
        console.log(`Extracted ${glyph.width}x${glyph.height} glyph from blinka.bmp to blinka_glyph.png`);
        console.log(`Original image size: ${image.width}x${image.height}`);
        
        // Also create resized versions for testing
        const glyph16 = glyph.clone().resize({ w: 16, h: 16 });
        await glyph16.write('blinka_glyph_16x16.png');
        
        const glyph20 = glyph.clone().resize({ w: 20, h: 20 });
        await glyph20.write('blinka_glyph_20x20.png');
        
        console.log('Also created 16x16 and 20x20 versions');
        
    } catch (error) {
        console.error('Error processing image:', error);
    }
}

extractGlyph();