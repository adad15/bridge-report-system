import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,log})=>{
 const deadline=Date.now()+300000;
 let job;
 do{
  job=await api("/api/report-jobs/"+state.job.id);
  console.log("Report stage",job.status,job.stage);
  if(!job.is_running)break;
  await new Promise(resolve=>setTimeout(resolve,3000));
 }while(Date.now()<deadline);
 await writeFile(join(log,"small-report-job.json"),JSON.stringify(job,null,2));
 assert.equal(job.status,"ready",JSON.stringify(job));
 state.completedJob=job;
 await page.reload();
 const [download]=await Promise.all([
  page.waitForEvent("download"),
  page.getByRole("button",{name:/下载.*Word|下载报告|下载 DOCX/}).click()
 ]);
 state.download=join(log,"small-generated.docx");
 await download.saveAs(state.download);
 assert.equal(await download.failure(),null);
 await page.screenshot({path:join(log,"small-report-ready.png"),fullPage:true});
};
