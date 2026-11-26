'use strict';

const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');
const WebSocket = require('../connections/node_modules/ws');

const root = path.resolve(__dirname, '..');
const profile = path.join(root, 'app/profiles/desktop.conf');
const host = path.join(root, 'connections/local-host.js');

async function waitFor(get, what) {
  const deadline = Date.now() + 2500;
  while (Date.now() < deadline) {
    const value = get();
    if (value) return value;
    await delay(5);
  }
  throw new Error('Timed out: ' + what);
}

async function withHost(runner, run) {
  const child = spawn(process.execPath, [host, '--profile', profile, runner, '0'],
                      { stdio: ['ignore', 'pipe', 'pipe'] });
  const sockets = [];
  let stdout = '';
  let stderr = '';
  child.stdout.on('data', data => { stdout += data; });
  child.stderr.on('data', data => { stderr += data; });
  try {
    const origin = await waitFor(() => /listening at (http:\/\/127\.0\.0\.1:\d+)\//.exec(stdout)?.[1],
                                 'stock loopback listener');
    console.log(stdout.trim());
    async function connect(route, receiver) {
      const ws = new WebSocket(origin.replace('http:', 'ws:') + route, { origin });
      sockets.push(ws);
      ws.on('message', (bytes, binary) => {
        assert.equal(binary, true);
        receiver?.(bytes);
      });
      await new Promise((resolve, reject) => {
        ws.once('open', resolve);
        ws.once('error', reject);
      });
      return ws;
    }
    let rx = '';
    const serial = await connect('/serial', bytes => { rx += bytes.toString('ascii'); });
    await waitFor(() => rx.endsWith('> '), 'serial prompt');
    async function command(line, last = 'OK') {
      rx = '';
      serial.send(Buffer.from(line + '\n'));
      await waitFor(() => rx.endsWith('> '), 'response for ' + line);
      const lines = rx.split('\r\n');
      assert.equal(lines.shift().trimEnd(), line);
      assert.equal(lines.pop(), '> ');
      assert.equal(lines.pop(), last, rx);
      console.log('serial> ' + line + '\n' + lines.concat(last).join('\n'));
      return lines;
    }
    await run({ command, connect });
  } finally {
    sockets.forEach(ws => ws.terminate());
    if (child.exitCode === null) {
      const exited = new Promise(resolve => child.once('exit', resolve));
      child.kill('SIGTERM');
      const timer = setTimeout(() => child.kill('SIGKILL'), 2500);
      await exited;
      clearTimeout(timer);
    }
    assert.equal(child.exitCode, 0, stderr);
    assert.equal(stderr, '');
    console.log('Loopback host and runner stopped cleanly');
  }
}

async function routing(command) {
  let revision;
  let offset = 0;
  const rows = [];
  while (offset !== '-') {
    const lines = await command(offset === 0 ? 'routing' : 'routing ' + revision + ' ' + offset);
    const header = /^ROUTING ([1-9]\d*) (-|\d+)$/.exec(lines.shift());
    assert.ok(header);
    if (revision !== undefined) assert.equal(header[1], revision);
    revision = header[1];
    offset = header[2];
    rows.push(...lines);
  }
  return { revision, rows };
}

async function run() {
  await withHost(path.join(root, 'build/dev-transform/gridpico_local_router'), async ({ command }) => {
    assert.ok(!(await command('help')).some(line => line.startsWith('COMMAND transform ')));
    await command('help transform', 'ERR UNAVAILABLE');
    await command('transform 1 12 7 4 if 1', 'ERR UNAVAILABLE');
  });

  await withHost(path.join(root, 'scripts/dev-transform-runner'), async ({ command, connect }) => {
    assert.match((await command('help transform'))[0], /^COMMAND transform /);
    assert.deepEqual(await command('route 1 12 managed on'), ['ROUTE 1 12 MANAGED PENDING']);
    const pending = await routing(command);
    await command('transform 1 12 7 4 if ' + pending.revision, 'ERR UNAVAILABLE');

    const transformed = [];
    const comparison = [];
    const midi = await connect('/midi/1');
    await connect('/midi/12', bytes => {
      transformed.push(bytes.toString('hex'));
      console.log('WS /midi/12 ' + transformed.at(-1));
    });
    await connect('/midi/6', bytes => {
      comparison.push(bytes.toString('hex'));
      console.log('WS /midi/6 ' + comparison.at(-1));
    });
    async function frames(expectedTransform, expectedComparison) {
      await waitFor(() => transformed.length >= expectedTransform.length &&
                          comparison.length >= expectedComparison.length, 'exact MIDI receipts');
      await delay(100);
      assert.deepEqual(transformed.splice(0), expectedTransform);
      assert.deepEqual(comparison.splice(0), expectedComparison);
    }
    const sendMidi = hex => midi.send(Buffer.from(hex, 'hex'));
    assert.deepEqual(await command('route 1 6 managed on'), ['ROUTE 1 6 MANAGED ACTIVE']);
    const active = await routing(command);
    for (const record of [
      'BINDING 1 1 in managed in-use "Keybed"',
      'BINDING 6 4 out managed in-use "Log"',
      'BINDING 12 7 out managed in-use "A1"',
      'ROUTE 1 6 MANAGED ACTIVE',
      'ROUTE 1 12 MANAGED ACTIVE'
    ]) assert.ok(active.rows.includes(record), record);
    await frames([], []);

    sendMidi('903c64');
    await frames(['903c64'], ['903c64']);
    await command('transform 1 6 7 4 if ' + active.revision, 'ERR UNAVAILABLE');
    await command('transform 1 12 -128 4 if ' + active.revision, 'ERR INVALID');
    await frames([], []);

    const switched = BigInt(active.revision) + 1n;
    assert.deepEqual(await command('transform 1 12 7 4 if ' + active.revision),
                     ['TRANSFORM 1 12 7 4 ' + switched]);
    await frames(['803c00'], []);
    assert.equal((await routing(command)).revision, String(switched));
    await command('transform 1 12 0 0 if ' + active.revision, 'ERR UNAVAILABLE');
    assert.deepEqual(await command('transform 1 12 7 4 if ' + switched),
                     ['TRANSFORM 1 12 7 4 ' + switched]);
    await frames([], []);
    sendMidi('803c27');
    await frames([], ['803c27']);

    sendMidi('903d62');
    await frames(['934462'], ['903d62']);
    sendMidi('803d1e');
    await frames(['83441e'], ['803d1e']);
    sendMidi('903d62');
    await frames(['934462'], ['903d62']);
    const identity = switched + 1n;
    assert.deepEqual(await command('transform 1 12 0 0 if ' + switched),
                     ['TRANSFORM 1 12 0 0 ' + identity]);
    await frames(['834400'], []);
    assert.equal((await routing(command)).revision, String(identity));
    sendMidi('803d13');
    await frames([], ['803d13']);
    sendMidi('92412a');
    await frames(['92412a'], ['92412a']);
    sendMidi('824118');
    await frames(['824118'], ['824118']);
    console.log('PASS exact selected-route transform and identity restoration');
  });
}

run().catch(error => { console.error(error); process.exitCode = 1; });
