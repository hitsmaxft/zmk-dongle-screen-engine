#!/usr/bin/env node
/* Render the real WASM RGB565 framebuffer directly to PNG; no browser involved. */
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const zlib = require('zlib');

function usage(message) {
  if (message) console.error(message);
  console.error(`usage: render_wasm.cjs PREVIEW_DIR --output FRAME.png [options]

options:
  --time MS                 absolute render timestamp (default: 0)
  --frames N --fps N        render a deterministic PNG sequence (defaults: 1, 24)
  --width PX --height PX    logical framebuffer size (default: 280x240)
  --wpm N --layer N         simulated keyboard state (defaults: 72, 0)
  --layer-name TEXT         layer label written through dte_name_buffer
  --endpoint N --mods N     endpoint and modifier mask (defaults: 2, 0)
  --battery-count N         2 or 3 battery layout (default: 3)
  --dongle-battery N        battery percentage, -1 means unknown (default: 93)
  --split0-battery N        battery percentage (default: 87)
  --split1-battery N        battery percentage (default: 64)
  --connected 0|1           split connection state (default: 1)
  --call NAME=A,B           call an exported integer setter; repeatable
  --gesture KIND@MS         apply a gesture before rendering; repeatable
`);
  process.exit(message ? 2 : 0);
}

function integer(value, name) {
  if (!/^-?\d+$/.test(value)) usage(`${name} expects an integer, got ${value}`);
  return Number(value);
}

function parse(argv) {
  if (!argv.length || argv.includes('--help')) usage();
  const options = {
    folder: path.resolve(argv[0]), output: null, time: 0, frames: 1, fps: 24,
    width: 280, height: 240,
    wpm: 72, layer: 0, layerName: 'BASE', endpoint: 2, mods: 0,
    batteryCount: 3, dongleBattery: 93, split0Battery: 87,
    split1Battery: 64, connected: 1, calls: [], gestures: [],
  };
  const numeric = new Map([
    ['--time', 'time'], ['--frames', 'frames'], ['--fps', 'fps'],
    ['--width', 'width'], ['--height', 'height'],
    ['--wpm', 'wpm'], ['--layer', 'layer'], ['--endpoint', 'endpoint'],
    ['--mods', 'mods'], ['--battery-count', 'batteryCount'],
    ['--dongle-battery', 'dongleBattery'], ['--split0-battery', 'split0Battery'],
    ['--split1-battery', 'split1Battery'], ['--connected', 'connected'],
  ]);
  for (let i = 1; i < argv.length; i++) {
    const flag = argv[i];
    if (flag === '--output' || flag === '--layer-name') {
      if (++i >= argv.length) usage(`${flag} requires a value`);
      options[flag === '--output' ? 'output' : 'layerName'] = argv[i];
    } else if (numeric.has(flag)) {
      if (++i >= argv.length) usage(`${flag} requires a value`);
      options[numeric.get(flag)] = integer(argv[i], flag);
    } else if (flag === '--call') {
      if (++i >= argv.length) usage('--call requires NAME=A,B');
      const match = argv[i].match(/^([A-Za-z_]\w*)=(.*)$/);
      if (!match) usage(`invalid --call: ${argv[i]}`);
      const args = match[2] === '' ? [] : match[2].split(',').map(v => integer(v, '--call'));
      options.calls.push([match[1], args]);
    } else if (flag === '--gesture') {
      if (++i >= argv.length) usage('--gesture requires KIND@MS');
      const match = argv[i].match(/^(-?\d+)@(-?\d+)$/);
      if (!match) usage(`invalid --gesture: ${argv[i]}`);
      options.gestures.push([Number(match[1]), Number(match[2])]);
    } else usage(`unknown option: ${flag}`);
  }
  if (!options.output) usage('--output is required');
  options.output = path.resolve(options.output);
  if (path.extname(options.output).toLowerCase() !== '.png') usage('--output must end in .png');
  if (options.width <= 0 || options.height <= 0) usage('width and height must be positive');
  if (options.frames <= 0 || options.fps <= 0) usage('frames and fps must be positive');
  if (![2, 3].includes(options.batteryCount)) usage('--battery-count must be 2 or 3');
  if (![0, 1].includes(options.connected)) usage('--connected must be 0 or 1');
  return options;
}

