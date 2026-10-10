import assert from "node:assert/strict";
import {readFile,writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 assert.equal(origin,"http://127.0.0.1:25173");
 const login=page.getByRole("heading",{name:"欢迎登录"});
 await login.waitFor({timeout:3000}).catch(()=>{});
 if(await login.isVisible()){
  await page.getByLabel("用户名",{exact:true}).fill("admin");
  await page.getByLabel("密码",{exact:true}).fill("admin123");
  await page.getByRole("button",{name:/登.*录/}).click();
  await page.getByRole("button",{name:/退出登录/}).waitFor();
 }
 const data=state.sourceConfirmation??={};
 const bridge=state.sourceDatabase.bridge;
 const reference=JSON.parse(await readFile("logs/source-db-acceptance/20260920-235718-b1a251/binding-and-fidelity/confirmed-inventory-current.json","utf8"));
 assert.equal(reference.entries.length,5174);
 if(!data.inventory){
  const parts=[
   ["lower.pier_column","墩柱",[4]],["lower.pier_cap","盖梁",[]],
   ["lower.abutment_body","台",[]],["lower.abutment_cap","台帽",[]],
   ["lower.foundation","基础",[]],["lower.wing_wall","翼墙",[]],
   ["lower.cone_slope","锥坡",[]],["lower.protection_slope","护坡",[]],
   ["lower.riverbed","河床",[]],["deck.pavement","桥面铺装",[]],
   ["deck.expansion_joint","伸缩缝",[9]],["deck.sidewalk","人行道",[]],
   ["deck.railing","栏杆",[]],["deck.drainage","排水系统",[]],
   ["deck.lighting","照明、标志",[]],["beam.girder","板",[25]],
   ["beam.wet_joint","铰缝",[24]],["bearing.support","支座",[50]]
  ];
  const packages=(await api("/api/standards")).packages;
  const standard=packages.find(p=>p.family==="technical_condition"&&p.package_version==="1.0.4"&&p.is_enabled);assert(standard);
  data.inventory=await api("/api/bridges/"+bridge.id+"/component-inventories/generate","POST",{
   standard_package_id:standard.id,bridge_type_id:"h21.bridge_type.beam",span_count:33,
   part_selections:parts.map(([part_key,site_name,counts])=>({part_key,site_name,counts}))
  });
 }
 const revision=data.inventory.revision.id;
 const entries=[];
 for(const group of data.inventory.groups){
  for(let offset=0;;offset++){
   const result=await api("/api/component-inventories/"+revision+"/entries?group="+encodeURIComponent(group.site_component_type)+"&page="+offset+"&size=200");
   entries.push(...result.entries);
   if((offset+1)*200>=result.total)break;
  }
 }
 const signatures=items=>items.map(e=>JSON.stringify([e.component_number,e.site_component_type,e.mappings.filter(m=>m.is_active).map(m=>[m.standard_component_category_id,m.structure_part]).sort()])).sort();
 assert.deepEqual(signatures(entries),signatures(reference.entries));
 data.inventoryVerified=true;
 await writeFile(join(log,"source-confirm-inventory.json"),JSON.stringify({equivalent:true,reference_revision:reference.revision,revision,entries},null,2));
 await page.goto(origin+"/bridges/"+bridge.id+"/inventory");
 await page.getByRole("heading",{name:"实际构件台账",exact:true}).waitFor();
 const pending=page.getByRole("button",{name:"一键确认全部待确认映射",exact:true});
 if(await pending.isVisible())await pending.click();
 const confirm=page.getByRole("button",{name:"确认本版台账",exact:true});
 if(await confirm.isVisible()){await confirm.click();await confirm.waitFor({state:"hidden"});}
 data.inventory=await api("/api/bridges/"+bridge.id+"/component-inventories/latest/summary");
 assert.equal(data.inventory.revision.status,"已确认");
 console.log("Inventory equivalent and confirmed:",entries.length,revision);
 if(!data.importId){
  await page.goto(origin+"/bridges/"+bridge.id+"/inspections/"+state.sourceDatabase.years[2026].year.id);
  await page.getByRole("button",{name:/导入资料/}).first().click();
  const dialog=page.getByRole("dialog",{name:"导入检测资料"});
  await page.waitForFunction(()=>{const p=document.getElementById("import-source-db-path"),t=document.getElementById("import-source-task");return p?.value&&!p.disabled&&t&&!t.disabled;});
  await dialog.getByLabel("离线库路径",{exact:true}).fill(state.sourceDatabase.snapshot);
  const [response]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/source-imports/tasks")&&r.request().method()==="POST"),dialog.getByRole("button",{name:/重新读取/}).click()]);
  assert(response.ok(),await response.text());
  const task=(await response.json()).tasks.find(t=>t.task_id==="ccaa788b-52b6-4569-8a16-c7bccab8219d");assert(task);
  await dialog.getByLabel("检测任务",{exact:true}).click();
  await page.getByText([task.name,task.check_date??"未填日期",task.defect_count+" 条病害",task.photo_count+" 张照片"].join(" · "),{exact:true}).last().click();
  await dialog.getByLabel("检查日期",{exact:true}).fill("2026-03-18");
  await dialog.getByLabel("检查日期",{exact:true}).press("Enter");
  await dialog.getByLabel("报告编号",{exact:true}).fill("SOURCE-CONFIRM-2026");
  await dialog.getByLabel("项目名称",{exact:true}).fill("来源软件数据库正式入库验收");
  await dialog.getByRole("button",{name:"开始导入",exact:true}).click();
  await page.waitForURL(/\/imports\/[^/]+\/review$/,{timeout:120000});
  data.importId=new URL(page.url()).pathname.split("/").at(-2);
 }
 const review=await api("/api/import-records/"+data.importId+"/review");
 assert.equal(review.parsed_result.contract.parser_version,"0.1.1");
 assert.equal(review.statistics.defect_count,381);assert.equal(review.statistics.photo_count,455);
 await writeFile(join(log,"source-confirm-initial-review.json"),JSON.stringify(review,null,2));
 const workspace=await api("/api/import-records/"+data.importId+"/resolution-workspace");
 await writeFile(join(log,"source-confirm-initial-workspace.json"),JSON.stringify(workspace,null,2));
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 console.log("Source imported:",data.importId,workspace.progress);
};