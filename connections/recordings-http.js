"use strict";

const { readFileSync, createReadStream } = require("node:fs");
const { pipeline } = require("node:stream");
const { createHash, randomBytes, timingSafeEqual } = require("node:crypto");
const path = require("node:path");

function createRecordingsHttp(recorder, matchesHeader, frozenPage) {
  const html = frozenPage?.toString("utf8") ?? readFileSync(path.join(__dirname, "recordings-page.html"), "utf8");
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const hash = createHash("sha256").update(script).digest("base64");
  const tokens = new Map();
  let downloads = 0;

  function status(scope, withToken) {
    const { state, activeId, completedId, revision } = recorder.snapshot();
    let actionToken = null;
    if (withToken && state !== "stopping") {
      if (tokens.get(scope)?.revision !== revision) {
        tokens.set(scope, { revision, value: randomBytes(32).toString("hex") });
      }
      actionToken = tokens.get(scope).value;
    }
    return { state, activeId, completedId, actionToken };
  }

  function json(response, code, value) {
    response.setHeader("Content-Type", "application/json; charset=utf-8");
    response.writeHead(code).end(JSON.stringify(value));
  }

  return function serveRecordings(request, response, scope, origin, loopbackPort) {
    if (!request.url.startsWith("/recordings/")) return false;
    response.setHeader("Cache-Control", "no-store");
    response.setHeader("X-Frame-Options", "DENY");
    response.setHeader("X-Content-Type-Options", "nosniff");
    response.setHeader("Cross-Origin-Resource-Policy", "same-origin");
    response.setHeader("Content-Security-Policy",
      `default-src 'none'; script-src 'sha256-${hash}'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`);
    const finish = code => { response.writeHead(code).end(); return true; };
    const fetchSite = request.headers["sec-fetch-site"];
    if ((fetchSite !== undefined && !["same-origin", "none"].some(value => matchesHeader(request, "sec-fetch-site", value))) ||
        ((request.method === "POST" || request.headers.origin !== undefined) &&
         !matchesHeader(request, "origin", origin))) return finish(403);

    if (request.url === "/recordings/" || request.url === "/recordings/status") {
      if (request.method !== "GET") return finish(405);
      if (request.url === "/recordings/") {
        response.setHeader("Content-Type", "text/html; charset=utf-8");
        response.end(html);
      } else {
        json(response, 200, status(scope, true));
      }
      return true;
    }

    const download = /^\/recordings\/([0-9a-f]{32})\.wav$/.exec(request.url);
    if (download) {
      if (request.method !== "GET") return finish(405);
      if (downloads >= 2) return finish(503);
      let opened;
      try { opened = recorder.download(download[1]); } catch { return finish(404); }
      if (!opened) return finish(404);
      downloads++;
      response.writeHead(200, {
        "Content-Type": "audio/wav",
        "Content-Length": opened.size,
        "Content-Disposition": `attachment; filename="gridpico-${download[1]}.wav"`,
      });
      response.setTimeout(5000, () => response.destroy());
      pipeline(createReadStream(null, { fd: opened.fd }), response, () => { downloads--; });
      return true;
    }

    const start = request.url === "/recordings/start";
    const stop = /^\/recordings\/([0-9a-f]{32})\/stop$/.exec(request.url);
    if (!start && !stop) return finish(404);
    if (request.method !== "POST") return finish(405);
    if (request.headers["transfer-encoding"] !== undefined ||
        (request.headers["content-length"] !== undefined && request.headers["content-length"] !== "0")) {
      return finish(400);
    }
    const token = request.headers["x-gridpico-record-action"];
    const current = recorder.snapshot();
    const issued = tokens.get(scope);
    if (typeof token !== "string" || !/^[0-9a-f]{64}$/.test(token) ||
        !matchesHeader(request, "x-gridpico-record-action", token) ||
        issued?.revision !== current.revision ||
        !timingSafeEqual(Buffer.from(token), Buffer.from(issued.value))) return finish(409);

    if (start ? current.activeId !== null :
      current.activeId !== stop[1] || !["starting", "recording"].includes(current.state)) return finish(409);
    tokens.delete(scope);
    const accepted = start ? recorder.start(loopbackPort) : recorder.stop(stop[1]);
    json(response, accepted ? 202 : 503, status(scope, false));
    return true;
  };
}

module.exports = { createRecordingsHttp };
