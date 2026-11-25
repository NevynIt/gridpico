#!/usr/bin/env node

import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { readFileSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, "..");
const require = createRequire(path.join(here, "pdf", "package.json"));
const MarkdownIt = require("markdown-it");
const puppeteer = require("puppeteer-core");
const chromium = require("@sparticuz/chromium");
const githubRepo = "https://github.com/NevynIt/gridpico";

function usage() {
  process.stderr.write("Usage: node docs/build-pdf.mjs [--ref COMMIT] [--output FILE]\n");
}

function parseArgs(argv) {
  const args = { ref: "HEAD", output: "build/docs/gridpico-documentation.pdf" };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === "--ref" && argv[i + 1]) args.ref = argv[++i];
    else if (arg === "--output" && argv[i + 1]) args.output = argv[++i];
    else if (arg === "--help" || arg === "-h") {
      usage();
      process.exit(0);
    } else {
      usage();
      throw new Error(`Unknown or incomplete option: ${arg}`);
    }
  }
  return args;
}

function git(args, binary = false) {
  return execFileSync("git", args, {
    cwd: repo,
    encoding: binary ? "buffer" : "utf8",
    maxBuffer: 64 * 1024 * 1024,
  });
}

function included(pathname) {
  if (!/\.(?:md|mmd)$/i.test(pathname)) return false;
  const parts = pathname.split("/");
  return !parts.some((part) => ["third_party", "vendor", "node_modules", "build", "dist"].includes(part));
}

function slug(text) {
  return text.toLowerCase()
    .trim()
    .replace(/\s+/gu, "-")
    .replace(/[^\p{L}\p{N}\p{M}_-]/gu, "");
}

function inlineText(token) {
  return (token.children ?? []).map((child) => {
    if (["text", "code_inline", "html_inline"].includes(child.type)) return child.content;
    if (child.type === "image") return child.content || child.attrGet("alt") || "";
    return "";
  }).join("");
}

function docId(file) {
  return `doc-${file.replace(/[^A-Za-z0-9_-]+/g, "-").replace(/^-|-$/g, "")}`;
}

function assetId(file) {
  return `asset-${file.replace(/[^A-Za-z0-9_-]+/g, "-").replace(/^-|-$/g, "")}`;
}

function githubUrl(commit, file, fragment = "", tree = false) {
  const encodedPath = file.split("/").map(encodeURIComponent).join("/");
  const anchor = fragment ? `#${encodeURIComponent(fragment)}` : "";
  return `${githubRepo}/${tree ? "tree" : "blob"}/${commit}/${encodedPath}${anchor}`;
}

