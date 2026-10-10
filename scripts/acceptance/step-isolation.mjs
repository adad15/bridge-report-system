import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 const yearPath=(bridge,year)=>"/bridges/"+bridge+"/inspections/"+year;
 const reportPath=(bridge,year)=>yearPath(bridge,year)+"/report";
 const reviewPath=sample=>yearPath(sample.bridge.id,sample.year.id)+"/imports/"+sample.import.id+"/review";
 const navigate=path=>page.evaluate(path=>{history.pushState({},"",path);dispatchEvent(new PopStateEvent("popstate"));},path);
 if(!state.year2025)state.year2025=(await api("/api/bridges/"+state.bridge.id+"/inspection-years","POST",{
  inspection_year:2025,rating_tree_version_id:state.trees.versions.find(t=>t.is_default).id
 })).inspection_year;
 const oldPath="/api/inspection-years/"+state.year.id+"/report-settings";
 await page.goto(origin+"/workbench");
 let release,arrived;
 const gate=new Promise(r=>release=r), seen=new Promise(r=>arrived=r);
 await page.route("**"+oldPath,async route=>{
  const response=await route.fetch();arrived();await gate;
  await route.fulfill({response}).catch(()=>{});
 });
 await navigate(reportPath(state.bridge.id,state.year.id));
 await seen;
 await navigate(reportPath(state.bridge.id,state.year2025.id));
 await page.getByText(/2025 年度定期检测报告/).waitFor();
 await page.getByText("还没有配置签字人员。",{exact:true}).waitFor();
 release();
 await page.unroute("**"+oldPath);
 await page.waitForTimeout(300);
 assert.equal(await page.getByText("验收批准人（测试机构）",{exact:true}).count(),0);
 assert.equal((await api("/api/inspection-years/"+state.year2025.id+"/report-settings")).template_id,null);
 const sample=state.realSamples[0];
 await navigate(reportPath(sample.bridge.id,sample.year.id));
 await page.getByText("还没有配置签字人员。",{exact:true}).waitFor();
 assert.equal((await api("/api/inspection-years/"+sample.year.id+"/report-settings")).template_id,null);
 state.reportIsolation=true;
 // Save a visible edit, leave normally, return and verify persisted draft version.
 await navigate(reviewPath(sample));
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 const before=await api("/api/import-records/"+sample.import.id+"/review");
 await page.getByText("病害与照片",{exact:true}).click();
 await page.getByRole("button",{name:/编.*辑/,exact:true}).first().click();
 const location=page.getByLabel("病害位置",{exact:true});
 const original=await location.inputValue();
 const marker=original+"（验收草稿恢复）";
 await location.fill(marker);
 await location.press("Tab");
 await page.getByText(/有未保存的修改/).waitFor();
 const [save]=await Promise.all([
  page.waitForResponse(r=>r.url().endsWith("/review-draft")&&r.request().method()==="PUT"),
  page.getByRole("button",{name:"保存草稿",exact:true}).click()
 ]);
 assert(save.ok(),await save.text());
 const saved=await api("/api/import-records/"+sample.import.id+"/review");
 assert(saved.import_record.draft_version>before.import_record.draft_version);
 assert(saved.parsed_result.defects.some(d=>d.defect_location===marker));
 await navigate("/workbench");
 await page.getByRole("heading",{name:"工作台",exact:true}).waitFor().catch(()=>{});
 await navigate(reviewPath(sample));
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 await page.getByText("病害与照片",{exact:true}).click();
 await page.getByText(marker,{exact:true}).first().waitFor();
 state.draftRestore=true;
 // Delay the previous import response while navigating to another real bridge.
 await navigate("/workbench");
 let releaseReview,arrivedReview;
 const reviewGate=new Promise(r=>releaseReview=r),reviewSeen=new Promise(r=>arrivedReview=r);
 const oldReview="/api/import-records/"+state.importId+"/review";
 await page.route("**"+oldReview,async route=>{
  const response=await route.fetch();arrivedReview();await reviewGate;await route.fulfill({response}).catch(()=>{});
 });
 await navigate(yearPath(state.bridge.id,state.year.id)+"/imports/"+state.importId+"/review");
 await reviewSeen;
 const other=state.realSamples[1];
 await navigate(reviewPath(other));
 await page.getByText(other.bridge.bridge_name+" · "+other.import.system_number,{exact:true}).waitFor();
 releaseReview();await page.unroute("**"+oldReview);await page.waitForTimeout(300);
 await page.getByText(other.bridge.bridge_name+" · "+other.import.system_number,{exact:true}).waitFor();
 assert.equal(await page.getByText(state.bridge.bridge_name+" · DRJL-000001",{exact:true}).count(),0);
 state.importIsolation=true;
 await writeFile(join(log,"isolation.json"),JSON.stringify({
  report_year_switch:state.reportIsolation,import_bridge_switch:state.importIsolation,draft_restore:state.draftRestore,
  draft_version_before:before.import_record.draft_version,draft_version_after:saved.import_record.draft_version,
  delayed_responses:"actual HTTP response fetched from isolated backend, delivery held until navigation"
 },null,2));
};
