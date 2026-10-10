import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 assert.equal(origin,"http://127.0.0.1:25173");
 const data=state.sourceConfirmation;
 const path="/api/import-records/"+data.importId;
 const targetPath="/bridges/"+state.sourceDatabase.bridge.id+"/inspections/"+state.sourceDatabase.years[2026].year.id+"/imports/"+data.importId+"/review";
 if(new URL(page.url()).pathname!==targetPath)await page.goto(origin+targetPath);
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 let workspace=await api(path+"/resolution-workspace");
 console.log("Before:",workspace.progress,workspace.groups.filter(g=>g.status!=="bound").map(g=>({number:g.source_component_number,split:g.split_eligible,side:g.side_pair_option})));
 await page.getByRole("button",{name:/^待处理 \d+/}).click();
 for(const [source,target] of [["0#台盖梁","0#台帽"],["33#台盖梁","33#台帽"],["管养牌","照明、标志"]]){
  const group=workspace.groups.find(g=>g.source_component_number===source);assert(group);
  if(group.status==="bound")continue;
  const input=page.getByRole("textbox",{name:"搜索实际构件 "+source,exact:true});
  await input.fill(target);
  await page.waitForResponse(r=>r.url().includes("/entries?")&&decodeURIComponent(r.url()).includes(target)&&r.ok());
  await page.getByRole("combobox",{name:"为 "+source+" 选择实际构件",exact:true}).click();
  const selected=page.getByText(target+" / ",{exact:false}).last();
  const [response]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/resolution")&&r.request().method()==="PUT"),selected.click()]);
  assert(response.ok(),await response.text());
  await page.getByRole("textbox",{name:"搜索实际构件 "+source,exact:true}).waitFor({state:"hidden"});
  workspace=await api(path+"/resolution-workspace");
  console.log("Explicit binding:",source,target);
 }
 for(const group of workspace.groups.filter(g=>g.status!=="bound"&&g.side_pair_option)){
  await page.getByRole("combobox",{name:"为 "+group.source_component_number+" 选择实际构件",exact:true}).click();
  const [response]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/resolution")&&r.request().method()==="PUT"),page.getByText(group.side_pair_option.label,{exact:true}).last().click()]);
  assert(response.ok(),await response.text());
  await page.getByRole("combobox",{name:"为 "+group.source_component_number+" 选择实际构件",exact:true}).waitFor({state:"hidden"});
 }
 workspace=await api(path+"/resolution-workspace");
 const ranges=workspace.groups.filter(g=>g.status!=="bound"&&g.split_eligible);
 if(ranges.length){
  for(const group of ranges)await page.getByRole("checkbox",{name:"选择拆分 "+group.source_component_number,exact:true}).check();
  await page.getByRole("button",{name:"预览拆分影响",exact:true}).click();
  const dialog=page.getByRole("dialog",{name:"拆分构件范围"});
  const [response]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/apply")&&r.request().method()==="POST"),dialog.getByRole("button",{name:"确认拆分",exact:true}).click()]);
  assert(response.ok(),await response.text());
  await dialog.waitFor({state:"hidden"});
 }
 workspace=await api(path+"/resolution-workspace");
 await writeFile(join(log,"source-confirm-bound-workspace.json"),JSON.stringify(workspace,null,2));
 assert.equal(workspace.progress.unresolved_count,0);
 assert.equal(workspace.progress.active_instance_count,430);
 const review=await api(path+"/review");
 await writeFile(join(log,"source-confirm-bound-review.json"),JSON.stringify(review,null,2));
 data.bindingVerified=true;
 await page.screenshot({path:join(log,"source-confirm-bound.png"),fullPage:true});
 const [preflight]=await Promise.all([page.waitForResponse(r=>r.url().includes("preflight")&&r.request().method()==="POST"),page.getByRole("button",{name:"入库前检查",exact:true}).click()]);
 const check=await preflight.json();
 await writeFile(join(log,"source-confirm-before-review-preflight.json"),JSON.stringify(check,null,2));
 assert.equal(check.can_confirm,false);
 await page.getByText("病害与照片",{exact:true}).click();
 console.log("After:",workspace.progress);
};