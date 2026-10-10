import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 const report="/bridges/"+state.bridge.id+"/inspections/"+state.year.id+"/report";
 const result={page_ms:[],api:[],jobs:[]};
 for(let i=0;i<3;i++){
  const started=performance.now();
  await page.goto(origin+report);
  await page.getByText("验收批准人（测试机构）",{exact:true}).waitFor();
  result.page_ms.push(Math.round(performance.now()-started));
  for(const path of ["/api/bridges/"+state.bridge.id+"/overview","/api/inspection-years/"+state.year.id+"/report-preflight","/api/import-records/"+state.realSamples[0].import.id+"/review"]){
   const start=performance.now();await api(path);result.api.push({path,ms:Math.round(performance.now()-start)});
  }
 }
 for(let index=0;index<3;index++){
  let job;
  if(index===0){
   const [response]=await Promise.all([page.waitForResponse(r=>r.request().method()==="POST"&&r.url().endsWith("/report-jobs")),page.getByRole("button",{name:/重新生成/}).click()]);
   assert(response.ok(),await response.text());job=await response.json();
  }else job=await api("/api/inspection-years/"+state.year.id+"/report-jobs","POST",{});
  const deadline=Date.now()+180000;
  while(job.is_running&&Date.now()<deadline){await new Promise(r=>setTimeout(r,1000));job=await api("/api/report-jobs/"+job.id);}
  assert.equal(job.status,"ready",JSON.stringify(job));
  result.jobs.push(job);console.log("Generation sample",index+1,job.progress);
  await writeFile(join(log,"performance.json"),JSON.stringify(result,null,2));
 }
 state.toolsRecovery=true;
 await page.reload();await page.getByRole("button",{name:/下载报告/}).waitFor();
};
