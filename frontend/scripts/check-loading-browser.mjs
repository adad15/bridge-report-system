// Production smoke using synthetic API data; never connects to a business backend.
// Set PLAYWRIGHT_PACKAGE to an existing playwright package directory if not installed locally.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile, mkdir, writeFile } from "node:fs/promises";
import { resolve, dirname, extname, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_PACKAGE || "playwright");
const reportDir = resolve(root, "../logs/baselines/batch-c");
const report = { measuredAt: new Date().toISOString(), environment: "Headless Chromium, localhost, synthetic API, no throttling; uncompressed JS bytes", samples: [], checks: [] };
const bridge = { id:"b1", system_number:"QL-000001", bridge_name:"测试桥", route_name:"G305", status:"在用", bridge_scale:"大桥", latest_inspection_year:2026, latest_overall_score:85, latest_overall_grade:"2类", pending_count:0 };
const component = { id:"c1", bridge_id:"b1", system_number:"GJ-000001", structure_part:"上部结构", component_type:"梁", business_component_code:"1#梁", current_status:"在用", thread_count:0, unbound_count:0, first_seen_year:2025, latest_seen_year:2026, latest_score:85, latest_score_year:2026 };
const servers = [];
let browser;
async function serve(directory) {
  const server = createServer(async (request, response) => {
    try {
      const pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
      const path = resolve(directory, "." + pathname);
      if (!path.startsWith(directory + sep)) { response.writeHead(403).end(); return; }
      const target = extname(path) ? path : resolve(directory, "index.html");
      const content = await readFile(target);
      response.writeHead(200, { "Content-Type": ({".js":"text/javascript", ".css":"text/css", ".html":"text/html", ".svg":"image/svg+xml"})[extname(target)] || "application/octet-stream" });
      response.end(content);
    } catch { response.writeHead(404).end(); }
  });
  await new Promise((done) => server.listen(0, "127.0.0.1", done));
  servers.push(server);
  return "http://127.0.0.1:" + server.address().port;
}
async function session(origin, { login=true, ratings=[] } = {}) {
  const context = await browser.newContext({ viewport: { width:1440, height:1000 } });
  if (login) await context.addInitScript(() => localStorage.setItem("bridge_report_auth_token", "synthetic-test-token"));
  const unexpected = [];
  await context.route("**/*", async (route) => {
    const url = new URL(route.request().url());
    if (url.origin === origin) return route.continue();
    if (url.origin !== "http://127.0.0.1:18080") { unexpected.push(url.href); return route.abort(); }
    const fixtures = {
      "/api/auth/me": { user: { username:"test", display_name:"测试用户", role:"admin" } },
      "/api/bridges": { bridges:[bridge] },
      "/api/bridges/b1/overview": { bridge },
      "/api/bridges/b1/components": { components:[component] },
      "/api/bridge-components/c1/defect-archive": { component, ratings, threads:[], unbound_observations:[] },
      "/api/standards": { packages:[] },
    };
    if (route.request().method() !== "GET" || !(url.pathname in fixtures)) {
      unexpected.push(route.request().method() + " " + url.pathname);
      return route.fulfill({ status:501, json:{error:"Unexpected synthetic test request"} });
    }
    return route.fulfill({ json:fixtures[url.pathname] });
  });
  const page = await context.newPage();
  const scripts = [];
  const errors = [];
  const navigations = { count: 0 };
  page.on("framenavigated", frame => { if (frame === page.mainFrame()) navigations.count++; });
  page.on("request", request => {
    const url = new URL(request.url());
    if (url.origin === origin && url.pathname.endsWith(".js")) scripts.push(url.pathname.slice(1));
  });
  page.on("pageerror", error => errors.push(error.message));
  return { context, page, scripts, errors, unexpected, navigations };
}
function assertHealthy(state) { assert.deepEqual(state.unexpected, []); assert.deepEqual(state.errors, []); }
const noChart = state => assert(!state.scripts.some(file => file.includes("RatingTrendChart")), "chart loaded before needed");
const noPanels = state => assert(!state.scripts.some(file => /StandardsAdminPanel|CreateBridgeDialog|BridgeInventoryWizard/.test(file)), "hidden panel loaded");
async function measure(version, directory, origin, scenario, path, ready, login=true) {
  const state = await session(origin, {login});
  try {
    await state.page.goto(origin + path);
    await ready(state.page);
    const readyMs = await state.page.evaluate(() => performance.now());
    await state.page.waitForLoadState("networkidle");
    assertHealthy(state);
    if (version === "after") { noChart(state); noPanels(state); }
    const files = [...new Set(state.scripts)];
    const sizes = await Promise.all(files.map(async file => (await readFile(resolve(directory,file))).length));
    report.samples.push({ version, scenario, readyMs:Math.round(readyMs), jsBytes:sizes.reduce((a,b)=>a+b,0), files });
  } finally { await state.context.close(); }
}
try {
  browser = await chromium.launch({ headless:true, ...(process.env.BROWSER_EXECUTABLE ? {executablePath:process.env.BROWSER_EXECUTABLE}: {}) });
  report.browser = browser.version();
  const afterDir = resolve(root, "dist");
  const after = await serve(afterDir);
  const beforeDir = process.argv.includes("--baseline") ? resolve(reportDir, "before-dist") : null;
  const versions = beforeDir ? [["before",beforeDir,await serve(beforeDir)],["after",afterDir,after]] : [["after",afterDir,after]];
  for (const [version, directory, origin] of versions) {
    for (let run=0; run<3; run++) {
      await measure(version,directory,origin,"login","/workbench",page=>page.getByRole("heading",{name:"欢迎登录"}).waitFor(),false);
      await measure(version,directory,origin,"workbench","/workbench",page=>page.getByText("当前没有待处理事项",{exact:true}).waitFor());
      await measure(version,directory,origin,"bridges","/bridges",page=>page.getByRole("button",{name:"测试桥",exact:true}).waitFor());
      await measure(version,directory,origin,"archive-no-score","/bridges/b1/components/c1",page=>page.getByText("该构件暂无年度评分记录。",{exact:true}).waitFor());
    }
  }
  report.checks.push("cold login/workbench/bridges/archive: no chart or hidden panels");
  const warm = await session(after);
  try {
    await warm.page.goto(after+"/workbench");
    await warm.page.getByText("当前没有待处理事项",{exact:true}).waitFor();
    await warm.page.locator('a[href="/bridges"]').first().click();
    await warm.page.getByRole("button",{name:"测试桥",exact:true}).waitFor();
    const loaded = warm.scripts.length;
    await warm.page.locator('a[href="/workbench"]').first().click();
    await warm.page.getByText("当前没有待处理事项",{exact:true}).waitFor();
    await warm.page.locator('a[href="/bridges"]').first().click();
    await warm.page.getByRole("button",{name:"测试桥",exact:true}).waitFor();
    assert.equal(warm.scripts.length, loaded, "cached route modules fetched again");
    noPanels(warm); assertHealthy(warm);
    // Delay the panel code and cancel while the download is pending.
    let release;
    const gate = new Promise(done => { release=done; });
    await warm.page.route("**/CreateBridgeDialog-*.js", async route => { await gate; await route.continue(); });
    await warm.page.getByRole("button",{name:"添加桥梁",exact:true}).click();
    await warm.page.getByRole("button",{name:"取消打开"}).click();
    release();
    await warm.page.waitForLoadState("networkidle");
    assert.equal(await warm.page.getByRole("dialog").count(),0);
    await warm.page.getByRole("button",{name:"添加桥梁",exact:true}).click();
    const dialog = warm.page.getByRole("dialog");
    await dialog.getByLabel("桥梁名称",{exact:true}).fill("未保存草稿");
    const createRequests = warm.scripts.filter(file=>file.includes("CreateBridgeDialog")).length;
    await dialog.getByRole("button",{name:/取\s*消/}).click();
    await warm.page.getByRole("button",{name:"添加桥梁",exact:true}).click();
    assert.equal(await dialog.getByLabel("桥梁名称",{exact:true}).inputValue(),"");
    assert.equal(warm.scripts.filter(file=>file.includes("CreateBridgeDialog")).length,createRequests);
    await dialog.getByRole("button",{name:/取\s*消/}).click();
    await warm.page.getByRole("button",{name:"规则管理",exact:true}).click();
    await warm.page.getByRole("dialog",{name:"规则管理"}).waitFor();
    assert(warm.scripts.some(file=>file.includes("StandardsAdminPanel")));
    assertHealthy(warm);
    report.checks.push("warm route modules reused; delayed create cancelled; reopening resets form without re-fetch; standards loads only on open");
  } finally { await warm.context.close(); }
  const ratings=[{inspection_year:2025,score:90,calculation_details:{},assessment_run_id:"run1"},{inspection_year:2026,score:85,calculation_details:{},assessment_run_id:"run2"}];
  const chart = await session(after,{ratings});
  try {
    await chart.page.goto(after+"/bridges/b1/components/c1");
    await chart.page.getByLabel("构件年度评分趋势").locator("canvas").waitFor();
    assert(chart.scripts.some(file=>file.includes("RatingTrendChart")));
    assertHealthy(chart);
    report.checks.push("scored archive requests chart chunk and renders canvas");
  } finally { await chart.context.close(); }
  const failed = await session(after,{ratings});
  try {
    await failed.page.route("**/RatingTrendChart-*.js", route=>route.abort());
    await failed.page.goto(after+"/bridges/b1/components/c1");
    await failed.page.getByText("评分趋势图暂时无法加载",{exact:true}).waitFor();
    await failed.page.getByText("2025 年：90 分",{exact:true}).waitFor();
    const navigation = failed.navigations.count;
    await failed.page.getByRole("button",{name:/重\s*试/,exact:true}).click();
    await failed.page.getByText("评分趋势图暂时无法加载",{exact:true}).waitFor();
    assert.equal(failed.navigations.count,navigation);
    // Browser module URL failure caching may require a user initiated reload.
    await failed.page.unroute("**/RatingTrendChart-*.js");
    failed.page.once("dialog",dialog=>dialog.accept());
    await failed.page.getByRole("button",{name:"重新加载页面",exact:true}).click();
    await failed.page.getByLabel("构件年度评分趋势").locator("canvas").waitFor();
    assert.deepEqual(failed.unexpected,[]);
    report.checks.push("chart failure retains score text and page shell; repeated retry does not reload; explicit confirmed reload recovers");
  } finally { await failed.context.close(); }
  const routeFailure = await session(after);
  try {
    await routeFailure.page.route("**/BridgesPage-*.js",route=>route.abort());
    await routeFailure.page.goto(after+"/bridges");
    await routeFailure.page.getByText("桥梁档案暂时无法加载",{exact:true}).waitFor();
    const navigation=routeFailure.navigations.count;
    await routeFailure.page.getByRole("button",{name:/重\s*试/,exact:true}).click();
    await routeFailure.page.getByText("桥梁档案暂时无法加载",{exact:true}).waitFor();
    assert.equal(routeFailure.navigations.count,navigation);
    await routeFailure.page.locator('a[href="/workbench"]').first().click();
    await routeFailure.page.getByText("当前没有待处理事项",{exact:true}).waitFor();
    assert.deepEqual(routeFailure.unexpected,[]);
    report.checks.push("route chunk failure stays inside shell; retry does not reload; sidebar navigation remains usable");
  } finally { await routeFailure.context.close(); }
  report.passed = true;
} catch (error) {
  report.passed = false;
  report.error = error.stack;
  process.exitCode = 1;
} finally {
  if (browser) await browser.close();
  await Promise.all(servers.map(server=>new Promise(done=>server.close(done))));
  await mkdir(reportDir,{recursive:true});
  await writeFile(resolve(reportDir,"browser-report.json"),JSON.stringify(report,null,2));
  console.log(JSON.stringify({passed:report.passed, samples:report.samples.length, checks:report.checks, error:report.error},null,2));
}
