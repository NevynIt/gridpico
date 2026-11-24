window.addEventListener("DOMContentLoaded", () => {
  "use strict";

  const cli = window.gridpicoDiagnosticsCli;
  const view = document.querySelector("#routing");
  const link = document.querySelector("#routing-navigation");
  const router = document.querySelector("#routing-router");
  const connect = document.querySelector("#routing-connect");
  const refresh = document.querySelector("#routing-refresh");
  const status = document.querySelector("#routing-status");
  const editStatus = document.querySelector("#routing-edit-status");
  const confirmLabel = document.querySelector("#routing-confirm-label");
  const confirmRouter = document.querySelector("#routing-confirm");
  const results = document.querySelector("#routing-snapshot");
  const revision = document.querySelector("#routing-revision");
  const managedTable = document.querySelector("#routing-managed");
  const rawTable = document.querySelector("#routing-raw");
  const inventoryTable = document.querySelector("#routing-inventory");
  const encoder = new TextEncoder();

  // The CLI projection backing is 8 devices, 18 ports and 13 enabled routes.
  const maxDevices = 8;
  const maxPorts = 18;
  const maxRoutes = 13;
  const maxRecords = maxDevices + maxPorts + maxRoutes;
  const maxRevision = 0xffffffffffffffffn;
  const availabilityLabels = { free: "Free", "in-use": "In use", unavailable: "Unavailable" };
  const routeLabels = { OFF: "Off", PENDING: "Pending", ACTIVE: "Active" };
  const rejections = { INVALID: "Invalid request", CAPACITY: "Capacity limit", UNAVAILABLE: "Unavailable" };

  let snapshot;
  let work;
  let stale = false;
  let initialRead = true;

  function assert(condition) {
    if (!condition) throw new Error("The local CLI returned an incomplete or invalid Routing reply.");
  }

  function cliReply(reply, command) {
    const lines = (cli.loopback ? reply : reply.replace(/^> (?:\r\n)?/, "")).split("\r\n");
    const echo = lines.shift();
    // The terminal can echo the no-argument routing command with one trailing space.
    assert((echo === command || (command === "routing" && echo === "routing ")) && lines.pop() === "> ");
    assert(lines.length > 0 && lines.every(line => encoder.encode(line).length + 2 <= 256));
    if (lines.length === 1 && /^ERR (INVALID|CAPACITY|UNAVAILABLE)$/.test(lines[0])) {
      return { rejected: lines[0].slice(4) };
    }
    assert(lines.pop() === "OK");
    return { lines };
  }

  function nameFrom(text) {
    let name;
    try { name = JSON.parse(text); } catch { assert(false); }
    assert(typeof name === "string" && encoder.encode(name).length < 32);
    return name;
  }

  function idFrom(text) {
    const id = Number(text);
    assert(id <= 65535);
    return id;
  }

  function routeFrom(line, enabledOnly = false) {
    const match = /^ROUTE ([1-9][0-9]*) ([1-9][0-9]*) (MANAGED|RAW) (OFF|PENDING|ACTIVE)$/.exec(line);
    assert(match && (!enabledOnly || match[4] !== "OFF"));
    return { source: idFrom(match[1]), destination: idFrom(match[2]), mode: match[3].toLowerCase(), state: match[4] };
  }

  function key({ source, destination, mode }) {
    return `${source}/${destination}/${mode}`;
  }

  function compatible(source, destination, mode) {
    return source?.direction === "in" && destination?.direction === "out" &&
      source.device !== destination.device && source.mode === mode && destination.mode === mode;
  }

  function stageRecord(projection, line) {
    const { devices, ports, routes } = projection;
    if (line.startsWith("DEVICE ")) {
      const match = /^DEVICE ([1-9][0-9]*) (connected|disconnected|ambiguous) (unset|#[0-9A-F]{6}) (".*")$/.exec(line);
      assert(match && !ports.size && !routes.size && devices.size < maxDevices);
      const id = idFrom(match[1]);
      assert(!devices.has(id));
      devices.set(id, { name: nameFrom(match[4]) });
    } else if (line.startsWith("BINDING ")) {
      const match = /^BINDING ([1-9][0-9]*) ([1-9][0-9]*) (in|out) (managed|raw|panel) (free|in-use|unavailable) (".*")$/.exec(line);
      assert(match && !routes.size && ports.size < maxPorts);
      const id = idFrom(match[1]);
      const device = idFrom(match[2]);
      assert(devices.has(device) && !ports.has(id));
      ports.set(id, { id, device, direction: match[3], mode: match[4], availability: match[5], name: nameFrom(match[6]) });
    } else {
      const route = routeFrom(line, true);
      const { source, destination, mode } = route;
      const routeKey = key(route);
      const previous = projection.previousRoute;
      assert(routes.size < maxRoutes && !routes.has(routeKey) && compatible(ports.get(source), ports.get(destination), mode));
      assert(!previous || source > previous.source || (source === previous.source &&
        (destination > previous.destination || (destination === previous.destination && mode > previous.mode))));
      routes.set(routeKey, route.state);
      projection.previousRoute = route;
    }
  }

  async function readAll(job) {
    const projection = { devices: new Map(), ports: new Map(), routes: new Map() };
    let offset = 0;
    for (let page = 0; page <= maxRecords; page++) {
      const command = offset ? `routing ${projection.revision} ${offset}` : "routing";
      const reply = cliReply(await cli.routingCommand(command), command);
      if (work !== job) return;
      if (reply.rejected) {
        if (offset && reply.rejected === "UNAVAILABLE") return { changed: true };
        if (cli.loopback && !offset && reply.rejected === "INVALID") {
          throw new Error("The selected router did not accept Routing. Start it with ./scripts/web-ui.sh.");
        }
        throw new Error(`Routing read failed (ERR ${reply.rejected}). ${rejections[reply.rejected]}.`);
      }
      const header = /^ROUTING ([1-9][0-9]{0,19}) (-|0|[1-9][0-9]*)$/.exec(reply.lines.shift());
      assert(header && BigInt(header[1]) <= maxRevision &&
        (!projection.revision || projection.revision === header[1]));
      projection.revision = header[1];
      assert(reply.lines.length <= 8 && offset + reply.lines.length <= maxRecords);
      for (const record of reply.lines) stageRecord(projection, record);
      if (header[2] === "-") {
        assert(projection.devices.size && projection.ports.size);
        return projection;
      }
      const next = Number(header[2]);
      assert(reply.lines.length > 0 && next === offset + reply.lines.length && next < maxRecords);
      offset = next;
    }
    assert(false);
  }

  function portDisplay(port, projection) {
    return `${projection.devices.get(port.device).name} / ${port.name} (ID ${port.id})`;
  }

  function cells(row, names) {
    for (const name of names) {
      const cell = document.createElement("th");
      cell.scope = "col";
      cell.textContent = name;
      row.append(cell);
    }
  }

  function tableBody(table, headings) {
    table.replaceChildren();
    cells(table.createTHead().insertRow(), headings);
    return table.createTBody();
  }

  function portHeading(node, port, projection) {
    const name = document.createElement("span");
    name.className = "port-name";
    name.textContent = portDisplay(port, projection);
    const role = document.createElement("span");
    role.className = "port-role";
    role.textContent = `${port.direction === "in" ? "Input" : "Output"} · ${port.mode} · ${availabilityLabels[port.availability]}`;
    node.append(name, role);
  }

  function addRouteCell(cell, projection, source, destination, mode) {
    const route = { source: source.id, destination: destination.id, mode };
    const state = projection.routes.get(key(route)) ?? "OFF";
    const action = state === "OFF" ? "Enable" : "Disable";
    const button = document.createElement("button");
    button.type = "button";
    button.dataset.routeState = state.toLowerCase();
    button.dataset.source = String(source.id);
    button.dataset.destination = String(destination.id);
    button.dataset.mode = mode;
    button.textContent = `${routeLabels[state]} · ${action}`;
    button.setAttribute("aria-label", `${action} ${portDisplay(source, projection)} to ${portDisplay(destination, projection)}, currently ${routeLabels[state]}`);
    button.addEventListener("click", () => { void edit(route, state === "OFF"); });
    cell.append(button);
  }

  function render(projection) {
    const entries = [...projection.ports.values()];
    const byMode = (direction, mode) => entries.filter(port => port.direction === direction && port.mode === mode);
    managedTable.replaceChildren();
    const header = managedTable.createTHead().insertRow();
    cells(header, ["Source input / Destination output"]);
    const destinations = byMode("out", "managed");
    for (const destination of destinations) {
      const column = document.createElement("th");
      column.scope = "col";
      portHeading(column, destination, projection);
      header.append(column);
    }
    const matrix = managedTable.createTBody();
    for (const source of byMode("in", "managed")) {
      const row = matrix.insertRow();
      const label = document.createElement("th");
      label.scope = "row";
      portHeading(label, source, projection);
      row.append(label);
      for (const destination of destinations) {
        const cell = row.insertCell();
        if (compatible(source, destination, "managed")) addRouteCell(cell, projection, source, destination, "managed");
        else cell.textContent = "Incompatible";
      }
    }
    const raw = tableBody(rawTable, ["Source input", "LED receiver output", "Route"]);
    const rawInputs = byMode("in", "raw");
    const rawOutputs = byMode("out", "raw");
    const rawRow = raw.insertRow();
    if (rawInputs.length === 1 && rawOutputs.length === 1 && compatible(rawInputs[0], rawOutputs[0], "raw")) {
      const [source] = rawInputs;
      const [destination] = rawOutputs;
      portHeading(rawRow.insertCell(), source, projection);
      portHeading(rawRow.insertCell(), destination, projection);
      addRouteCell(rawRow.insertCell(), projection, source, destination, "raw");
    } else {
      const cell = rawRow.insertCell();
      cell.colSpan = 3;
      cell.textContent = "A unique Raw LED route is unavailable.";
    }

    const inventory = tableBody(inventoryTable, ["Port", "Direction", "Capability", "Availability"]);
    for (const port of entries) {
      const row = inventory.insertRow();
      for (const value of [portDisplay(port, projection), port.direction === "in" ? "Input" : "Output",
        port.mode, availabilityLabels[port.availability]]) row.insertCell().textContent = value;
    }
    results.hidden = false;
  }

  function updateControls() {
    connect.hidden = !cli?.supported;
    connect.textContent = cli?.selected === "cdc" ? "Connect Web Serial CDC" : "Connect local CLI";
    connect.disabled = Boolean(cli?.attached);
    refresh.textContent = cli?.attached && !cli.structured ? "Check Routing" : "Refresh routing";
    refresh.disabled = !cli?.ready || Boolean(work);
    confirmLabel.hidden = !cli?.attached || cli.loopback || !cli.structured || !snapshot;
    confirmRouter.disabled = Boolean(work || stale);
    const enabled = Boolean(cli?.ready && !work && !stale && (cli.loopback || (cli.structured && confirmRouter.checked)));
    for (const button of results.querySelectorAll("button")) button.disabled = !enabled;
    link.hidden = !cli?.supported;
    router.textContent = cli?.endpoint ? `Selected router endpoint · ${cli.endpoint}` :
      cli?.selected === "loopback" ? `Selected router ${location.origin} · Loopback CLI` :
        cli?.selected === "cdc" ? "Web Serial CDC · Choose the router’s port." : "Routing needs a local serial provider.";
    if (snapshot) revision.textContent = stale ? `Stale snapshot · revision ${snapshot.revision}. Refresh before editing.` :
      `Snapshot revision ${snapshot.revision}`;
  }

  function begin() {
    if (!cli?.ready || work) return;
    const job = {};
    work = job;
    initialRead = false;
    stale = true;
    updateControls();
    return job;
  }

  async function load(job) {
    status.textContent = "Reading Routing snapshot…";
    let completed = await readAll(job);
    if (completed?.changed && cli.loopback) {
      status.textContent = "Routing changed during the read. Starting again…";
      completed = await readAll(job);
    }
    if (work !== job) return;
    if (completed.changed) throw new Error("Routing changed again. Refresh to try a new snapshot.");
    cli.confirmRoutingCapability();
    render(completed);
    snapshot = completed;
    stale = false;
    status.textContent = "Routing ready.";
  }

  function end(job) {
    if (work !== job) return;
    work = undefined;
    updateControls();
  }

  async function read() {
    const job = begin();
    if (!job) return;
    editStatus.textContent = "";
    try { await load(job); } catch (error) {
      if (work === job) {
        const probing = !cli.loopback && !cli.structured;
        if (probing) cli.keepPlainTerminal();
        status.textContent = probing ? `${error.message} The plain terminal is available. Reconnect before checking Routing again.` :
          error.message;
      }
    } finally {
      end(job);
    }
  }

  async function edit(route, on) {
    if (stale || (!cli.loopback && !confirmRouter.checked)) return;
    const job = begin();
    if (!job) return;
    const { source, destination, mode } = route;
    const pair = `${source} → ${destination}`;
    status.textContent = "Waiting for the router to commit the edit…";
    editStatus.textContent = "";
    try {
      const command = `route ${source} ${destination} ${mode} ${on ? "on" : "off"}`;
      const reply = cliReply(await cli.routingCommand(command, confirmRouter.checked), command);
      if (work !== job) return;
      if (reply.rejected) {
        editStatus.textContent = `Route ${pair} edit rejected (ERR ${reply.rejected}). ${rejections[reply.rejected]}.`;
      } else {
        assert(reply.lines.length === 1);
        const committed = routeFrom(reply.lines[0]);
        assert(key(committed) === key(route));
        editStatus.textContent = `Route ${pair} reported ${routeLabels[committed.state]}.`;
      }
      await load(job);
    } catch (error) {
      if (work === job) status.textContent = `${error.message} Refresh to check committed state.`;
    } finally {
      end(job);
    }
  }

  function onQueue() {
    updateControls();
    if (!view.hidden && cli?.loopback && cli.ready && initialRead && !work) void read();
  }

  function onHash() {
    view.hidden = location.hash !== "#routing";
    if (!view.hidden) {
      if (!cli?.supported) status.textContent = "Open Routing on the router host or in a supported local browser.";
      else if (!cli.attached) status.textContent = cli.notice || "Choose and connect a serial provider to read this router.";
      onQueue();
      view.scrollIntoView();
    }
  }

  connect.addEventListener("click", () => cli?.connect());
  refresh.addEventListener("click", () => { void read(); });
  confirmRouter.addEventListener("change", updateControls);
  window.addEventListener("gridpico-local-cli-queue", onQueue);
  window.addEventListener("gridpico-local-cli-binding", () => {
    work = undefined;
    snapshot = undefined;
    results.hidden = true;
    managedTable.replaceChildren();
    rawTable.replaceChildren();
    inventoryTable.replaceChildren();
    revision.textContent = "";
    confirmRouter.checked = false;
    stale = true;
    initialRead = true;
    editStatus.textContent = "";
    status.textContent = cli?.notice || (cli?.attached && cli.loopback ? "Connecting the local CLI…" :
      cli?.attached ? "Identify the selected router in the transcript, then choose Check Routing." :
        "Choose and connect a serial provider to read this router.");
    onQueue();
  });
  window.addEventListener("gridpico-local-cli-terminal", () => {
    if (snapshot) stale = true;
    if (!work && snapshot) status.textContent = "A terminal command was submitted. Refresh to check current routing.";
    updateControls();
  });
  window.addEventListener("hashchange", onHash);
  onHash();
});
