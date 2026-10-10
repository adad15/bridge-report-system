import assert from "node:assert/strict";
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
export default async({page,api,state,log})=>{
 await page.getByText("报告配置",{exact:true}).waitFor();
 await page.getByRole("combobox").nth(0).click();
 await page.getByText("集成验收模板（默认）",{exact:true}).last().click();
 const roles=["批准","审核","检测负责人","编制"];
 for(let index=0;index<4;index++){
  await page.getByRole("button",{name:"添加一行",exact:true}).nth(0).click();
  await page.getByRole("combobox").nth(2+index*2).click();
  await page.getByText(state.personnel[index].full_name+"（测试机构）",{exact:true}).last().click();
  if(index){
   await page.getByRole("combobox").nth(3+index*2).click();
   await page.getByText(roles[index],{exact:true}).last().click();
  }
 }
 await page.getByRole("button",{name:"添加一行",exact:true}).nth(1).click();
 await page.getByRole("combobox").nth(10).click();
 await page.getByText("验收裂缝尺（TEST-01）",{exact:true}).last().click();
 await page.getByPlaceholder("用途，如裂缝宽度").fill("病害尺寸测量");
 const [saved]=await Promise.all([
  page.waitForResponse(r=>r.url().endsWith("/report-settings")&&r.request().method()==="PUT"),
  page.getByRole("button",{name:/保存配置/}).click()
 ]);
 assert(saved.ok(),await saved.text());
 const check=await api("/api/inspection-years/"+state.year.id+"/report-preflight");
 await writeFile(join(log,"report-preflight-ready.json"),JSON.stringify(check,null,2));
 assert(check.can_generate,JSON.stringify(check));
 const response=page.waitForResponse(r=>r.url().endsWith("/report-jobs")&&r.request().method()==="POST");
 const started=Date.now();
 await page.getByRole("button",{name:/生成报告/}).click();
 const jobResponse=await response;
 state.job=await jobResponse.json();
 assert(jobResponse.ok(),JSON.stringify(state.job));
 const duplicate=await api("/api/inspection-years/"+state.year.id+"/report-jobs","POST",{});
 assert.equal(duplicate.id,state.job.id,"Repeated generation must reuse active job");
 state.duplicateJobReused=true;
 await page.reload();
 await page.getByText("报告配置",{exact:true}).waitFor();
 state.jobStartedAt=started;
 await writeFile(join(log,"small-assessment.json"),JSON.stringify(await api("/api/import-records/"+state.importId+"/assessment-report"),null,2));
};