const crcTable = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buffer) {
  let c = 0xffffffff;
  for (const byte of buffer) c = crcTable[(c ^ byte) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const name = Buffer.from(type, 'ascii');
  const length = Buffer.alloc(4); length.writeUInt32BE(data.length);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([name, data])));
  return Buffer.concat([length, name, data, crc]);
}

function pngFromRgb565(pixels, width, height) {
  const rows = Buffer.alloc(height * (1 + width * 3));
  for (let y = 0; y < height; y++) {
    let out = y * (1 + width * 3); rows[out++] = 0;
    for (let x = 0; x < width; x++) {
      const pixel = pixels[y * width + x];
      rows[out++] = Math.round((pixel >> 11) * 255 / 31);
      rows[out++] = Math.round(((pixel >> 5) & 63) * 255 / 63);
      rows[out++] = Math.round((pixel & 31) * 255 / 31);
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(rows, {level: 9})),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

function setLayerName(api, value) {
  if (!api.dte_name_buffer || !api.dte_set_layer_name) return;
  const bytes = new Uint8Array(api.memory.buffer, api.dte_name_buffer(), 24);
  bytes.fill(0); bytes.set(new TextEncoder().encode(value).slice(0, 23));
  api.dte_set_layer_name(api.dte_name_buffer());
}

(async () => {
  const options = parse(process.argv.slice(2));
  const wasmPath = path.join(options.folder, 'theme.wasm');
  const {instance} = await WebAssembly.instantiate(fs.readFileSync(wasmPath));
  const api = instance.exports;
  api.dte_init(options.width, options.height);
  api.dte_set_battery_count(options.batteryCount);
  api.dte_set_state(options.wpm, options.layer, options.endpoint, 1, options.mods,
    options.split0Battery, options.split1Battery, options.dongleBattery, 1,
    options.connected);
  setLayerName(api, options.layerName);
  for (const [name, args] of options.calls) {
    if (typeof api[name] !== 'function') throw new Error(`WASM export not found: ${name}`);
    api[name](...args);
  }
  for (const [kind, at] of options.gestures) api.dte_gesture(kind, at);
  fs.mkdirSync(path.dirname(options.output), {recursive: true});
  const extension = path.extname(options.output);
  const stem = options.output.slice(0, -extension.length);
  const hashes = [];
  let firstOutput, lastOutput, width, height, lastPng;
  for (let frame = 0; frame < options.frames; frame++) {
    const time = options.time + Math.round(frame * 1000 / options.fps);
    api.dte_render(time);
    width = api.dte_width(); height = api.dte_height();
    const pixels = new Uint16Array(api.memory.buffer, api.dte_pixels(), width * height);
    lastPng = pngFromRgb565(pixels, width, height);
    const output = options.frames === 1 ? options.output
      : `${stem}-${String(frame).padStart(4, '0')}${extension}`;
    fs.writeFileSync(output, lastPng);
    if (frame === 0) firstOutput = output;
    lastOutput = output;
    hashes.push((api.dte_hash() >>> 0).toString(16).padStart(8, '0'));
  }
  console.log(JSON.stringify({
    output: options.frames === 1 ? firstOutput : `${stem}-NNNN${extension}`,
    first_output: firstOutput, last_output: lastOutput, width, height,
    start_ms: options.time, frames: options.frames, fps: options.fps,
    duration_ms: Math.round(options.frames * 1000 / options.fps),
    first_frame_hash: `0x${hashes[0]}`, last_frame_hash: `0x${hashes.at(-1)}`,
    frame_hashes_sha256: crypto.createHash('sha256').update(hashes.join('\n')).digest('hex'),
    png_sha256: options.frames === 1
      ? crypto.createHash('sha256').update(lastPng).digest('hex') : undefined,
    source: 'theme.wasm RGB565 framebuffer',
  }));
})().catch(error => { console.error(error.stack || error.message); process.exit(1); });
