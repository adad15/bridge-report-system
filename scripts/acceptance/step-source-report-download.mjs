import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 const entry=state.sourceReport;
 const year=state.sourceDatabase.years["2026"].year.id;
 const bridge=state.sourceDatabase.bridge.id;
 const job=await api("/api/report-jobs/"+entry.job.id);
 assert.equal(job.inspection_year_id,year);
 assert.equal(job.status,"ready",JSON.stringify(job));
 assert.equal(job.progress.image_count,455);
 entry.latestJob=job;
 await page.goto(origin+"/bridges/"+bridge+"/inspections/"+year+"/report");
 await page.getByText("报告配置",{exact:true}).waitFor();
 const [download]=await Promise.all([
   page.waitForEvent("download"),
   page.getByRole("button",{name:/下载报告|下载.*Word|下载 DOCX/}).click()
 ]);
 entry.download=join(log,entry.previousJob?"source-2026-generated-fixed.docx":"source-2026-generated.docx");
 await download.saveAs(entry.download);
 assert.equal(await download.failure(),null);
 await page.screenshot({path:join(log,"source-report-ready.png"),fullPage:true});
 await writeFile(join(log,"source-report-job-ready.json"),JSON.stringify(job,null,2));
 console.log(JSON.stringify({job_id:job.id,status:job.status,download:entry.download,filename:job.download_filename,progress:job.progress},null,2));
};


