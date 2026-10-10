import { build } from "vite";
import { gzipSync } from "node:zlib";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const args = process.argv.slice(2);
const option = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const outDir = resolve(root, option("--outDir", "dist"));
const reportPath = resolve(root, option("--report", "../logs/baselines/batch-c/build-report.json"));
let chunks = [];
await build({
  root,
  build: { outDir, manifest: true },
  plugins: [{
    name: "report-loading-graph",
    generateBundle(_options, bundle) {
      chunks = Object.values(bundle).filter((item) => item.type === "chunk").map((item) => ({
        file: item.fileName, entry: item.isEntry, facade: item.facadeModuleId?.replaceAll("\\", "/"),
        bytes: Buffer.byteLength(item.code), gzipBytes: gzipSync(item.code).length,
        imports: item.imports, dynamicImports: item.dynamicImports,
        modules: Object.keys(item.modules).map((id) => id.replaceAll("\\", "/")),
      }));
    },
  }],
});
// Read emitted files after Vite's final transforms; count actual UTF-8 bytes.
for (const chunk of chunks) {
  const emitted = await readFile(resolve(outDir, chunk.file));
  chunk.bytes = emitted.length;
  chunk.gzipBytes = gzipSync(emitted).length;
}
const byFile = new Map(chunks.map((item) => [item.file, item]));
function closure(file, seen = new Set()) {
  if (seen.has(file) || !byFile.has(file)) return seen;
  seen.add(file);
  for (const imported of byFile.get(file).imports) closure(imported, seen);
  return seen;
}
const entry = chunks.find((item) => item.entry);
if (!entry) throw new Error("No production entry found");
const staticFiles = [...closure(entry.file)];
const summary = (files) => ({
  files, bytes: files.reduce((sum, name) => sum + byFile.get(name).bytes, 0),
  gzipBytes: files.reduce((sum, name) => sum + byFile.get(name).gzipBytes, 0),
});
const report = { generatedAt: new Date().toISOString(), entry: summary(staticFiles), chunks };
await mkdir(dirname(reportPath), { recursive: true });
await writeFile(reportPath, JSON.stringify(report, null, 2));
console.log("Entry static dependency closure:", JSON.stringify(report.entry));
if (args.includes("--check")) {
  const modules = staticFiles.flatMap((file) => byFile.get(file).modules);
  const forbidden = modules.filter((id) =>
    /\/pages\/(?!LoginPage|LegacyTriageRedirect)/.test(id) ||
    /node_modules\/@(?:ant-design\/charts|antv\/)/.test(id) ||
    /\/(StandardsAdminPanel|CreateBridgeDialog|BridgeInventoryWizard)\.tsx$/.test(id));
  if (forbidden.length) throw new Error("Unexpected startup modules: " + forbidden.join(", "));
  const chart = chunks.find((item) => item.modules.some((id) => id.endsWith("/archive/RatingTrendChart.tsx")));
  if (!chart) throw new Error("Rating chart must have a separate chunk");
  for (const page of chunks.filter((item) => item.modules.some((id) => /\/pages\/.+Page\.tsx$/.test(id)))) {
    if ([...closure(page.file)].some((file) => byFile.get(file).modules.some((id) => /node_modules\/@(?:ant-design\/charts|antv\/)/.test(id)))) {
      throw new Error("Page statically loads chart engine: " + page.file);
    }
  }
  const bridges = chunks.find((item) => item.modules.some((id) => id.endsWith("/pages/BridgesPage.tsx")));
  if (!bridges) throw new Error("Bridge list chunk missing");
  const hidden = [...closure(bridges.file)].flatMap((file) => byFile.get(file).modules)
    .filter((id) => /\/(StandardsAdminPanel|CreateBridgeDialog|BridgeInventoryWizard)\.tsx$/.test(id));
  if (hidden.length) throw new Error("Bridge list statically loads hidden panels: " + hidden.join(", "));
  console.log("Loading boundaries verified.");
}
