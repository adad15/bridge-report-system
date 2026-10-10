import assert from "node:assert/strict";
import {readFile,writeFile} from "node:fs/promises";
import {join,resolve} from "node:path";
export default async({page,context,api,state,origin,log})=>{
 const login=page.getByRole("heading",{name:"欢迎登录"});
 if(await login.isVisible()){
   await page.getByLabel("用户名",{exact:true}).fill("admin");
   await page.getByLabel("密码",{exact:true}).fill("admin123");
   await page.getByRole("button",{name:/登\s*录/,exact:true}).click();
   await page.getByText("退出登录",{exact:true}).waitFor();
 }
 const entry=state.sourceReport;
 const year=state.sourceDatabase.years["2026"].year.id;
 const bridge=state.sourceDatabase.bridge.id;
 const before=await api("/api/inspection-years/"+year+"/report-settings");
 assert.equal(before.inspection_date,null);
 assert.equal(before.report_number,null);
 const token=await page.evaluate(()=>localStorage.getItem("bridge_report_auth_token"));
 const templatePath=resolve("templates/report/periodic_inspection_v1/periodic-inspection-v1.docx");
 const response=await context.request.post(origin+"/api/report/templates/"+state.template.id+"/file",{
   headers:{Authorization:"Bearer "+token},
   multipart:{file:{name:"periodic-inspection-v1.docx",
     mimeType:"application/vnd.openxmlformats-officedocument.wordprocessingml.document",
     buffer:await readFile(templatePath)}}
 });
 const replaced=await response.json();
 assert(response.ok(),JSON.stringify(replaced));
 assert.notEqual(replaced.file_checksum,state.template.file_checksum);
 state.template=replaced;
 await writeFile(join(log,"source-report-template-replaced.json"),JSON.stringify(replaced,null,2));
 await page.goto(origin+"/bridges/"+bridge+"/inspections/"+year+"/report");
 await page.getByText("报告配置",{exact:true}).waitFor();
 await page.getByLabel("检测日期",{exact:true}).fill("2026-03-18");
 await page.getByLabel("报告编号",{exact:true}).fill("ACCEPT-REPORT-2026");
 await page.getByLabel("项目名称",{exact:true}).fill("百股大桥2026年度定期检测（隔离验收）");
 await page.getByLabel("检测单位",{exact:true}).fill("验收检测机构");
 const [savedResponse]=await Promise.all([
   page.waitForResponse(r=>r.url().endsWith("/report-settings")&&r.request().method()==="PUT"),
   page.getByRole("button",{name:/保存配置/}).click()
 ]);
 assert(savedResponse.ok(),await savedResponse.text());
 const saved=await api("/api/inspection-years/"+year+"/report-settings");
 assert.equal(saved.inspection_date,"2026-03-18");
 assert.equal(saved.report_number,"ACCEPT-REPORT-2026");
 assert.equal(saved.project_name,"百股大桥2026年度定期检测（隔离验收）");
 assert.equal(saved.inspection_org,"验收检测机构");
 assert.equal(saved.template_id,state.template.id);
 await writeFile(join(log,"source-report-settings-fixed.json"),JSON.stringify(saved,null,2));
 await page.screenshot({path:join(log,"source-report-configured-fixed.png"),fullPage:true});
 const preflight=await api("/api/inspection-years/"+year+"/report-preflight");
 assert.equal(preflight.can_generate,true,JSON.stringify(preflight));
 const responsePromise=page.waitForResponse(r=>r.url().endsWith("/report-jobs")&&r.request().method()==="POST");
 await page.getByRole("button",{name:/重新生成/}).click();
 const jobResponse=await responsePromise;
 const job=await jobResponse.json();
 assert(jobResponse.ok(),JSON.stringify(job));
 assert.notEqual(job.id,entry.job.id);
 assert.equal(job.template_checksum,replaced.file_checksum);
 entry.previousJob=entry.job;
 entry.job=job;
 entry.startedAt=new Date().toISOString();
 await writeFile(join(log,"source-report-job-fixed-started.json"),JSON.stringify(job,null,2));
 console.log(JSON.stringify({saved_cover:{inspection_date:saved.inspection_date,report_number:saved.report_number,project_name:saved.project_name,inspection_org:saved.inspection_org},template_checksum:replaced.file_checksum,job_id:job.id,status:job.status},null,2));
};


