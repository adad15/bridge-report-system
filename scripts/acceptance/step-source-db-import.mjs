import assert from "node:assert/strict";
import {readFile,writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 const login=page.getByRole("heading",{name:"欢迎登录"});
 await login.waitFor({timeout:3000}).catch(()=>{});
 if(await login.isVisible()){
  await page.getByLabel("用户名",{exact:true}).fill("admin");
  await page.getByLabel("密码",{exact:true}).fill("admin123");
  await page.getByRole("button",{name:/登.*录/}).click();
  await page.getByRole("button",{name:/退出登录/}).waitFor();
 }
 state.trees??=await api("/api/rating-trees");
 const source=JSON.parse(await readFile(process.env.SOURCE_DB_MANIFEST,"utf8"));
 state.sourceDatabase??={snapshot:source.snapshot_path,years:{}};
 const data=state.sourceDatabase;
 if(!data.bridge)data.bridge=(await api("/api/bridges","POST",{bridge_name:"百股大桥",route_number:"S320",status:"在用"})).bridge;
 for(const [year,taskId,date,defects,photos] of [
  [2024,"4ec7bd71-2c0c-4f19-9ba1-0d1a17b44a20","2024-06-21",279,166],
  [2025,"7ce48f01-cdae-41fc-826b-e4163cd591d4","2025-06-20",314,253],
  [2026,"ccaa788b-52b6-4569-8a16-c7bccab8219d","2026-03-18",381,455]
 ]){
  const sample=data.years[year]??={taskId};
  if(sample.verified)continue;
  if(!sample.year)sample.year=(await api("/api/bridges/"+data.bridge.id+"/inspection-years","POST",{
   inspection_year:year,rating_tree_version_id:state.trees.versions.find(t=>t.is_default).id
  })).inspection_year;
  await page.goto(origin+"/bridges/"+data.bridge.id+"/inspections/"+sample.year.id);
  await page.getByRole("button",{name:"导入检测资料",exact:true}).first().click();
  const dialog=page.getByRole("dialog",{name:"导入检测资料"});
  await page.waitForFunction(()=>{
   const path=document.getElementById("import-source-db-path"),task=document.getElementById("import-source-task");
   return path?.value && !path.disabled && task && !task.disabled;
  });
  await dialog.getByLabel("离线库路径",{exact:true}).fill(data.snapshot);
  const [tasksResponse]=await Promise.all([
   page.waitForResponse(r=>r.url().endsWith("/source-imports/tasks")&&r.request().method()==="POST"),
   dialog.getByRole("button",{name:/重新读取/}).click()
  ]);
  assert(tasksResponse.ok(),await tasksResponse.text());
  const tasks=(await tasksResponse.json()).tasks;
  const task=tasks.find(t=>t.task_id===taskId);assert(task);
  await dialog.getByLabel("检测任务",{exact:true}).click();
  const label=[task.name,task.check_date??"未填日期",task.defect_count+" 条病害",task.photo_count+" 张照片"].join(" · ");
  await page.getByText(label,{exact:true}).last().click();
  await dialog.getByLabel("检查日期",{exact:true}).fill(date);
  await dialog.getByLabel("检查日期",{exact:true}).press("Enter");
  await dialog.getByLabel("报告编号",{exact:true}).fill("SOURCE-ACCEPT-"+year);
  await dialog.getByLabel("项目名称",{exact:true}).fill("来源软件数据库验收 "+year);
  const begin=performance.now();
  await dialog.getByRole("button",{name:"开始导入",exact:true}).click();
  await page.waitForURL(/\/imports\/[^/]+\/review$/,{timeout:120000});
  sample.importId=new URL(page.url()).pathname.split("/").at(-2);
  await page.getByText("构件绑定",{exact:true}).first().waitFor();
  const review=await api("/api/import-records/"+sample.importId+"/review");
  sample.elapsedMs=Math.round(performance.now()-begin);
  assert.equal(review.parsed_result.contract.parser_name,"source_db_importer");
  assert.equal(review.parsed_result.import_context.source_type,"接口同步");
  assert.equal(review.statistics.defect_count,defects);assert.equal(review.statistics.photo_count,photos);
  assert(review.parsed_result.defects.every(d=>d.source_defect_indicator_id));
  assert(review.parsed_result.photos.every(p=>p.photo_number==null&&p.linked_defect_candidate_id));
  sample.statistics=review.statistics;sample.verified=true;
  sample.sourceTask=task;sample.suppliedInspectionDate=date;
  await writeFile(join(log,"source-db-review-"+year+".json"),JSON.stringify(review,null,2));
  await page.screenshot({path:join(log,"source-db-review-"+year+".png"),fullPage:true});
  console.log(year,JSON.stringify(sample.statistics),sample.elapsedMs);
 }
 await writeFile(join(log,"source-db-import-summary.json"),JSON.stringify(data,null,2));
};
