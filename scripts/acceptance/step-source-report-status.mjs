import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({api,state,log})=>{
 const entry=state.sourceReport;
 assert(entry?.job?.id);
 const job=await api("/api/report-jobs/"+entry.job.id);
 entry.latestJob=job;
 await writeFile(join(log,"source-report-job-latest.json"),JSON.stringify(job,null,2));
 console.log(JSON.stringify({id:job.id,status:job.status,is_running:job.is_running,error_code:job.error_code,error_message:job.error_message,progress:job.progress},null,2));
};

