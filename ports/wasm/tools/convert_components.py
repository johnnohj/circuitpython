#!/usr/bin/env python3
"""
Convert Wippersnapper component definitions to CircuitPython WASM format.

Usage:
    python3 tools/convert_components.py [--ws-dir /tmp/wsc] [--bundle /tmp/bundle.json] [--out boards/components]

Reads Wippersnapper I2C component definitions, cross-references with the
CircuitPython bundle metadata JSON, and writes enriched definitions to
boards/components/{name}/definition.json in our schema.

Data sources:
    - Wippersnapper: I2C addresses, vendor, subcomponents, product URLs
    - Bundle JSON: repo URL, PyPI name, dependencies, version, package flag
    - sensors.json: sensor type display names, default periods
    - Manual mapping: WS component name → bundle library name (for family chips)
"""

import json
import os
import sys
import argparse

# ── Manual mapping: WS component name → bundle library key ──
# For chips where the WS directory name doesn't match the library name
# (e.g., aht20 uses adafruit_ahtx0, bmp390 uses adafruit_bmp3xx)
MANUAL_MAP = {
    'aht20': 'adafruit_ahtx0', 'aht21': 'adafruit_ahtx0',
    'aht30': 'adafruit_ahtx0', 'am2301b': 'adafruit_ahtx0',
    'am2315c': 'adafruit_ahtx0', 'dht20': 'adafruit_ahtx0',
    'bmp388': 'adafruit_bmp3xx', 'bmp390': 'adafruit_bmp3xx',
    'bmp580': 'adafruit_bmp581', 'bmp585': 'adafruit_bmp581',
    'bme688': 'adafruit_bme680',
    'ens161': 'adafruit_ens160',
    'sht30_shell': 'adafruit_sht31d', 'sht31': 'adafruit_sht31d',
    'sht35': 'adafruit_sht31d',
    'sht40': 'adafruit_sht4x', 'sht41': 'adafruit_sht4x',
    'sht45': 'adafruit_sht4x',
    'lis3dh': 'adafruit_lis3dh',
    'lsm6ds': 'adafruit_lsm6ds',
    'lsm303': 'adafruit_lsm303_accel',
    'mcp9808': 'adafruit_mcp9808',
    'sgp41': 'adafruit_sgp40',
    'lps25hb': 'adafruit_lps2x',
}

