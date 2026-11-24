"use strict";

const { readFileSync, statSync } = require("node:fs");
const { createServer } = require("node:http");
const { isIPv4 } = require("node:net");
const { networkInterfaces } = require("node:os");

function createTrustedLanListener(filename) {
  const info = statSync(filename);
  if (!info.isFile() || info.size > 4096) throw new Error("Expected a small trusted LAN configuration file");
  const config = JSON.parse(readFileSync(filename, "utf8"));
  const { interface: device, address, port, cidr } = config;
  if (device !== "wlan0" || address !== "192.168.0.223" || port !== 18768 || cidr !== "192.168.0.0/24") {
    throw new Error("Expected wlan0, 192.168.0.223:18768 and 192.168.0.0/24");
  }
  const matching = () => networkInterfaces()[device]?.some((entry) =>
    entry.family === "IPv4" && !entry.internal && entry.address === address && entry.netmask === "255.255.255.0");
  if (!matching()) throw new Error("The configured address and subnet are not active on wlan0");

  return {
    server: createServer(),
    address,
    port,
    host: `${address}:${port}`,
    origin: `http://${address}:${port}`,
    ports: new Map([[1, "in"], [2, "in"], [3, "out"]]),
    trustedLan: true,
    available: matching,
    identity(socket) {
      const remote = socket.remoteAddress;
      if (isIPv4(remote) && remote.startsWith("192.168.0.") && matching()) return "lan";
    },
  };
}

module.exports = { createTrustedLanListener };
