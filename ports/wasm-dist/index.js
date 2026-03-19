/*
 * index.js — wasm-dist public entry point
 *
 * Usage (browser or Node):
 *   import { PythonHost } from './index.js';
 *   const python = new PythonHost({ executors: 1 });
 *   await python.init();
 *   const { delta, stdout } = await python.exec('print("hello")');
 */

export { PythonHost }           from './js/PythonHost.js';
export { PythonREPL }           from './js/PythonREPL.js';
export { HardwareSimulator }    from './js/HardwareSimulator.js';
export { SensorSimulator }      from './js/SensorSimulator.js';
export { VirtualBoard, BOARD_STATE } from './js/VirtualBoard.js';
export { BLINKA_SHIMS }         from './js/shims.js';
export { MSG, CHANNEL }         from './js/BroadcastBus.js';