# ── Sensor type metadata (ranges, units, defaults) ──
# Enriches the bare WS subcomponent slugs with simulation-useful data
SENSOR_DEFAULTS = {
    'ambient-temp':            {'unit': '°C',   'range': {'min': -40, 'max': 85},    'defaultValue': 22,      'widget': 'slider'},
    'ambient-temp-fahrenheit': {'unit': '°F',   'range': {'min': -40, 'max': 185},   'defaultValue': 72,      'widget': 'slider'},
    'humidity':                {'unit': '%RH',  'range': {'min': 0, 'max': 100},     'defaultValue': 45,      'widget': 'slider'},
    'pressure':                {'unit': 'hPa',  'range': {'min': 300, 'max': 1100},  'defaultValue': 1013.25, 'widget': 'gauge'},
    'altitude':                {'unit': 'm',    'range': {'min': -500, 'max': 9000}, 'defaultValue': 0,       'widget': 'gauge'},
    'light':                   {'unit': 'lux',  'range': {'min': 0, 'max': 65535},   'defaultValue': 300,     'widget': 'slider'},
    'proximity':               {'unit': '',     'range': {'min': 0, 'max': 255},     'defaultValue': 0,       'widget': 'slider'},
    'accel-x':                 {'unit': 'm/s²', 'range': {'min': -78, 'max': 78},    'defaultValue': 0,       'widget': 'slider'},
    'accel-y':                 {'unit': 'm/s²', 'range': {'min': -78, 'max': 78},    'defaultValue': 0,       'widget': 'slider'},
    'accel-z':                 {'unit': 'm/s²', 'range': {'min': -78, 'max': 78},    'defaultValue': 9.8,     'widget': 'slider'},
    'gyro-x':                  {'unit': '°/s',  'range': {'min': -2000, 'max': 2000},'defaultValue': 0,       'widget': 'slider'},
    'gyro-y':                  {'unit': '°/s',  'range': {'min': -2000, 'max': 2000},'defaultValue': 0,       'widget': 'slider'},
    'gyro-z':                  {'unit': '°/s',  'range': {'min': -2000, 'max': 2000},'defaultValue': 0,       'widget': 'slider'},
    'mag-x':                   {'unit': 'µT',   'range': {'min': -4900, 'max': 4900},'defaultValue': 0,       'widget': 'gauge'},
    'mag-y':                   {'unit': 'µT',   'range': {'min': -4900, 'max': 4900},'defaultValue': 0,       'widget': 'gauge'},
    'mag-z':                   {'unit': 'µT',   'range': {'min': -4900, 'max': 4900},'defaultValue': 0,       'widget': 'gauge'},
    'gas-resistance':          {'unit': 'Ω',    'range': {'min': 0, 'max': 1000000}, 'defaultValue': 50000,   'widget': 'gauge'},
    'co2':                     {'unit': 'ppm',  'range': {'min': 400, 'max': 8192},  'defaultValue': 400,     'widget': 'gauge'},
    'eco2':                    {'unit': 'ppm',  'range': {'min': 400, 'max': 8192},  'defaultValue': 400,     'widget': 'gauge'},
    'tvoc':                    {'unit': 'ppb',  'range': {'min': 0, 'max': 1187},    'defaultValue': 0,       'widget': 'gauge'},
    'voltage':                 {'unit': 'V',    'range': {'min': 0, 'max': 36},      'defaultValue': 3.3,     'widget': 'gauge'},
    'current':                 {'unit': 'mA',   'range': {'min': -3200, 'max': 3200},'defaultValue': 0,       'widget': 'gauge'},
    'distance':                {'unit': 'mm',   'range': {'min': 0, 'max': 4000},    'defaultValue': 100,     'widget': 'slider'},
    'raw':                     {'unit': '',     'range': {'min': 0, 'max': 65535},   'defaultValue': 0,       'widget': 'slider'},
    'rotation':                {'unit': '°',    'range': {'min': 0, 'max': 360},     'defaultValue': 0,       'widget': 'slider'},
    'color':                   {'unit': '',     'range': {'min': 0, 'max': 65535},   'defaultValue': 0,       'widget': 'color_picker'},
    'soil-moisture':           {'unit': '',     'range': {'min': 0, 'max': 65535},   'defaultValue': 500,     'widget': 'slider'},
    'pm25':                    {'unit': 'µg/m³','range': {'min': 0, 'max': 500},     'defaultValue': 10,      'widget': 'gauge'},
    'pm10':                    {'unit': 'µg/m³','range': {'min': 0, 'max': 500},     'defaultValue': 15,      'widget': 'gauge'},
    'sound-level':             {'unit': 'dB',   'range': {'min': 0, 'max': 120},     'defaultValue': 40,      'widget': 'gauge'},
}

# Visual defaults by primary sensor type
VISUAL_DEFAULTS = {
    'ambient-temp': {'icon': 'thermometer', 'color': '#e74c3c'},
    'humidity':     {'icon': 'droplet',     'color': '#3498db'},
    'pressure':     {'icon': 'barometer',   'color': '#9b59b6'},
    'light':        {'icon': 'sun',         'color': '#f1c40f'},
    'proximity':    {'icon': 'radar',       'color': '#2ecc71'},
    'accel-x':      {'icon': 'accelerometer','color': '#e67e22'},
    'gas-resistance':{'icon': 'wind',       'color': '#1abc9c'},
    'voltage':      {'icon': 'bolt',        'color': '#e74c3c'},
    'distance':     {'icon': 'ruler',       'color': '#3498db'},
    'raw':          {'icon': 'chip',        'color': '#95a5a6'},
}


