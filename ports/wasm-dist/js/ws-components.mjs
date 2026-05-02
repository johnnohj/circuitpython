/**
 * ws-components.mjs — load Wippersnapper component catalog
 *
 * Fetches the component database from Adafruit's Wippersnapper
 * Offline Configurator and converts entries to sensor panel
 * definitions compatible with sensor-panel.mjs.
 *
 * Source: https://github.com/adafruit/Adafruit_Wippersnapper_Offline_Configurator
 */

const CATALOG_URL = 'https://raw.githubusercontent.com/adafruit/Adafruit_Wippersnapper_Offline_Configurator/main/wippersnapper_components.json';

// ── Data type → sensor definition mapping ──
// Maps Wippersnapper dataType strings to sensor panel configs.

const DATA_TYPE_MAP = {
    'ambient-temp':            { displayName: 'Temperature',   unit: '°C',    range: { min: -40, max: 85 },     defaultValue: 22 },
    'ambient-temp-fahrenheit': { displayName: 'Temperature',   unit: '°F',    range: { min: -40, max: 185 },    defaultValue: 72 },
    'relative-humidity':       { displayName: 'Humidity',      unit: '%RH',   range: { min: 0, max: 100 },      defaultValue: 45 },
    'pressure':                { displayName: 'Pressure',      unit: 'hPa',   range: { min: 300, max: 1100 },   defaultValue: 1013 },
    'altitude':                { displayName: 'Altitude',      unit: 'm',     range: { min: -500, max: 9000 },  defaultValue: 0 },
    'light':                   { displayName: 'Light',         unit: 'lux',   range: { min: 0, max: 100000 },   defaultValue: 500 },
    'co2':                     { displayName: 'CO₂',           unit: 'ppm',   range: { min: 400, max: 5000 },   defaultValue: 400 },
    'gas-resistance':          { displayName: 'Gas Resistance',unit: 'Ω',     range: { min: 0, max: 500000 },   defaultValue: 50000 },
    'pm10-std':                { displayName: 'PM1.0',         unit: 'µg/m³', range: { min: 0, max: 500 },      defaultValue: 10 },
    'pm25-std':                { displayName: 'PM2.5',         unit: 'µg/m³', range: { min: 0, max: 500 },      defaultValue: 10 },
    'pm100-std':               { displayName: 'PM10',          unit: 'µg/m³', range: { min: 0, max: 500 },      defaultValue: 10 },
    'voltage':                 { displayName: 'Voltage',       unit: 'V',     range: { min: 0, max: 5 },        defaultValue: 3.3 },
    'current':                 { displayName: 'Current',       unit: 'mA',    range: { min: 0, max: 1000 },     defaultValue: 0 },
    'acceleration-x':          { displayName: 'Accel X',       unit: 'm/s²',  range: { min: -160, max: 160 },   defaultValue: 0 },
    'acceleration-y':          { displayName: 'Accel Y',       unit: 'm/s²',  range: { min: -160, max: 160 },   defaultValue: 0 },
    'acceleration-z':          { displayName: 'Accel Z',       unit: 'm/s²',  range: { min: -160, max: 160 },   defaultValue: 9.8 },
    'gyroscope-x':             { displayName: 'Gyro X',        unit: '°/s',   range: { min: -2000, max: 2000 }, defaultValue: 0 },
    'gyroscope-y':             { displayName: 'Gyro Y',        unit: '°/s',   range: { min: -2000, max: 2000 }, defaultValue: 0 },
    'gyroscope-z':             { displayName: 'Gyro Z',        unit: '°/s',   range: { min: -2000, max: 2000 }, defaultValue: 0 },
    'magnetic-x':              { displayName: 'Mag X',         unit: 'µT',    range: { min: -100, max: 100 },   defaultValue: 0 },
    'magnetic-y':              { displayName: 'Mag Y',         unit: 'µT',    range: { min: -100, max: 100 },   defaultValue: 0 },
    'magnetic-z':              { displayName: 'Mag Z',         unit: 'µT',    range: { min: -100, max: 100 },   defaultValue: 0 },
    'proximity':               { displayName: 'Proximity',     unit: '',      range: { min: 0, max: 255 },      defaultValue: 0 },
    'color-r':                 { displayName: 'Red',           unit: '',      range: { min: 0, max: 255 },      defaultValue: 0 },
    'color-g':                 { displayName: 'Green',         unit: '',      range: { min: 0, max: 255 },      defaultValue: 0 },
    'color-b':                 { displayName: 'Blue',          unit: '',      range: { min: 0, max: 255 },      defaultValue: 0 },
    'raw':                     { displayName: 'Raw',           unit: '',      range: { min: 0, max: 65535 },    defaultValue: 0 },
};

/**
 * Load the Wippersnapper component catalog.
 * @param {string} [url] — override catalog URL (for local/cached copy)
 * @returns {Promise<object>} — { components: { category: [...] } }
 */
export async function loadCatalog(url) {
    const resp = await fetch(url || CATALOG_URL);
    return resp.json();
}

/**
 * Convert a WS catalog component to a sensor panel definition.
 * @param {object} wsComponent — entry from wippersnapper_components.json
 * @returns {object|null} — sensor panel compatible definition, or null if no known data types
 */
export function toSensorDef(wsComponent) {
    const sensors = [];
    for (const dt of (wsComponent.dataTypes || [])) {
        const mapped = DATA_TYPE_MAP[dt];
        if (mapped) {
            sensors.push({
                type: dt,
                ...mapped,
                defaultPeriod: 2,
            });
        }
    }
    if (sensors.length === 0) return null;

    return {
        componentName: wsComponent.id || wsComponent.name,
        displayName: wsComponent.displayName || wsComponent.name,
        type: wsComponent.category,
        productURL: wsComponent.productUrl,
        documentationURL: wsComponent.documentationUrl,
        image: wsComponent.image,
        i2c: wsComponent.address ? {
            addresses: (wsComponent.addresses || [wsComponent.address]).map(
                a => typeof a === 'string' ? parseInt(a, 16) : a
            ),
            defaultAddress: typeof wsComponent.address === 'string'
                ? parseInt(wsComponent.address, 16) : wsComponent.address,
        } : undefined,
        sensors,
    };
}

/**
 * Get all I2C sensor components from the catalog.
 * @param {object} catalog — loaded catalog
 * @returns {object[]} — array of sensor panel definitions
 */
export function getI2CSensors(catalog) {
    const components = catalog.components?.i2c || [];
    return components.map(toSensorDef).filter(Boolean);
}

/**
 * Get all component categories and their counts.
 * @param {object} catalog
 * @returns {object} — { category: count }
 */
export function getCategoryCounts(catalog) {
    const counts = {};
    for (const [cat, items] of Object.entries(catalog.components || {})) {
        counts[cat] = items.length;
    }
    return counts;
}

/**
 * Search components by name.
 * @param {object} catalog
 * @param {string} query
 * @returns {object[]} — matching sensor panel definitions
 */
export function searchComponents(catalog, query) {
    const q = query.toLowerCase();
    const results = [];
    for (const items of Object.values(catalog.components || {})) {
        for (const item of items) {
            if ((item.displayName || '').toLowerCase().includes(q) ||
                (item.name || '').toLowerCase().includes(q) ||
                (item.id || '').toLowerCase().includes(q)) {
                const def = toSensorDef(item);
                if (def) results.push(def);
            }
        }
    }
    return results;
}
