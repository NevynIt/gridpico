"""Build the served Gridpico browser release and its exact download."""

import hashlib
from pathlib import Path
from zipfile import ZIP_STORED, ZipFile, ZipInfo


root = Path(__file__).resolve().parent.parent
bundle = root / "build" / "web-ui" / "bundle"
sources = {
    "index.html": "connections/home.html",
    "apc/index.html": "web/apc/index.html",
    "diagnostics/index.html": "connections/diagnostics/index.html",
    "routing/index.html": "connections/diagnostics/index.html",
    "recordings/index.html": "connections/recordings-page.html",
    "local-transport.js": "connections/local-transport.js",
    "local-diagnostics.js": "connections/local-diagnostics.js",
    "local-home.js": "connections/local-home.js",
    "local-routing.js": "connections/local-routing.js",
    "local-led-test.js": "connections/local-led-test.js",
}
for tool in ("fm", "midi-file", "circuit"):
    source = f"web/{tool}/index.html"
    if (root / source).is_file():
        sources[f"{tool}/index.html"] = source

assets = {name: (root / sources[name]).read_bytes() for name in sorted(sources)}
manifest = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in assets.items()).encode("ascii")

bundle.mkdir(parents=True, exist_ok=True)
for name, data in assets.items():
    target = bundle / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
(bundle / "SHA256SUMS").write_bytes(manifest)

archive = bundle / "web-ui.zip.tmp"
with ZipFile(archive, "w", compression=ZIP_STORED) as release:
    for name, data in {**assets, "SHA256SUMS": manifest}.items():
        entry = ZipInfo(name)
        entry.create_system = 3
        entry.external_attr = 0o100644 << 16
        release.writestr(entry, data)
archive.replace(bundle / "web-ui.zip")
release = hashlib.sha256(manifest).hexdigest()
archive_digest = hashlib.sha256((bundle / "web-ui.zip").read_bytes()).hexdigest()
(bundle / "web-ui.zip.check").write_text(f"{release} {archive_digest}\n", encoding="ascii")
print(f"Gridpico browser bundle {release} · {bundle}")
