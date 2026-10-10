import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({browser,context,state,origin,log})=>{
 const isolated=await browser.newContext({storageState:await context.storageState()});
 try{
  const page=await isolated.newPage();
  let aborted=0;
  await page.route("**/ReportGenerationPage-*.js",route=>{aborted++;return route.abort("failed");});
  await page.goto(origin+"/bridges/"+state.bridge.id+"/inspections/"+state.year.id+"/report");
  await page.getByText(/报告生成暂时无法加载/).waitFor();
  assert(aborted>0);
  await page.getByRole("link",{name:"工作台",exact:true}).click();
  await page.waitForURL("**/workbench");
  await page.getByText(state.bridge.bridge_name,{exact:true}).first().waitFor();
  await page.unroute("**/ReportGenerationPage-*.js");
  await page.goto(origin+"/bridges/"+state.bridge.id+"/inspections/"+state.year.id+"/report");
  await page.getByText("验收批准人（测试机构）",{exact:true}).waitFor();
  state.moduleRecovery=true;
  await writeFile(join(log,"module-recovery.json"),JSON.stringify({aborted,sidebar_navigation:true,reload_recovered:true},null,2));
 }finally{await isolated.close();}
};
