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
const machineSelection = 'format machine';
const machineAck = 'FORMAT MACHINE\r\nOK\r\n';
const machineTerminal = /(?:^|\r\n)(?:OK|ERR (?:INVALID|CAPACITY|UNAVAILABLE))\r\n$/;
const routeRecord = /^ROUTE [1-9]\d* [1-9]\d* (?:MANAGED|RAW) (?:OFF|PENDING|ACTIVE)$/;
const routingHeader = /^ROUTING ([1-9]\d*) (-|0|[1-9]\d*)$/;
const routingDevice = /^DEVICE [1-9]\d* (?:connected|disconnected|ambiguous) (?:unset|#[0-9A-F]{6}) "(?:[^"\\\x00-\x1f\x7f]|\\["\\])*"$/;
const routingBinding = /^BINDING [1-9]\d* [1-9]\d* (?:in|out) (?:managed|raw|panel) (?:free|in-use|unavailable) "(?:[^"\\\x00-\x1f\x7f]|\\["\\])*"$/;

async function waitFor(get, what) {
  const deadline = Date.now() + 2500;
  while (Date.now() < deadline) {
    const value = await get();
    if (value) return value;
    await delay(5);
  }
  throw new Error('Timed out: ' + what);
}

function machineRecords(line, reply, last) {
  const records = reply.split('\r\n');
  assert.equal(records.pop(), '', reply);
  assert.equal(records.pop(), last, reply);
  if (last !== 'OK') {
    assert.deepEqual(records, [], reply);
    return records;
  }
  const command = line.split(' ', 1)[0];
  assert.ok(records.length > 0 && records.every(record => !/[\r\n]/.test(record)), reply);
  if (command === 'routing') {
    assert.match(records[0], routingHeader);
    assert.ok(records.slice(1).every(record => routingDevice.test(record) ||
      routingBinding.test(record) || routeRecord.test(record)), reply);
  } else if (command === 'help') {
    if (line !== 'help') assert.equal(records.length, 1, reply);
    assert.ok(records.every(record => /^COMMAND [\x20-\x7e]+$/.test(record)), reply);
  } else {
    assert.equal(records.length, 1, reply);
    if (command === 'route') assert.match(records[0], routeRecord);
    else {
      assert.equal(command, 'transform');
      assert.match(records[0], /^TRANSFORM [1-9]\d* [1-9]\d* (?:0|-?[1-9]\d*) (?:0|[1-9]\d*) [1-9]\d*$/);
    }
  }
  return records;
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
    const serial = await connect('/serial', bytes => { rx += bytes.toString('latin1'); });
    await waitFor(() => rx.length >= 2, 'serial attach prompt');
    assert.equal(rx, '> ');
    rx = '';
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
    async function exchange(line) {
      assert.equal(rx, '', 'unsolicited serial bytes');
      serial.send(Buffer.from(line + '\n'));
      await waitFor(() => {
        assert.equal(serial.readyState, WebSocket.OPEN);
        assert.ok(rx.length <= 4096, 'oversized machine reply');
        return machineTerminal.test(rx);
      }, 'response for ' + line);
      const reply = rx;
      rx = '';
      return reply;
    }
    assert.equal(await exchange(machineSelection), machineAck);
    async function command(line, last = 'OK') {
      const lines = machineRecords(line, await exchange(line), last);
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

async function directSerial(profile, enabled) {
  const child = spawn(runner, ['--web-ui', '--profile', profile, '--recorder']);
  const closed = new Promise(resolve => child.once('close', resolve));
  let wire = '';
  let stderr = '';
  let failure;
  child.stdout.on('data', bytes => { wire += bytes.toString('latin1'); });
  child.stderr.on('data', bytes => { stderr += bytes; });
  child.on('error', error => { failure = error; });
  child.stdin.on('error', error => { failure = error; });
  async function readWire(complete, what) {
    await waitFor(() => {
      assert.ifError(failure);
      assert.ok(wire.length <= 16384, 'oversized private reply');
      return complete.test(wire);
    }, what);
    const lines = wire.split('\n');
    wire = '';
    assert.equal(lines.pop(), '');
    return lines;
  }
  async function dispatch(line) {
    assert.equal(wire, '', 'unsolicited private bytes');
    child.stdin.write(line + '\n');
    const lines = await readWire(/(?:^|\n)(?:ok|err (?:INVALID|CAPACITY|UNAVAILABLE))\n$/, line);
    assert.equal(lines.pop(), 'ok', line);
    return lines;
  }
  function serialBytes(lines) {
    return lines.map(line => {
      const match = /^serial 1 ((?:[0-9A-F]{2})+)$/.exec(line);
      assert.ok(match, line);
      return Buffer.from(match[1], 'hex').toString('latin1');
    }).join('');
  }
  async function exchange(line) {
    return serialBytes(await dispatch('serial 1 ' + Buffer.from(line + '\n').toString('hex')));
  }
  async function command(line, last = 'OK') {
    return machineRecords(line, await exchange(line), last);
  }
  try {
    const ready = await readWire(/\n$/, 'private Runner readiness');
    assert.equal(ready.length, 1);
    assert.match(ready[0], /^ready [1-9]\d*$/);
    for (const [port, direction] of [[1, 'in'], [5, 'out'], [12, 'out']]) {
      const lines = await dispatch('bind ' + port + ' 2');
      assert.equal(lines.length, 1);
      assert.match(lines[0], new RegExp('^bound ' + port + ' 2 ' + direction + ' [1-9]\\d*$'));
    }
    assert.equal(serialBytes(await dispatch('serial-bind 1')), '> ');
    assert.equal(await exchange(machineSelection), machineAck);

    const help = await command('help transform', enabled ? 'OK' : 'ERR UNAVAILABLE');
    if (enabled) assert.match(help[0], /^COMMAND transform /);
    assert.deepEqual(await command('route 1 12 managed'), ['ROUTE 1 12 MANAGED ACTIVE']);
    assert.deepEqual(await command('route 1 5 managed'), ['ROUTE 1 5 MANAGED ACTIVE']);
    assert.equal((await command('routing'))[0], 'ROUTING 1 8');
    await command('transform 1 12 7 4 if 1', 'ERR UNAVAILABLE');
    const transform = await command('transform 1 5 -5 0 if 1', enabled ? 'OK' : 'ERR UNAVAILABLE');
    if (enabled) assert.deepEqual(transform, ['TRANSFORM 1 5 -5 0 2']);
  } finally {
    child.stdin.end();
    const timer = setTimeout(() => child.kill('SIGKILL'), 2500);
    await closed;
    clearTimeout(timer);
  }
  assert.ifError(failure);
  assert.equal(child.exitCode, 0, stderr);
  assert.equal(stderr, '');
  assert.equal(wire, '', 'unsolicited private bytes after EOF');
  console.log('PASS direct serial profile ' + path.basename(profile) + ', transform ' +
              (enabled ? 'available with recorder excluded' : 'unavailable for Active public branch'));
}

async function profileAuthority() {
  await directSerial(desktopProfile, true);
  await directSerial(piProfile, false);
  const directory = mkdtempSync(path.join(tmpdir(), 'gridpico-transform-profile-'));
  try {
    const renamed = path.join(directory, 'renamed.conf');
    const desktopName = path.join(directory, 'desktop.conf');
    copyFileSync(desktopProfile, renamed);
    copyFileSync(piProfile, desktopName);
    await directSerial(renamed, true);
    await directSerial(desktopName, false);

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
    const header = routingHeader.exec(lines.shift());
    assert.ok(header);
    if (revision !== undefined) assert.equal(header[1], revision);
    revision = header[1];
    offset = header[2];
    rows.push(...lines);
  }
  return { revision, rows };
}

async function run() {
  await profileAuthority();
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