function safeLocalPath(source, raw) {
  let decoded;
  try { decoded = decodeURIComponent(raw); } catch { decoded = raw; }
  const workspacePrefix = "/workspace/gridpico/";
  if (decoded.startsWith(workspacePrefix)) decoded = decoded.slice(workspacePrefix.length);
  else if (/^\/(?:tmp|home|workspace)\//.test(decoded)) return null;
  let resolved;
  if (decoded.startsWith("/")) resolved = path.posix.normalize(decoded.replace(/^\/+/, ""));
  else resolved = path.posix.normalize(path.posix.join(path.posix.dirname(source), decoded));
  if (resolved === ".." || resolved.startsWith("../")) return null;
  return resolved;
}

function isExternal(href) {
  return /^(?:https?:|mailto:|ftp:|data:|tel:|sms:|javascript:)/i.test(href);
}

function dataUri(file, bytes) {
  const ext = path.posix.extname(file).toLowerCase();
  const mime = ({
    ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
    ".gif": "image/gif", ".svg": "image/svg+xml", ".webp": "image/webp",
  })[ext];
  return mime ? `data:${mime};base64,${bytes.toString("base64")}` : null;
}

function addHeadingIds(tokens, file, doc) {
  const seen = new Map();
  for (let i = 0; i < tokens.length; ++i) {
    if (tokens[i].type !== "heading_open") continue;
    const content = tokens[i + 1];
    const label = inlineText(content);
    const base = slug(label) || "section";
    const count = seen.get(base) ?? 0;
    seen.set(base, count + 1);
    const headingSlug = count ? `${base}-${count}` : base;
    const target = `${doc.id}-${headingSlug}`;
    tokens[i].attrSet("id", target);
    doc.anchors.set(headingSlug, target);
    doc.headings.push({
      level: Number(tokens[i].tag.slice(1)), label,
      id: target,
    });
  }
}

function resolveHref(href, file, docs, commit, trackedPaths, assetPaths, unbundledRefs) {
  if (!href || isExternal(href)) return href;
  const splitAt = href.indexOf("#");
  const rawPath = splitAt < 0 ? href : href.slice(0, splitAt);
  const fragment = splitAt < 0 ? "" : decodeURIComponent(href.slice(splitAt + 1));
  if (!rawPath) {
    const doc = docs.get(file);
    const local = doc.anchors.get(slug(fragment));
    return local ? `#${local}` : href;
  }

  const targetPath = safeLocalPath(file, rawPath);
  if (!targetPath) return registerUnbundled(file, href, unbundledRefs);
  if (assetPaths.has(targetPath)) return `#${assetId(targetPath)}`;
  const target = docs.get(targetPath);
  if (!target) {
    const isDir = [...trackedPaths].some((p) => p.startsWith(`${targetPath.replace(/\/$/, "")}/`));
    if (trackedPaths.has(targetPath) || isDir || targetPath === "") {
      return githubUrl(commit, targetPath, fragment, isDir || targetPath === "");
    }
    return registerUnbundled(file, href, unbundledRefs);
  }
  if (!fragment) return `#${target.id}`;
  const local = target.anchors.get(slug(fragment));
  return local ? `#${local}` : githubUrl(commit, targetPath, fragment);
}

function registerUnbundled(file, href, refs) {
  const key = createHash("sha256").update(`${file}\0${href}`).digest("hex").slice(0, 16);
  const id = `unbundled-${key}`;
  if (!refs.has(id)) refs.set(id, { id, href, sources: new Set() });
  refs.get(id).sources.add(file);
  return `#${id}`;
}

function renderTokens(md, tokens, doc, docs, commit, blobCache, trackedPaths, assetPaths, unbundledRefs) {
  for (const token of tokens) {
    if (token.type === "inline") {
      for (const child of token.children ?? []) {
        if (child.type === "link_open") {
          const href = child.attrGet("href");
          if (href) {
            child.attrSet("href", resolveHref(href, doc.file, docs, commit, trackedPaths, assetPaths, unbundledRefs));
            if (child.attrGet("href")?.startsWith("#unbundled-")) {
              child.attrSet("title", "Local evidence or file path is not packaged in this PDF snapshot");
            }
          }
        } else if (child.type === "image") {
          const src = child.attrGet("src") ?? "";
          if (src && !isExternal(src)) {
            const targetPath = safeLocalPath(doc.file, src.split("#")[0].split("?")[0]);
            if (targetPath && blobCache.has(targetPath)) {
              const bytes = blobCache.get(targetPath);
              const uri = dataUri(targetPath, bytes);
              if (uri) child.attrSet("src", uri);
            } else if (targetPath) {
              child.attrSet("src", githubUrl(commit, targetPath));
            }
          }
        }
      }
    }
  }

  return md.renderer.render(tokens, md.options, { file: doc.file });
}

function installRendererRules(md) {
  const defaultFence = md.renderer.rules.fence;
  md.renderer.rules.fence = (tokens, index, options, env, renderer) => {
    const token = tokens[index];
    if (token.info.trim().toLowerCase() === "mermaid") {
      return `<aside class="diagram-note">Mermaid source included verbatim. This PDF does not render Mermaid diagrams.</aside>\n<pre class="mermaid-source"><code>${md.utils.escapeHtml(token.content)}</code></pre>\n`;
    }
    return defaultFence(tokens, index, options, env, renderer);
  };
}

function htmlDocument(commit, sourceDigest, docs, md, blobCache, trackedPaths, assetPaths, unbundledRefs) {
  const toc = [];
  const bodies = [];
  for (const doc of docs.values()) {
    const rendered = renderTokens(md, doc.tokens, doc, docs, commit, blobCache, trackedPaths, assetPaths, unbundledRefs);
    toc.push(`<li class="toc-doc"><a href="#${doc.id}">${md.utils.escapeHtml(doc.file)}</a></li>`);
    for (const heading of doc.headings) {
      const depth = Math.min(heading.level, 4);
      toc.push(`<li class="toc-h toc-h${depth}"><a href="#${heading.id}">${md.utils.escapeHtml(heading.label)}</a></li>`);
    }
    bodies.push(`<section class="source-doc" id="${doc.id}"><p class="doc-path">${md.utils.escapeHtml(doc.file)}</p>${rendered}</section>`);
  }
  if (assetPaths.size) {
    toc.push(`<li class="toc-doc"><a href="#visual-assets">Embedded visual assets</a></li>`);
    const figures = [...assetPaths].sort().map((file) => {
      const uri = dataUri(file, blobCache.get(file));
      if (!uri) return "";
      return `<figure id="${assetId(file)}"><p class="doc-path">${md.utils.escapeHtml(file)}</p><img src="${uri}" alt="${md.utils.escapeHtml(file)}"></figure>`;
    }).join("\n");
    bodies.push(`<section class="source-doc" id="visual-assets"><p class="doc-path">Tracked first-party images</p><h1>Embedded visual assets</h1><p>Images linked from documentation are embedded here for offline viewing.</p>${figures}</section>`);
  }
  if (unbundledRefs.size) {
    toc.push(`<li class="toc-doc"><a href="#unbundled-local-paths">Local paths not packaged</a></li>`);
    const rows = [...unbundledRefs.values()]
      .sort((a, b) => a.href < b.href ? -1 : a.href > b.href ? 1 : 0)
      .map((ref) => `<li id="${ref.id}"><code>${md.utils.escapeHtml(ref.href)}</code><br><small>Referenced by ${[...ref.sources].sort().map(md.utils.escapeHtml).join(", ")}. This file is not part of the tracked PDF snapshot.</small></li>`);
    bodies.push(`<section class="source-doc" id="unbundled-local-paths"><p class="doc-path">PDF link appendix</p><h1>Local paths not packaged</h1><p>Links to untracked evidence or local files target this appendix. The original file is not included in the repository snapshot. Use durable evidence linked from the document or its pinned Git revision.</p><ul>${rows.join("\n")}</ul></section>`);
  }
  const docCount = docs.size;
  const sourceNote = `Snapshot of ${docCount} tracked first-party Markdown/Mermaid sources at ${commit}. Source SHA-256: ${sourceDigest}. Local documentation links resolve within this PDF when the target and heading are included. Other tracked files link to the matching GitHub revision. Untracked local evidence paths are listed in an appendix.`;
  return `<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'none'; style-src 'unsafe-inline'; img-src data:; font-src data:; connect-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'"><title>Gridpico documentation snapshot</title>
<style>
@page { size: A4; margin: 16mm 15mm 18mm; }
* { box-sizing: border-box; }
body { font: 9pt/1.42 Arial, sans-serif; color: #20252b; }
h1,h2,h3,h4,h5,h6 { color: #142c45; line-height: 1.2; page-break-after: avoid; }
h1 { font-size: 21pt; margin: 20pt 0 9pt; }
h2 { font-size: 16pt; margin: 18pt 0 7pt; }
h3 { font-size: 12pt; margin: 14pt 0 5pt; }
h4,h5,h6 { font-size: 10pt; margin: 11pt 0 4pt; }
p { margin: 0 0 7pt; orphans: 3; widows: 3; }
a { color: #0645ad; text-decoration: underline; }
ul,ol { margin: 3pt 0 7pt; padding-left: 20pt; }
li { margin: 0 0 3pt; }
blockquote { border-left: 2px solid #8ca2b8; margin: 7pt 0; padding: 2pt 9pt; color: #46525e; }
pre,code { font: 7.2pt/1.3 "DejaVu Sans Mono", monospace; }
pre { white-space: pre-wrap; overflow-wrap: anywhere; background: #f2f4f7; border: .5px solid #d6dbe2; padding: 6pt; page-break-inside: auto; }
code { overflow-wrap: anywhere; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 10pt; font-size: 7.6pt; page-break-inside: auto; }
thead { display: table-header-group; }
tr { page-break-inside: avoid; }
th,td { border: .5px solid #aab4bf; padding: 3pt 4pt; vertical-align: top; overflow-wrap: anywhere; }
th { background: #e8edf3; text-align: left; }
img { display: block; max-width: 100%; max-height: 180mm; object-fit: contain; margin: 7pt auto; page-break-inside: avoid; }
hr { border: none; border-top: 1px solid #b6c1cc; margin: 10pt 0; }
.cover { page-break-after: always; }
.cover h1 { font-size: 27pt; margin-top: 0; }
.source-doc { page-break-before: always; }
.doc-path { color: #566575; font: 8pt monospace; border-bottom: 1px solid #aab4bf; padding-bottom: 4pt; }
.toc { page-break-after: always; }
.toc ul { list-style: none; padding-left: 0; }
.toc-doc { font-weight: bold; margin-top: 4pt; }
.toc-h { font-size: 8.2pt; }
.toc-h2 { padding-left: 11pt; }
.toc-h3 { padding-left: 22pt; }
.toc-h4 { padding-left: 33pt; }
.diagram-note { background: #fff4d8; padding: 5pt; margin: 4pt 0; font-size: 8pt; }
.mermaid-source { font-size: 6.5pt; page-break-inside: auto; }
</style></head><body>
<section class="cover"><h1>Gridpico documentation</h1><p>${md.utils.escapeHtml(sourceNote)}</p><p>Tracked images are embedded in an appendix. Mermaid source is included verbatim because this pinned toolchain has no Mermaid renderer.</p></section>
<section class="toc"><h1>Contents</h1><ul>${toc.join("\n")}</ul></section>
${bodies.join("\n")}
</body></html>`;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const commit = git(["rev-parse", "--verify", `${args.ref}^{commit}`]).trim();
  const treePaths = git(["ls-tree", "-r", "-z", "--name-only", commit], true)
    .toString("utf8").split("\0").filter(Boolean);
  const trackedPaths = new Set(treePaths);
  const paths = treePaths.filter(included).sort((a, b) => a < b ? -1 : a > b ? 1 : 0);
  const assetPaths = treePaths.filter((file) =>
    /\.(?:png|jpe?g|gif|svg|webp)$/i.test(file) &&
    !file.split("/").some((part) => ["third_party", "vendor", "node_modules", "build", "dist"].includes(part)))
    .sort((a, b) => a < b ? -1 : a > b ? 1 : 0);
  if (!paths.length) throw new Error(`No first-party Markdown or Mermaid sources in ${commit}`);

  const markdownVersion = require("markdown-it/package.json").version;
  const puppeteerVersion = require("puppeteer-core/package.json").version;
  const chromiumVersion = require("@sparticuz/chromium/package.json").version;
  if (markdownVersion !== "14.1.0" || puppeteerVersion !== "23.8.0" || chromiumVersion !== "131.0.1") {
    throw new Error(`Unexpected renderer versions: markdown-it ${markdownVersion}, puppeteer-core ${puppeteerVersion}, chromium ${chromiumVersion}`);
  }

  const blobCache = new Map();
  const blobs = new Map();
  const sourceHasher = createHash("sha256");
  for (const file of [...paths, ...assetPaths].sort((a, b) => a < b ? -1 : a > b ? 1 : 0)) {
    const bytes = git(["show", `${commit}:${file}`], true);
    blobs.set(file, bytes);
    sourceHasher.update(file, "utf8");
    sourceHasher.update(Buffer.from([0]));
    sourceHasher.update(bytes);
    if (assetPaths.includes(file)) blobCache.set(file, bytes);
  }
  const sourceDigest = sourceHasher.digest("hex");
  const md = new MarkdownIt({ html: true, linkify: true, typographer: false });
  installRendererRules(md);
  const docs = new Map();
  for (const file of paths) {
    if (!/\.(?:md|mmd)$/i.test(file)) continue;
    const source = blobs.get(file).toString("utf8");
    const content = file.endsWith(".mmd") ? `\`\`\`mermaid\n${source}\n\`\`\`` : source;
    const doc = { file, id: docId(file), anchors: new Map(), headings: [], tokens: [] };
    doc.anchors.set("", doc.id);
    doc.tokens = md.parse(content, { file });
    addHeadingIds(doc.tokens, file, doc);
    docs.set(file, doc);
  }
  const unbundledRefs = new Map();
  const html = htmlDocument(commit, sourceDigest, docs, md, blobCache, trackedPaths, new Set(assetPaths), unbundledRefs);
  const output = path.resolve(repo, args.output);
  mkdirSync(path.dirname(output), { recursive: true });
  const profile = mkdtempSync(path.join(os.tmpdir(), "gridpico-pdf-profile-"));
  const useBundledChromium = !process.env.GRIDPICO_CHROMIUM_PATH;
  const executablePath = process.env.GRIDPICO_CHROMIUM_PATH || await chromium.executablePath();
  const env = { ...process.env };
  const fontConfig = process.env.GRIDPICO_FONTCONFIG_FILE || process.env.FONTCONFIG_FILE;
  if (fontConfig) env.FONTCONFIG_FILE = fontConfig;
  else if (useBundledChromium) env.FONTCONFIG_FILE = path.join(path.dirname(executablePath), "fonts", "fonts.conf");
  const browser = await puppeteer.launch({
    executablePath,
    headless: "shell",
    args: chromium.args,
    userDataDir: profile,
    env,
  });
  let linkStats;
  try {
    const page = await browser.newPage();
    await page.setRequestInterception(true);
    page.on("request", (request) => {
      const url = request.url();
      if (url.startsWith("data:") || url.startsWith("about:")) request.continue();
      else request.abort();
    });
    await page.setContent(html, { waitUntil: "load" });
    await page.evaluate(async () => {
      await Promise.all([...document.images].map((img) => img.decode().catch(() => undefined)));
    });
    linkStats = await page.evaluate(() => {
      const internal = [...document.querySelectorAll('a[href^="#"]')];
      const broken = internal.filter((anchor) => {
        const id = decodeURIComponent(anchor.getAttribute("href").slice(1));
        return !document.getElementById(id);
      }).map((anchor) => anchor.outerHTML);
      const images = [...document.images];
      const failedImages = images.filter((img) => img.complete && img.naturalWidth === 0)
        .map((img) => img.getAttribute("alt") || img.getAttribute("src"));
      return {
        internalLinks: internal.length,
        externalLinks: [...document.querySelectorAll('a[href^="http"],a[href^="mailto:"]')].length,
        brokenInternal: broken,
        embeddedImages: images.length,
        failedImages,
      };
    });
    if (linkStats.brokenInternal.length || linkStats.failedImages.length) {
      throw new Error(`PDF preflight failed: broken internal links=${linkStats.brokenInternal.length} ${JSON.stringify(linkStats.brokenInternal.slice(0, 10))}, failed images=${linkStats.failedImages.length}`);
    }
    await page.pdf({
      path: output,
      format: "A4",
      printBackground: true,
      preferCSSPageSize: true,
      displayHeaderFooter: true,
      headerTemplate: "<div></div>",
      footerTemplate: "<div style='font-size:8px;width:100%;text-align:center;color:#667'>Gridpico docs · <span class='pageNumber'></span> / <span class='totalPages'></span></div>",
      margin: { top: "16mm", right: "15mm", bottom: "18mm", left: "15mm" },
    });
  } finally {
    await browser.close();
    rmSync(profile, { recursive: true, force: true });
  }
  const renderedPdf = readFileSync(output);
  const pdfText = renderedPdf.toString("latin1");
  const linkObjects = [...pdfText.matchAll(/\d+\s+\d+\s+obj([\s\S]*?)endobj/g)]
    .map((match) => match[1])
    .filter((object) => /\/Subtype\s*\/Link\b/.test(object));
  const internalPdfLinks = linkObjects.filter((object) => /\/Dest\b/.test(object)).length;
  const externalPdfLinks = linkObjects.filter((object) => /\/S\s*\/URI\b/.test(object)).length;
  const pdfFontObjects = (pdfText.match(/\/Type\s*\/Font\b/g) ?? []).length;
  if (!internalPdfLinks || !externalPdfLinks || !pdfFontObjects) {
    throw new Error(`PDF output check failed: link annotations internal=${internalPdfLinks}, external=${externalPdfLinks}, font objects=${pdfFontObjects}`);
  }
  const commitTime = Number(git(["show", "-s", "--format=%ct", commit]).trim());
  const pdfDate = `D:${new Date(commitTime * 1000).toISOString().replace(/[-:T.Z]/g, "").slice(0, 14)}+00'00'`;
  let pdfBytes = readFileSync(output);
  for (const key of ["CreationDate", "ModDate"]) {
    const pattern = new RegExp(`/${key} \\(D:[^)]*\\)`, "g");
    const contents = pdfBytes.toString("latin1");
    if (!pattern.test(contents)) throw new Error(`Rendered PDF is missing ${key} metadata`);
    pdfBytes = Buffer.from(contents.replace(pattern, `/${key} (${pdfDate})`), "latin1");
  }
  writeFileSync(output, pdfBytes);
  const pdfHash = createHash("sha256").update(readFileSync(output)).digest("hex");
  const manifest = [
    `source-commit ${commit}`,
    `source-sha256 ${sourceDigest}`,
    `markdown-mermaid-count ${paths.length}`,
    `embedded-image-count ${assetPaths.length}`,
    "excluded-path-segments third_party vendor node_modules build dist",
    "included-markdown-mermaid:",
    ...paths.map((file) => `  ${file}`),
    "embedded-images:",
    ...assetPaths.map((file) => `  ${file}`),
    "",
  ].join("\n");
  const manifestPath = `${output}.manifest.txt`;
  writeFileSync(manifestPath, manifest);
  const manifestHash = createHash("sha256").update(manifest).digest("hex");
  process.stdout.write(`PDF: ${path.relative(repo, output)}\nSource commit: ${commit}\nSources: ${paths.length} tracked first-party Markdown/Mermaid files\nSource SHA-256: ${sourceDigest}\nPDF SHA-256: ${pdfHash}\n`);
  process.stdout.write(`Manifest: ${path.relative(repo, manifestPath)}\nManifest SHA-256: ${manifestHash}\n`);
  process.stdout.write(`Preflight: ${linkStats.internalLinks} internal links, ${linkStats.externalLinks} external links, ${linkStats.embeddedImages} images\n`);
  process.stdout.write(`PDF objects: ${internalPdfLinks} internal link annotations, ${externalPdfLinks} external link annotations, ${pdfFontObjects} fonts\n`);
  if (unbundledRefs.size) process.stdout.write(`Unbundled local references: ${unbundledRefs.size} (listed in PDF appendix)\n`);
}

main().catch((error) => {
  process.stderr.write(`${error.stack ?? error}\n`);
  process.exitCode = 1;
});
