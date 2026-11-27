window.addEventListener("DOMContentLoaded", async () => {
  "use strict";

  const dedicated = document.body.dataset.tool === "routing";
  if (!dedicated && location.protocol === "http:" && location.hostname === "127.0.0.1") return;
  const adapter = window.gridpicoLocal;
  await adapter?.ready;
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
  const focusedPair = document.querySelector("#routing-focus");
  const matrixRegion = managedTable.parentElement;
  const encoder = new TextEncoder();

  if (dedicated) {
    document.querySelector("#routing-tools").hidden = false;
    view.setAttribute("aria-label", "Router connections and routes");
    view.removeAttribute("aria-labelledby");
    document.querySelector("#routing-session").append(document.querySelector(".connection[data-serial-controls]"));
    document.querySelector("#routing-terminal-link").hidden = true;
    document.querySelector("#routing-more").open = false;

    const terminal = document.createElement("details");
    terminal.id = "routing-terminal";
    const title = document.createElement("summary");
    title.textContent = "Serial transcript and CLI commands";
    terminal.append(title, document.querySelector('[aria-labelledby="transcript-heading"]'),
      document.querySelector("#terminal"));
    view.after(terminal);
    if (location.hash === "#terminal") terminal.open = true;
    if (!cli?.supported) terminal.hidden = true;
    if (adapter?.capabilityStatus === "unavailable") {
      const retry = document.querySelector("#discovery-retry");
      retry.hidden = false;
      retry.addEventListener("click", () => location.reload());
    }
  }

  // Upper bound for the portable CLI projection. Smaller profiles use the same grammar.
  const maxDevices = 12;
  const maxPorts = 36;
  const maxRoutes = 13;
  const maxRecords = maxDevices + maxPorts + maxRoutes;
  const maxRevision = 0xffffffffffffffffn;
  const availabilityLabels = { free: "Free", "in-use": "In use", unavailable: "Unavailable" };
  const routeLabels = { OFF: "Off", PENDING: "Pending", ACTIVE: "Active" };
  const rejections = { INVALID: "Invalid request", CAPACITY: "Capacity limit", UNAVAILABLE: "Unavailable" };

  let snapshot;
  let managedGuard;
  let work;
  let stale = false;
  let initialRead = true;
  let editFeedback;

  function assert(condition) {
    if (!condition) throw new Error("The local CLI returned an incomplete or invalid Routing reply.");
  }

  async function routingReply(command, confirmed = false) {
    const machine = cli.loopback;
    const reply = await cli.routingCommand(command, confirmed);
    const lines = (machine ? reply : reply.replace(/^> (?:\r\n)?/, "")).split("\r\n");
    if (machine) assert(lines.pop() === "");
    else {
      const echo = lines.shift();
      // A qualified Web Serial terminal can echo routing with a trailing space.
      assert((echo === command || (command === "routing" && echo === "routing ")) && lines.pop() === "> ");
    }
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
      (mode === "managed" || source.device !== destination.device) && source.mode === mode && destination.mode === mode;
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

  async function readManagedGuard() {
    const command = "help route";
    try {
      const reply = await routingReply(command);
      const form = reply.lines?.[0]?.split(" -- ", 1)[0];
      return reply.lines?.length === 1 && form.startsWith("COMMAND route ") &&
        form.split("; ").includes("route <src> <dst> managed on|off if <revision>");
    } catch {
      return false;
    }
  }

  async function readAll(job) {
    const projection = { devices: new Map(), ports: new Map(), routes: new Map() };
    let offset = 0;
    for (let page = 0; page <= maxRecords; page++) {
      const command = offset ? `routing ${projection.revision} ${offset}` : "routing";
      const reply = await routingReply(command);
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

  function pairName(source, destination) {
    return `${source.name} (${source.id}) to ${destination.name} (${destination.id})`;
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
    const enabled = state !== "OFF";
    const pair = pairName(source, destination);
    const button = document.createElement("button");
    button.type = "button";
    button.className = "route-switch";
    button.dataset.routeState = state.toLowerCase();
    button.dataset.source = String(source.id);
    button.dataset.destination = String(destination.id);
    button.dataset.mode = mode;
    button.setAttribute("role", "switch");
    button.setAttribute("aria-checked", String(enabled));
    button.setAttribute("aria-label", pair);
    const stateText = document.createElement("span");
    stateText.className = "route-state";
    stateText.id = `route-state-${source.id}-${destination.id}`;
    stateText.textContent = routeLabels[state];
    button.setAttribute("aria-describedby", stateText.id);
    button.append(enabled ? "Enabled" : "Disabled", stateText);
    button.addEventListener("click", () => {
      if (button.getAttribute("aria-disabled") !== "true") void edit(route, !enabled);
    });
    button.addEventListener("focus", () => {
      focusedPair.textContent = pair;
      if (mode !== "managed") return;
      const region = matrixRegion.getBoundingClientRect();
      const bounds = cell.getBoundingClientRect();
      const firstColumn = cell.parentElement.firstElementChild.getBoundingClientRect();
      if (bounds.right > region.right - 1) matrixRegion.scrollLeft += bounds.right - region.right + 1;
      else if (bounds.left < firstColumn.right) matrixRegion.scrollLeft -= firstColumn.right - bounds.left;
    });
    cell.append(button);
    const note = document.createElement("small");
    note.className = "route-note";
    note.id = `route-note-${source.id}-${destination.id}`;
    const feedback = editFeedback?.key === key(route) ? editFeedback.message : "";
    note.textContent = feedback;
    note.hidden = !feedback;
    button.setAttribute("aria-describedby", `${stateText.id} ${note.id}`);
    cell.append(note);
  }

  function editNote(route, message) {
    editFeedback = { key: key(route), message };
    const { source, destination } = route;
    const note = document.getElementById(`route-note-${source}-${destination}`);
    if (!note) return;
    note.hidden = !message;
    note.textContent = message;
  }

  function render(projection) {
    const focused = document.activeElement?.closest(".route-switch");
    const scrollLeft = matrixRegion.scrollLeft;
    const entries = [...projection.ports.values()];
    const byMode = (direction, mode) => entries.filter(port => port.direction === direction && port.mode === mode);
    managedTable.replaceChildren();
    const header = managedTable.createTHead().insertRow();
    const axes = document.createElement("th");
    axes.scope = "col";
    const outputs = document.createElement("span");
    outputs.className = "port-name";
    outputs.textContent = "Outputs / destinations";
    const inputs = document.createElement("span");
    inputs.className = "port-role";
    inputs.textContent = "Inputs / sources";
    axes.append(outputs, inputs);
    header.append(axes);
    const destinations = byMode("out", "managed");
    for (const destination of destinations) {
      const column = document.createElement("th");
      column.scope = "col";
      portHeading(column, destination, projection);
      header.append(column);
    }
    const matrix = managedTable.createTBody();
    const sources = byMode("in", "managed");
    for (const source of sources) {
      const row = matrix.insertRow();
      const label = document.createElement("th");
      label.scope = "row";
      portHeading(label, source, projection);
      row.append(label);
      for (const destination of destinations) {
        addRouteCell(row.insertCell(), projection, source, destination, "managed");
      }
    }
    if (!sources.length || !destinations.length) {
      const cell = matrix.insertRow().insertCell();
      cell.colSpan = destinations.length + 1;
      cell.textContent = "This router has no managed input and output pairs.";
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
    matrixRegion.scrollLeft = scrollLeft;
    if (focused && results.contains(focused) === false) {
      const { source, destination, mode } = focused.dataset;
      results.querySelector(`.route-switch[data-source="${source}"][data-destination="${destination}"][data-mode="${mode}"]`)
        ?.focus({ preventScroll: true });
    }
  }

  function updateControls() {
    connect.hidden = dedicated || !cli?.supported;
    connect.textContent = cli?.selected === "cdc" ? "Connect Web Serial CDC" : "Connect local CLI";
    connect.disabled = Boolean(cli?.attached);
    refresh.hidden = dedicated && !cli?.supported;
    refresh.textContent = cli?.attached && !cli.structured ? "Check Routing" : "Refresh routing";
    refresh.disabled = !cli?.ready || Boolean(work);
    confirmLabel.hidden = !cli?.attached || cli.loopback || !cli.structured || !snapshot;
    confirmRouter.disabled = Boolean(work || stale);
    const authorized = cli?.loopback || (cli?.structured && confirmRouter.checked);
    const enabled = Boolean(cli?.ready && !work && !stale && authorized);
    for (const button of results.querySelectorAll(".route-switch")) {
      const editable = enabled && (button.dataset.mode === "raw" || managedGuard === true);
      button.setAttribute("aria-disabled", String(!editable));
      if (!dedicated) button.disabled = !editable;
    }
    if (!dedicated) link.hidden = !cli?.supported;
    router.textContent = cli?.endpoint ? `Selected router endpoint · ${cli.endpoint}` :
      cli?.selected === "loopback" ? `Selected router ${location.origin} · Loopback CLI` :
        cli?.selected === "cdc" ? "Web Serial CDC · Choose the router’s port." :
          dedicated ? `Router · ${location.origin}` : "Routing needs a local serial provider.";
    if (snapshot) {
      if (work) revision.textContent = `Reading current state · showing confirmed revision ${snapshot.revision}.`;
      else if (stale) revision.textContent = `Stale snapshot · revision ${snapshot.revision}. Refresh before editing.`;
      else if (!authorized) revision.textContent = `Read-only · revision ${snapshot.revision}. Confirm the selected device to edit.`;
      else if (!managedGuard) revision.textContent =
        `Managed routes are read-only · revision ${snapshot.revision}. The selected CLI did not advertise guarded edits.`;
      else revision.textContent = `Snapshot revision ${snapshot.revision}`;
    }
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
    if (managedGuard === undefined) {
      const supported = await readManagedGuard();
      if (work !== job) return;
      managedGuard = supported;
    }
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
    editFeedback = undefined;
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
    if (stale || (route.mode === "managed" && !managedGuard) || (!cli.loopback && !confirmRouter.checked)) return;
    const guard = snapshot.revision;
    const job = begin();
    if (!job) return;
    const { source, destination, mode } = route;
    const pair = pairName(snapshot.ports.get(source), snapshot.ports.get(destination));
    const previous = editFeedback?.key.split("/").map(Number);
    if (previous) {
      const note = document.getElementById(`route-note-${previous[0]}-${previous[1]}`);
      if (note) note.hidden = true;
    }
    editNote(route, "Awaiting readback.");
    status.textContent = "Waiting for the router to commit the edit…";
    editStatus.textContent = `${pair} · Awaiting readback.`;
    try {
      const write = `route ${source} ${destination} ${mode} ${on ? "on" : "off"}`;
      const command = mode === "managed" ? `${write} if ${guard}` : write;
      const reply = await routingReply(command, confirmRouter.checked);
      if (work !== job) return;
      if (reply.rejected) {
        const reason = `Edit rejected (ERR ${reply.rejected}) · ${rejections[reply.rejected]}.`;
        editNote(route, reason);
        editStatus.textContent = `${pair} · ${reason}`;
      } else {
        assert(reply.lines.length === 1);
        const committed = routeFrom(reply.lines[0]);
        assert(key(committed) === key(route));
      }
      await load(job);
      if (work === job && !reply.rejected) {
        editNote(route, "");
        const state = snapshot.routes.get(key(route)) ?? "OFF";
        editStatus.textContent = `${pair} · Confirmed ${routeLabels[state]}.`;
      }
    } catch (error) {
      if (work === job) {
        if (editFeedback?.message === "Awaiting readback.") {
          editNote(route, "Readback unconfirmed. Refresh this route.");
        }
        status.textContent = `${error.message} Refresh to check committed state.`;
        editStatus.textContent = `${pair} · ${editFeedback.message}`;
      }
    } finally {
      end(job);
    }
  }

  function onQueue() {
    updateControls();
    if (!view.hidden && cli?.loopback && cli.ready && initialRead && !work) void read();
  }

  function onHash() {
    view.hidden = !dedicated && location.hash !== "#routing";
    if (!view.hidden) {
      if (dedicated && adapter?.capabilityStatus === "unavailable") {
        status.textContent = "Couldn’t load capabilities from " + location.origin + ".";
      } else if (!cli?.supported) {
        if (adapter?.capabilities?.actions.picoCdc) {
          status.textContent = "Typed Pico Routing is unavailable. Use Diagnostics for the Pico terminal or the Pi’s SSH loopback for Pi Routing.";
        } else status.textContent = dedicated && adapter?.capabilities ? "Routing unavailable on this connection. Use the router’s local CLI or SSH loopback connection." :
          "Routing unavailable. Open it from the router’s loopback Home or connect the device through Web Serial CDC in a supported browser.";
      }
      else if (!cli.attached) status.textContent = cli.notice || "Routing unavailable. Connect a serial provider to read this router.";
      onQueue();
      if (!dedicated) view.scrollIntoView();
    }
  }

  connect.addEventListener("click", () => cli?.connect());
  refresh.addEventListener("click", () => { void read(); });
  confirmRouter.addEventListener("change", updateControls);
  window.addEventListener("gridpico-local-cli-queue", onQueue);
  window.addEventListener("gridpico-local-cli-binding", () => {
    work = undefined;
    snapshot = undefined;
    managedGuard = undefined;
    results.hidden = true;
    managedTable.replaceChildren();
    rawTable.replaceChildren();
    inventoryTable.replaceChildren();
    revision.textContent = "";
    editFeedback = undefined;
    focusedPair.textContent = "Focus a crosspoint to see its full port names.";
    confirmRouter.checked = false;
    stale = true;
    initialRead = true;
    editStatus.textContent = "";
    status.textContent = cli?.notice || (cli?.attached && cli.loopback ? "Connecting the local CLI…" :
      cli?.attached ? "Identify the selected router in the transcript, then choose Check Routing." :
        "Routing unavailable. Connect a serial provider to read this router.");
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
