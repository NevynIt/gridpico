'use strict';

const assert = require('node:assert/strict');
const { spawn, spawnSync } = require('node:child_process');
const { copyFileSync, mkdtempSync, readFileSync, rmSync, writeFileSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');
const WebSocket = require('../connections/node_modules/ws');

const root = path.resolve(__dirname, '..');
const desktopProfile = path.join(root, 'app/profiles/desktop.conf');
const piProfile = path.join(root, 'app/profiles/pi.conf');
const host = path.join(root, 'connections/local-host.js');
const runner = process.argv[2] ? path.resolve(process.argv[2]) :
  path.join(root, 'build/portable-transform/gridpico_local_router');

async function waitFor(get, what) {
  const deadline = Date.now() + 2500;
  while (Date.now() < deadline) {
    const value = await get();
    if (value) return value;
    await delay(5);
  }
  throw new Error('Timed out: ' + what);
}

async function withHost({ profile, webUi = false }, run) {
  const args = [host];
  if (webUi) args.push('--web-ui');
  if (profile) args.push('--profile', profile);
  args.push(runner, '0');
  const child = spawn(process.execPath, args,
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
    if (profile) {
      for (const [requestOrigin, status] of [['http://example.invalid', 403], [origin, 503]]) {
        const rejected = new WebSocket(origin.replace('http:', 'ws:') + '/serial',
                                       { origin: requestOrigin, handshakeTimeout: 2500 });
        sockets.push(rejected);
        await assert.rejects(new Promise((resolve, reject) => {
          rejected.once('open', resolve);
          rejected.once('error', reject);
        }), new RegExp('Unexpected server response: ' + status));
      }
    }
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

function directSerial(profile, enabled) {
  const commands = ['help transform', 'route 1 12 managed', 'route 1 5 managed', 'routing',
                    'transform 1 12 7 4 if 1',
                    'transform 1 5 -5 0 if 1'];
  const input = ['bind 1 2', 'bind 5 2', 'bind 12 2', 'serial-bind 1',
                 ...commands.map(line => 'serial 1 ' + Buffer.from(line + '\n').toString('hex'))];
  const result = spawnSync(runner, ['--web-ui', '--profile', profile, '--recorder'],
                           { input: input.join('\n') + '\n', encoding: 'utf8', timeout: 2500 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stderr, '');
  const serial = [...result.stdout.matchAll(/^serial 1 ([\da-f]+)$/gmi)]
    .map(([, hex]) => Buffer.from(hex, 'hex').toString('ascii')).join('');
  assert.match(serial, /ROUTE 1 12 MANAGED ACTIVE\r\nOK/);
  assert.match(serial, /ROUTE 1 5 MANAGED ACTIVE\r\nOK/);
  assert.match(serial, /ROUTING 1 8\r\n/);
  assert.match(serial, /transform 1 12 7 4 if 1 *\r\nERR UNAVAILABLE/);
  if (enabled) {
    assert.match(serial, /COMMAND transform /);
    assert.match(serial, /TRANSFORM 1 5 -5 0 2\r\nOK/);
  } else {
    assert.match(serial, /help transform *\r\nERR UNAVAILABLE/);
    assert.match(serial, /transform 1 5 -5 0 if 1 *\r\nERR UNAVAILABLE/);
    assert.doesNotMatch(serial, /COMMAND transform |TRANSFORM /);
  }
  console.log('PASS direct serial profile ' + path.basename(profile) + ', transform ' +
              (enabled ? 'available with recorder excluded' : 'unavailable for Active public branch'));
}

function profileAuthority() {
  directSerial(desktopProfile, true);
  directSerial(piProfile, false);
  const directory = mkdtempSync(path.join(tmpdir(), 'gridpico-transform-profile-'));
  try {
    const renamed = path.join(directory, 'renamed.conf');
    const desktopName = path.join(directory, 'desktop.conf');
    copyFileSync(desktopProfile, renamed);
    copyFileSync(piProfile, desktopName);
    directSerial(renamed, true);
    directSerial(desktopName, false);

    const profile = readFileSync(piProfile, 'utf8');
    const invalid = path.join(directory, 'invalid.conf');
    for (const rows of ['note-transform on\nnote-transform on', 'note-transform',
                        'note-transform off', 'note-transform on extra']) {
      writeFileSync(invalid, profile + '\n' + rows + '\n');
      const result = spawnSync(runner, ['--profile', invalid],
                               { input: '', encoding: 'utf8', timeout: 2500 });
      assert.ifError(result.error);
      assert.equal(result.status, 1);
      assert.equal(result.stdout, '');
      assert.match(result.stderr, /invalid or duplicate router profile row/);
    }
    console.log('PASS duplicate and malformed note-transform rows fail profile validation');
  } finally {
    rmSync(directory, { recursive: true });
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
  profileAuthority();
  async function expectNoTransform({ command }) {
    assert.ok(!(await command('help')).some(line => line.startsWith('COMMAND transform ')));
    await command('help transform', 'ERR UNAVAILABLE');
    await command('transform 1 12 7 4 if 1', 'ERR UNAVAILABLE');
  }
  await withHost({}, expectNoTransform);
  await withHost({ webUi: true }, expectNoTransform);
  await withHost({ profile: piProfile, webUi: true }, async ({ command, connect }) => {
    await expectNoTransform({ command });
    await connect('/midi/1');
    await connect('/midi/5');
    assert.deepEqual(await command('route 1 5 managed'), ['ROUTE 1 5 MANAGED ACTIVE']);
    const current = await routing(command);
    await command('transform 1 5 7 4 if ' + current.revision, 'ERR UNAVAILABLE');
    console.log('PASS configured Pi profile has an Active route and no transform over stock /serial');
  });
  await withHost({ profile: desktopProfile }, async ({ command }) => {
    assert.match((await command('help transform'))[0], /^COMMAND transform /);
    assert.deepEqual(await command('route 1 5 managed'), ['ROUTE 1 5 MANAGED OFF']);
  });

  await withHost({ profile: desktopProfile, webUi: true }, async ({ command, connect }) => {
    assert.match((await command('help transform'))[0], /^COMMAND transform /);
    assert.deepEqual(await command('route 1 12 managed on'), ['ROUTE 1 12 MANAGED PENDING']);
    const pending = await routing(command);
    await command('transform 1 12 7 4 if ' + pending.revision, 'ERR UNAVAILABLE');

    const transformed = [];
    const comparison = [];
    const control = [];
    const midi = await connect('/midi/1');
    function receiveTransformed(bytes) {
      transformed.push(bytes.toString('hex'));
      console.log('WS /midi/12 ' + transformed.at(-1));
    }
    const receiver = await connect('/midi/12', receiveTransformed);
    await connect('/midi/6', bytes => {
      comparison.push(bytes.toString('hex'));
      console.log('WS /midi/6 ' + comparison.at(-1));
    });
    await connect('/midi/5', bytes => {
      control.push(bytes.toString('hex'));
      console.log('WS /midi/5 ' + control.at(-1));
    });
    async function frames(expectedTransform, expectedComparison, expectedControl = expectedComparison) {
      await waitFor(() => transformed.length >= expectedTransform.length &&
                          comparison.length >= expectedComparison.length &&
                          control.length >= expectedControl.length, 'exact MIDI receipts');
      await delay(100);
      assert.deepEqual(transformed.splice(0), expectedTransform);
      assert.deepEqual(comparison.splice(0), expectedComparison);
      assert.deepEqual(control.splice(0), expectedControl);
    }
    const sendMidi = hex => midi.send(Buffer.from(hex, 'hex'));
    assert.deepEqual(await command('route 1 6 managed on'), ['ROUTE 1 6 MANAGED ACTIVE']);
    const active = await routing(command);
    for (const record of [
      'BINDING 1 1 in managed in-use "Keybed"',
      'BINDING 5 3 out managed in-use "Monitor"',
      'BINDING 6 4 out managed in-use "Log"',
      'BINDING 12 7 out managed in-use "A1"',
      'ROUTE 1 5 MANAGED ACTIVE',
      'ROUTE 1 6 MANAGED ACTIVE',
      'ROUTE 1 12 MANAGED ACTIVE'
    ]) assert.ok(active.rows.includes(record), record);
    await frames([], []);
    sendMidi('803711');
    await frames(['803711'], ['803711']);
    sendMidi('903800');
    await frames(['803800'], ['803800']);

    sendMidi('903c64');
    await frames(['903c64'], ['903c64']);
    await command('transform 1 13 7 4 if ' + active.revision, 'ERR UNAVAILABLE');
    await command('transform 12 1 7 4 if ' + active.revision, 'ERR UNAVAILABLE');
    await command('transform 4 3 7 4 if ' + active.revision, 'ERR UNAVAILABLE');
    await command('transform 1 12 -128 4 if ' + active.revision, 'ERR INVALID');
    await command('transform 1 12 0 17 if ' + active.revision, 'ERR INVALID');
    await frames([], []);

    const switched = BigInt(active.revision) + 1n;
    assert.deepEqual(await command('transform 1 12 7 4 if ' + active.revision),
                     ['TRANSFORM 1 12 7 4 ' + switched]);
    await frames([], []);
    assert.equal((await routing(command)).revision, String(switched));
    await command('transform 1 12 0 0 if ' + active.revision, 'ERR UNAVAILABLE');
    assert.deepEqual(await command('transform 1 12 7 4 if ' + switched),
                     ['TRANSFORM 1 12 7 4 ' + switched]);
    await frames([], []);
    sendMidi('803c27');
    await frames(['834327'], ['803c27']);

    sendMidi('903d62');
    await frames(['934462'], ['903d62']);
    sendMidi('803d1e');
    await frames(['83441e'], ['803d1e']);
    sendMidi('903d62');
    await frames(['934462'], ['903d62']);
    const identity = switched + 1n;
    assert.deepEqual(await command('transform 1 12 0 0 if ' + switched),
                     ['TRANSFORM 1 12 0 0 ' + identity]);
    await frames([], []);
    assert.equal((await routing(command)).revision, String(identity));
    sendMidi('803d13');
    await frames(['803d13'], ['803d13']);
    sendMidi('92412a');
    await frames(['92412a'], ['92412a']);
    sendMidi('824118');
    await frames(['824118'], ['824118']);

    sendMidi('92412a');
    await frames(['92412a'], ['92412a']);
    const second = identity + 1n;
    assert.deepEqual(await command('transform 1 6 -12 0 if ' + identity),
                     ['TRANSFORM 1 6 -12 0 ' + second]);
    await frames([], []);
    assert.equal((await routing(command)).revision, String(second));
    sendMidi('824118');
    await frames(['824118'], ['823518'], ['824118']);
    sendMidi('92412a');
    await frames(['92412a'], ['92352a'], ['92412a']);
    sendMidi('824118');
    await frames(['824118'], ['823518'], ['824118']);

    sendMidi('903f65');
    await frames(['903f65'], ['903365'], ['903f65']);
    const beforeOff = await routing(command);
    assert.deepEqual(await command('route 1 6 managed off if ' + beforeOff.revision),
                     ['ROUTE 1 6 MANAGED OFF']);
    await frames([], []);
    sendMidi('803f30');
    await frames(['803f30'], [], ['803f30']);
    const off = await routing(command);
    await command('transform 1 6 7 4 if ' + off.revision, 'ERR UNAVAILABLE');
    assert.deepEqual(await command('route 1 6 managed on if ' + off.revision),
                     ['ROUTE 1 6 MANAGED ACTIVE']);
    sendMidi('92412a');
    await frames(['92412a'], ['92412a']);
    sendMidi('824118');
    await frames(['824118'], ['824118']);

    const beforeDetach = await routing(command);
    await command('transform 1 12 7 4 if ' + beforeDetach.revision);
    sendMidi('903d62');
    await frames(['934462'], ['903d62']);
    receiver.close(1000);
    await waitFor(() => receiver.readyState === WebSocket.CLOSED, 'selected output close');
    await waitFor(async () => (await command('route 1 12 managed'))[0] ===
                            'ROUTE 1 12 MANAGED PENDING', 'selected route Pending');
    await connect('/midi/12', receiveTransformed);
    await frames([], []);
    sendMidi('803d1e');
    await frames(['83441e'], ['803d1e']);
    sendMidi('903d62');
    await frames(['934462'], ['903d62']);
    sendMidi('803d1e');
    await frames(['83441e'], ['803d1e']);
    const other = await connect('/midi/7');
    const beforeFanIn = await routing(command);
    assert.deepEqual(await command('route 7 5 managed on if ' + beforeFanIn.revision),
                     ['ROUTE 7 5 MANAGED ACTIVE']);
    sendMidi('903e63');
    await frames(['934563'], ['903e63']);
    other.send(Buffer.from('903e71', 'hex'));
    await frames([], [], ['903e71']);
    sendMidi('803e11');
    await frames(['834511'], ['803e11']);
    other.send(Buffer.from('803e22', 'hex'));
    await frames([], [], ['803e22']);
    console.log('PASS current-message transforms, independent fan-in, Off and reattach without generated MIDI');
  });
}

run().catch(error => { console.error(error); process.exitCode = 1; });
