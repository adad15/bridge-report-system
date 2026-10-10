import assert from "node:assert/strict";
import { readFile,writeFile } from "node:fs/promises";
import { join,resolve } from "node:path";
export default async({page,context,api,state,origin,log})=>{
 const reportRoot=resolve("templates/report/periodic_inspection_v1");
 if(!state.template){
  const token=await page.evaluate(()=>localStorage.getItem("bridge_report_auth_token"));
  const config=JSON.parse(await readFile(join(reportRoot,"template.json"),"utf8"));
  const response=await context.request.post(origin+"/api/report/templates",{headers:{Authorization:"Bearer "+token},multipart:{
   metadata:JSON.stringify({template_code:"acceptance-v1",template_name:"集成验收模板",contract_type:"periodic_inspection_v1",contract_config:config}),
   file:{name:"periodic-inspection-v1.docx",mimeType:"application/vnd.openxmlformats-officedocument.wordprocessingml.document",buffer:await readFile(join(reportRoot,"periodic-inspection-v1.docx"))}
  }});
  state.template=await response.json();
  assert(response.ok(),JSON.stringify(state.template));
  await api("/api/report/templates/"+state.template.id+"/default","POST",{});
 }
 if(!state.personnel){
  state.personnel=[];
  for(const name of ["验收批准人","验收审核人","验收负责人","验收编制人"])state.personnel.push(await api("/api/report/personnel","POST",{full_name:name,organization:"测试机构",professional_title:"工程师"}));
 }
 if(!state.equipment)state.equipment=await api("/api/report/equipment","POST",{equipment_name:"验收裂缝尺",model_spec:"TEST-01",calibration_valid_until:"2027-12-31"});
 await page.goto(origin+"/bridges/"+state.bridge.id+"/inspections/"+state.year.id+"/report");
 await page.getByText("报告配置",{exact:true}).waitFor();
 state.reportPreflight=await api("/api/inspection-years/"+state.year.id+"/report-preflight");
 await writeFile(join(log,"report-preflight-before-settings.json"),JSON.stringify(state.reportPreflight,null,2));
};
