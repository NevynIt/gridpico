"use strict";

const { createHash } = require("node:crypto");
const { readFileSync } = require("node:fs");
const path = require("node:path");

const pages = new Map([
  ["/", "index.html"],
  ["/apc/", "apc/index.html"],
  ["/diagnostics/", "diagnostics/index.html"],
  ["/routing/", "routing/index.html"],
  ["/recordings/", "recordings/index.html"],
  ["/fm/", "fm/index.html"],
  ["/midi-file/", "midi-file/index.html"],
]);
const optionalPages = new Set(["/fm/", "/midi-file/"]);
const scripts = ["local-transport", "local-home", "local-diagnostics", "local-routing", "local-led-test"];
const allowed = new Map([...pages, ...scripts.map(name => [`/${name}.js`, `${name}.js`])]);
const digest = bytes => createHash("sha256").update(bytes).digest("hex");

function loadWebUiBundle() {
  const directory = path.join(__dirname, "../build/web-ui/bundle");
  const read = name => readFileSync(path.join(directory, name));
  const manifest = read("SHA256SUMS");
  const entries = new Map();
  for (const line of manifest.toString("latin1").trimEnd().split("\n")) {
    const match = /^([a-f0-9]{64})  ([a-z][a-z0-9./-]*)$/.exec(line);
    if (!match || entries.has(match[2])) throw new Error("Invalid browser bundle manifest");
    entries.set(match[2], match[1]);
  }

  const assets = new Map();
  for (const [url, file] of allowed) {
    if (optionalPages.has(url) && !entries.has(file)) continue;
    const content = read(file);
    if (entries.get(file) !== digest(content)) throw new Error(`Browser bundle mismatch: ${file}`);
    entries.delete(file);
    assets.set(url, [file.endsWith(".js") ? "text/javascript; charset=utf-8" :
      "text/html; charset=utf-8", content]);
  }
  if (entries.size) throw new Error("Unexpected browser bundle assets");
  const release = digest(manifest);
  const archive = read("web-ui.zip");
  if (read("web-ui.zip.check").toString("ascii") !== `${release} ${digest(archive)}\n`) {
    throw new Error("Browser archive does not match the prepared release");
  }
  assets.set("/web-ui/SHA256SUMS", ["text/plain; charset=utf-8", manifest]);
  assets.set("/web-ui.zip", ["application/zip", archive]);
  return { assets, release };
}

module.exports = { loadWebUiBundle };
