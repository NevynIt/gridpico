"use strict";

const assert = require("node:assert/strict");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const puppeteer = require("puppeteer-core");

async function main() {
  const directory = path.resolve(process.argv[2]);
  const home = pathToFileURL(path.join(directory, "HOME.HTM")).href;
  assert.ok(process.env.GRIDPICO_TEST_CHROME, "Set GRIDPICO_TEST_CHROME to the stock browser");
  const browser = await puppeteer.launch({
    executablePath: process.env.GRIDPICO_TEST_CHROME,
    headless: true,
    pipe: true,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--disable-background-networking"],
  });
  try {
    const page = await browser.newPage();
    const errors = [];
    const requests = [];
    const sockets = [];
    page.on("pageerror", error => errors.push(error.message));
    const cdp = await page.createCDPSession();
    await cdp.send("Network.enable");
    cdp.on("Network.webSocketCreated", event => sockets.push(event.url));
    cdp.on("Network.requestWillBeSent", event => requests.push(event.request.url));
    await page.goto(home, { waitUntil: "load" });
    const tools = await page.$$eval("a[href]", links => links.map(link => link.getAttribute("href")));
    assert.deepEqual(tools, ["INDEX.HTM", "DIAG.HTM", "FM.HTM"]);
    console.log(await browser.version(), "file: HOME.HTM with three relative links");
    for (const filename of tools) {
      await page.goto(home, { waitUntil: "load" });
      await Promise.all([
        page.waitForNavigation({ waitUntil: "load" }),
        page.click(`a[href="${filename}"]`),
      ]);
      assert.equal(page.url(), pathToFileURL(path.join(directory, filename)).href);
      const state = await page.evaluate(async filename => {
        await window.gridpicoLocal.ready;
        const text = selector => document.querySelector(selector).textContent.trim();
        const state = {
          file: filename,
          title: document.title,
          capability: window.gridpicoLocal.capabilityStatus,
          nativeMidi: typeof navigator.requestMIDIAccess === "function",
          nativeSerialChooser: typeof navigator.serial?.requestPort === "function",
          midiProviders: window.gridpicoLocal.midiProviders,
        };
        if (filename === "INDEX.HTM") {
          state.pads = document.querySelectorAll(".pad[data-apc-note]").length;
          state.status = text("#route-pad-status");
          const button = document.querySelector("#local-midi-access");
          state.midiChoice = button.getClientRects().length > 0 && !button.disabled;
        } else if (filename === "DIAG.HTM") {
          state.serialProviders = Array.from(document.querySelector("#serial-provider").options,
            option => option.value);
          const button = document.querySelector("#connect");
          state.serialChoice = button.getClientRects().length > 0 && !button.disabled;
          state.status = text("#status");
          state.loopback = typeof window.gridpicoLocal.openSerial === "function";
          state.viaPi = typeof window.gridpicoLocal.openPicoCdc === "function";
        } else {
          state.status = text("#midi-status");
          state.midiChoice = document.querySelector("#provider option[value='web-midi']") !== null;
        }
        return state;
      }, filename);
      assert.equal(state.capability, "offline");
      assert.deepEqual(state.midiProviders, state.nativeMidi ? ["web-midi"] : []);
      if (filename === "INDEX.HTM") {
        assert.equal(state.pads, 40);
        assert.equal(state.status, "Disconnected");
        assert.equal(state.midiChoice, state.nativeMidi);
      } else if (filename === "DIAG.HTM") {
        assert.equal(state.loopback, false);
        assert.equal(state.viaPi, false);
        assert.deepEqual(state.serialProviders, state.nativeSerialChooser ? ["cdc"] : []);
        assert.equal(state.serialChoice, state.nativeSerialChooser);
        assert.equal(state.status, state.nativeSerialChooser ? "Web Serial CDC disconnected." :
          "Web Serial is unavailable in this browser or context.");
      } else {
        assert.equal(state.status, "MIDI disconnected.");
        assert.equal(state.midiChoice, state.nativeMidi);
      }
      console.log(JSON.stringify(state));
    }
    assert.deepEqual(errors, []);
    assert.deepEqual(sockets, []);
    assert.ok(requests.length > 0 && requests.every(url => url.startsWith("file:")));
    console.log("Only file requests; no WebSocket/discovery, no chooser opened or ports connected");
  } finally {
    await browser.close();
  }
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