def find_bundle_key(ws_name, bundle):
    """Find the bundle library key for a WS component name."""
    # Check manual mapping first
    if ws_name in MANUAL_MAP:
        key = MANUAL_MAP[ws_name]
        if key in bundle:
            return key

    # Direct match
    candidate = f'adafruit_{ws_name}'
    if candidate in bundle:
        return candidate

    # Try without underscores
    candidate = f'adafruit_{ws_name.replace("_", "")}'
    if candidate in bundle:
        return candidate

    return None


def parse_subcomponents(ws_def, sensors_json):
    """Convert WS subcomponents to our sensor array format."""
    sensors = []
    seen = set()

    for sub in ws_def.get('subcomponents', []):
        if isinstance(sub, str):
            slug = sub
            display_name = sensors_json.get(slug, {}).get('displayName', slug)
            period = sensors_json.get(slug, {}).get('defaultPeriod', 900) / 1000
        elif isinstance(sub, dict):
            slug = sub.get('sensorType', 'raw')
            display_name = sub.get('displayName', slug)
            period = sub.get('defaultPeriod', 900) / 1000
        else:
            continue

        if slug in seen:
            continue
        seen.add(slug)

        defaults = SENSOR_DEFAULTS.get(slug, {
            'unit': '', 'range': {'min': 0, 'max': 100},
            'defaultValue': 0, 'widget': 'slider'
        })

        sensors.append({
            'type': slug,
            'displayName': display_name,
            'unit': defaults['unit'],
            'range': defaults['range'],
            'defaultValue': defaults['defaultValue'],
            'defaultPeriod': period,
        })

    return sensors


# ── WS type → our type mapping ──
WS_TYPE_MAP = {
    'i2c': 'i2c',
    'i2c_output': 'i2c',  # I2C displays → still I2C type
    'pin': 'digital',      # default; refined below per component
    'pixel': 'neopixel',
    'pwm': 'analog',       # PWM outputs
    'servo': 'analog',
    'display': 'display',
    'uart': 'uart',
    'ds18x20': 'digital',  # 1-wire sensors
}

# ── Pin component → our type refinement ──
PIN_TYPE_OVERRIDES = {
    'analog_pin': 'analog',
    'potentiometer': 'analog',
    'etape_liquid_level_sensor': 'analog',
    'simple_soil_sensor': 'analog',
    'light_sensor': 'analog',
    'water_sensor': 'analog',
}


def convert_component(ws_name, ws_def, ws_type, bundle, sensors_json):
    """Convert one WS component to our schema."""
    bundle_key = find_bundle_key(ws_name, bundle)
    lib_info = bundle.get(bundle_key, {}) if bundle_key else {}

    # Determine our component type
    comp_type = WS_TYPE_MAP.get(ws_type, ws_type)
    if ws_type == 'pin':
        comp_type = PIN_TYPE_OVERRIDES.get(ws_name, 'digital')
    if ws_type == 'pixel' and 'dotstar' in ws_name:
        comp_type = 'neopixel'  # treat DotStar same as NeoPixel for our purposes

    # Parse I2C addresses (hex strings → integers)
    addrs = []
    for a in ws_def.get('i2cAddresses', []):
        if isinstance(a, str):
            addrs.append(int(a, 16))
        else:
            addrs.append(int(a))

    sensors = parse_subcomponents(ws_def, sensors_json)
    primary_type = sensors[0]['type'] if sensors else 'raw'

    # Build driver info from bundle
    driver = {}
    if lib_info:
        driver['package'] = lib_info.get('pypi_name', '').replace(
            'adafruit-circuitpython-', 'adafruit_').replace('-', '_')
        driver['repo'] = lib_info.get('repo', '')
        path = lib_info.get('path', '')
        if lib_info.get('package'):
            driver['importName'] = os.path.basename(path) + '.basic' if 'bme' in ws_name or 'bmp' in ws_name else os.path.basename(path)
        else:
            driver['importName'] = os.path.basename(path)

    # Build visual
    vis_defaults = {
        'digital': {'icon': 'toggle', 'color': '#2ecc71', 'widget': 'toggle'},
        'analog':  {'icon': 'slider', 'color': '#3498db', 'widget': 'slider'},
        'neopixel':{'icon': 'led_strip', 'color': '#9b59b6', 'widget': 'led_strip'},
        'display': {'icon': 'display', 'color': '#2ecc71', 'widget': 'text'},
        'uart':    {'icon': 'serial', 'color': '#95a5a6', 'widget': 'text'},
    }

    if sensors:
        vis = VISUAL_DEFAULTS.get(primary_type, {'icon': 'chip', 'color': '#95a5a6'})
        vis['widget'] = SENSOR_DEFAULTS.get(primary_type, {}).get('widget', 'gauge')
    else:
        vis = vis_defaults.get(comp_type, {'icon': 'chip', 'color': '#95a5a6', 'widget': 'gauge'})

    result = {
        'componentName': ws_name,
        'displayName': ws_def.get('displayName', ws_name),
        'vendor': ws_def.get('vendor', ''),
        'type': comp_type,
    }

    if ws_def.get('productURL'):
        result['productURL'] = ws_def['productURL']
    if ws_def.get('documentationURL'):
        result['documentationURL'] = ws_def['documentationURL']

    if driver:
        result['driver'] = driver

    # I2C-specific fields
    if addrs:
        result['i2c'] = {
            'addresses': addrs,
            'defaultAddress': addrs[0] if addrs else 0,
            'registers': [],
        }

    # NeoPixel/DotStar specific
    if ws_type == 'pixel':
        pixel_info = ws_def.get('pixelCount', ws_def.get('count'))
        if pixel_info:
            result['pixel'] = {'count': pixel_info}

    if sensors:
        result['sensors'] = sensors

    result['visual'] = vis

    return result


