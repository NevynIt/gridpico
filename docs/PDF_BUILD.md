# Full documentation PDF

The PDF is a revision-stamped snapshot, not a separate contract. It collects every Markdown (`.md`) and Mermaid source (`.mmd`) file tracked in the selected Git commit, in path order. It excludes `third_party/`, `vendor/`, `node_modules/`, `build/`, and `dist/`. Tracked first-party image assets are embedded. The generated PDF itself is not a source input.

From the repository root, install the locked Node dependencies and build the PDF:

```sh
npm ci --prefix docs/pdf
node docs/build-pdf.mjs --ref HEAD --output build/docs/gridpico-documentation.pdf
```

Use Node.js 18 and npm. The lockfile pins `markdown-it` 14.1.0, `puppeteer-core` 23.8.0 and `@sparticuz/chromium` 131.0.1, including the Chromium executable and fonts. On Debian or Ubuntu, the headless browser also needs the system packages `libnss3` and `libnspr4`. No browser download is needed at build time. Do not fetch remote resources during PDF generation.

The PDF has a linked contents page. Links between included Markdown pages and headings navigate within the PDF. Links to other tracked files point to that exact GitHub commit. Untracked local evidence paths are collected in a PDF appendix rather than presented as live links. External URLs remain clickable and require network access. Tracked Mermaid sources are shown verbatim as code because this toolchain has no Mermaid renderer; open the editable `.mmd` source for a rendered view.

The build writes a sidecar `.manifest.txt` with the exact ordered source and image paths, source revision and digest, and coverage exclusions. Creation and modification dates in the PDF are normalized to the selected Git commit time so repeated builds with the same pinned toolchain and browser produce the same PDF hash. Long Mermaid source blocks can flow across printed pages.

Build from a clean, committed source revision for a publishable snapshot. The script reads blobs from Git, not dirty worktree contents, and reports the source revision, input digest, and PDF SHA-256. The current repository has no CI workflow; Documentation refreshes the PDF after significant documentation changes. Project Manager publishes the generated file as a docs-only GitHub Release asset. The generated PDF stays under ignored `build/` and is not committed.
