const { Jimp } = require('jimp');

async function createFullPNG() {
    try {
        // Load the original BMP image
        const image = await Jimp.read('./blinka.bmp');
        
        // Save the full image as PNG (with margins intact)
        await image.write('blinka_full.png');
        
        console.log(`Created full PNG version: blinka_full.png (${image.width}x${image.height})`);
        
    } catch (error) {
        console.error('Error processing image:', error);
    }
}

createFullPNG();