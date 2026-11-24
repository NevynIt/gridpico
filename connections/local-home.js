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
  if (!actions.cli) {
    routing.textContent = "Routing unavailable on this connection. Open Routing for access options.";
  }
  if (!actions.cli && !actions.ledTest && !roles.monitor && !roles.log) {
    document.querySelector("#home-diagnostics").textContent =
      "See which diagnostics are available on this connection.";
  }
  document.querySelector("[data-bundle-only]").hidden = !actions.recording;
  document.querySelector("[data-bundle-download]").hidden = false;

  // FM is optional in the release manifest.
  const fm = document.querySelector("[data-optional-tool=fm]");
  try {
    const response = await fetch("/fm/", { method: "HEAD", mode: "same-origin",
      redirect: "error", signal: AbortSignal.timeout(5000) });
    if (response.status === 404) fm.hidden = true;
  } catch {
    // A later navigation can still check the installed tool.
  }
});
