import assert from "node:assert/strict";
import {readFile,writeFile} from "node:fs/promises";
import {join,basename} from "node:path";
export default async({page,context,state,environment,origin,log})=>{
 const sample=state.realSamples[1],source=environment.sources[1];
 const token=await page.evaluate(()=>localStorage.getItem("bridge_report_auth_token"));
 const options={headers:{Authorization:"Bearer "+token}};
 const uploaded=await context.request.post(origin+"/api/inspection-years/"+sample.year.id+"/import-records/word",{
  ...options,multipart:{source_type:"正式Word",file:{name:basename(source.path),mimeType:"application/vnd.openxmlformats-officedocument.wordprocessingml.document",buffer:await readFile(source.path)}}
 });
 assert(uploaded.ok());const record=(await uploaded.json()).import_record;
 const response=await context.request.post(origin+"/api/import-records/"+record.id+"/parse-word",{
  ...options,data:{rule_profile:"辽宁国省干线",import_mode:"已有桥年度导入",file_role:"当前年度检测资料",data_role:"当前年度",inspection_date:"2026-05-18",report_number:"FORMAT-ERROR",project_name:"错误格式回归"}
 });
 const result=await response.json();
 assert.equal(response.status(),400);assert.equal(result.code,"docx_open_failed");assert(result.message.includes("另存为"));
 const gone=await context.request.get(origin+"/api/import-records/"+record.id+"/review",options);
 assert.equal(gone.status(),404);
 await writeFile(join(log,"format-error.json"),JSON.stringify({status:response.status(),result,failed_import_removed:gone.status()===404},null,2));
};
