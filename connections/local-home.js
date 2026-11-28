window.addEventListener("DOMContentLoaded", async () => {
  "use strict";

  const adapter = window.gridpicoLocal;
  if (!adapter) return;
  const router = document.querySelector("#home-router");
  router.textContent = "Router · " + location.origin;
  router.hidden = false;
  const routing = document.querySelector("#home-routing");
  routing.closest("a").href = "/routing/";
  await adapter.ready;
  if (!adapter.capabilities) {
    if (adapter.capabilityStatus === "unavailable") {
      const status = document.querySelector("#home-capability-status");
      const retry = document.querySelector("#home-capability-retry");
      status.textContent = "Couldn’t load capabilities from " + location.origin + ".";
      status.hidden = retry.hidden = false;
      retry.addEventListener("click", () => location.reload());
    }
    return;
  }

  const { actions, roles } = adapter.capabilities;
  const target = adapter.target;
  if (location.origin === "http://192.168.0.223:18768" && adapter.capabilityStatus === "ready" &&
      adapter.trustedLan === true && target?.origin === location.origin && target.capabilities === adapter.capabilities) {
    router.textContent = "Pi router · " + location.origin;
    document.querySelector("#home-pico-cdc").hidden = actions.picoCdc !== true;
  }
  if (!actions.cli) {
    routing.textContent = "Routing unavailable on this connection. Open Routing for access options.";
  }
  if (!actions.cli && !actions.ledTest && !roles.monitor && !roles.log) {
    document.querySelector("#home-diagnostics").textContent =
      "See which diagnostics are available on this connection.";
  }
  document.querySelector("#home-recordings").hidden = !actions.recording;
  document.querySelector("[data-bundle-download]").hidden = false;

  // Optional pages are published only if included in the served release.
  for (const tile of document.querySelectorAll("[data-optional-tool]")) {
    const link = tile.querySelector("a");
    void fetch(link.href, { method: "HEAD", mode: "same-origin",
      redirect: "error", signal: AbortSignal.timeout(5000) }).then(response => {
      if (response.ok) tile.hidden = false;
      else if (response.status === 404) tile.hidden = true;
    }).catch(() => {});
  }
});
