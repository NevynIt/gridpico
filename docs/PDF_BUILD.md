# Full documentation PDF

The PDF is a revision-stamped snapshot, not a separate contract. It collects every Markdown (`.md`) and Mermaid source (`.mmd`) file tracked in the selected Git commit, in path order. It excludes `third_party/`, `vendor/`, `node_modules/`, `build/`, and `dist/`. Tracked first-party image assets are embedded. The generated PDF itself is not a source input.

From the repository root, install the locked Node dependencies and build the PDF:

```sh
npm ci --prefix docs/pdf && node docs/build-pdf.mjs --ref HEAD --output build/docs/gridpico-documentation.pdf
```

Use Node.js 18 and npm. `docs/pdf/package-lock.json` pins `markdown-it` 14.1.0, `puppeteer-core` 23.8.0 and `@sparticuz/chromium` 131.0.1. The Chromium package includes Chromium 131.0.6778.0, its fonts and Fontconfig file. On Debian or Ubuntu, install the public OS runtime packages `libnss3` and `libnspr4` (`sudo apt-get update && sudo apt-get install --yes libnss3 libnspr4`). The builder sets the bundled Fontconfig file after Chromium initializes. No browser download is needed at build time. Do not fetch remote resources during PDF generation.

Local fallback for diagnosis only: point the overrides at a separately installed Chromium 131.0.6778.0 and its matching Fontconfig file:

```sh
GRIDPICO_CHROMIUM_PATH=/path/to/chrome GRIDPICO_FONTCONFIG_FILE=/path/to/fonts.conf \
  node docs/build-pdf.mjs --ref HEAD --output build/docs/gridpico-documentation.pdf
```

Other browser versions or system libraries may change PDF bytes. The PDF object checks below must still pass. The standard build uses the pinned package above.

The PDF has a linked contents page. Links between included Markdown pages and headings navigate within the PDF. A fragment that does not match a heading in an included source document is listed in the PDF appendix and links to the pinned source document without that fragment. Broken intra-PDF links remain a build error. Links to other tracked files point to that exact GitHub commit. Untracked local evidence paths are collected in the appendix rather than presented as live links. External URLs remain clickable and require network access. Tracked Mermaid sources are shown verbatim as code because this toolchain has no Mermaid renderer; open the editable `.mmd` source for a rendered view.

The build writes a sidecar `.manifest.txt` with the exact ordered source and image paths, source revision and digest, and coverage exclusions. It checks that the rendered PDF contains internal and external link annotations and font objects. Creation and modification dates are normalized to the selected Git commit time. The PDF SHA-256 is a byte-level pin and should match when the lockfile, Chromium, fonts, system libraries, and build environment match. Across renderer or runtime differences, use the source/manifest digests and content, link, and rendering checks to assess reproducibility. A PDF hash difference alone does not establish a content difference. Long Mermaid source blocks can flow across printed pages.

Build from a clean, committed source revision for a publishable snapshot. The script reads blobs from Git, not dirty worktree contents, and reports the source revision, input digest, and PDF SHA-256. The current repository has no CI workflow; Documentation refreshes the PDF after significant documentation changes. For each refresh, Project Manager publishes a new immutable docs-only GitHub Release asset, then Documentation updates the direct PDF asset link in the root README to that release tag. Publish the PDF and its manifest together. The generated files stay under ignored `build/` and are not committed.