def main():
    parser = argparse.ArgumentParser(description='Convert Wippersnapper components')
    parser.add_argument('--ws-dir', default='/tmp/wsc', help='Wippersnapper repo root')
    parser.add_argument('--bundle', default='/tmp/bundle.json', help='Bundle metadata JSON')
    parser.add_argument('--out', default='boards/components', help='Output directory')
    parser.add_argument('--dry-run', action='store_true', help='Print without writing')
    args = parser.parse_args()

    bundle = json.load(open(args.bundle))
    sensors_json = json.load(open(os.path.join(args.ws_dir, 'components', 'sensors.json')))

    # All WS component type directories
    ws_types = ['i2c', 'i2c_output', 'pin', 'pixel', 'pwm', 'servo',
                'display', 'uart', 'ds18x20']

    converted = 0
    skipped = 0

    for ws_type in ws_types:
        ws_type_dir = os.path.join(args.ws_dir, 'components', ws_type)
        if not os.path.isdir(ws_type_dir):
            continue

        type_count = 0
        for name in sorted(os.listdir(ws_type_dir)):
            defn_path = os.path.join(ws_type_dir, name, 'definition.json')
            if not os.path.isfile(defn_path):
                continue

            ws_def = json.load(open(defn_path))

            # Skip unpublished
            if not ws_def.get('published', True):
                skipped += 1
                continue

            result = convert_component(name, ws_def, ws_type, bundle, sensors_json)

            if args.dry_run:
                print(f'\n=== {ws_type}/{name} ===')
                print(json.dumps(result, indent=2))
            else:
                out_dir = os.path.join(args.out, name)
                os.makedirs(out_dir, exist_ok=True)
                out_path = os.path.join(out_dir, 'definition.json')

                # Don't overwrite existing definitions that may have
                # hand-crafted register maps
                if os.path.exists(out_path):
                    print(f'  SKIP {ws_type}/{name} (already exists)')
                    skipped += 1
                    continue

                with open(out_path, 'w') as f:
                    json.dump(result, f, indent=2)
                    f.write('\n')
                print(f'  OK   {ws_type}/{name} → {out_path}')
                converted += 1
                type_count += 1

        if type_count > 0:
            print(f'  --- {ws_type}: {type_count} converted ---')

    print(f'\nTotal converted: {converted}, Skipped: {skipped}')


if __name__ == '__main__':
    main()
