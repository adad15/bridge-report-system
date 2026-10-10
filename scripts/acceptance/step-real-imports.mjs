import assert from "node:assert/strict";
import {readFile,writeFile} from "node:fs/promises";
import {basename,join} from "node:path";
export default async({page,context,api,state,environment,origin,log})=>{
 state.realSamples??=[];
 for(let index=0;index<environment.sources.length;index++){
  if(state.realSamples[index]?.parsed)continue;
  const source=environment.sources[index];
  const input=source.normalized_path||source.path;
  const name=index===0?"百股大桥":"大凌河桥";
  const sample=state.realSamples[index]??={source:source.path};
  if(!sample.bridge)sample.bridge=(await api("/api/bridges","POST",{bridge_name:name,status:"在用"})).bridge;
  if(!sample.year)sample.year=(await api("/api/bridges/"+sample.bridge.id+"/inspection-years","POST",{
   inspection_year:2026,rating_tree_version_id:state.trees.versions.find(tree=>tree.is_default).id
  })).inspection_year;
  const token=await page.evaluate(()=>localStorage.getItem("bridge_report_auth_token"));
  const begin=performance.now();
  const uploaded=await context.request.post(origin+"/api/inspection-years/"+sample.year.id+"/import-records/word",{
   headers:{Authorization:"Bearer "+token},multipart:{source_type:"正式Word",
   file:{name:basename(input),mimeType:"application/vnd.openxmlformats-officedocument.wordprocessingml.document",buffer:await readFile(input)}},timeout:120000});
  assert(uploaded.ok(),await uploaded.text());
  sample.import=(await uploaded.json()).import_record;
  const parseStart=performance.now();
  sample.parse=await api("/api/import-records/"+sample.import.id+"/parse-word","POST",{
   rule_profile:"辽宁国省干线",import_mode:"已有桥年度导入",file_role:"当前年度检测资料",data_role:"当前年度",
   inspection_date:"2026-05-18",report_number:"ACCEPT-REAL-"+index,project_name:"真实报告兼容性验收"
  });
  sample.parseMs=Math.round(performance.now()-parseStart);sample.uploadMs=Math.round(parseStart-begin);
  sample.review=await api("/api/import-records/"+sample.import.id+"/review");
  assert(sample.review.statistics.defect_count>0);
  assert(sample.review.statistics.photo_count>0);
  sample.parsed=true;
  await writeFile(join(log,"real-sample-"+index+".json"),JSON.stringify(sample,null,2));
  await page.goto(origin+"/bridges/"+sample.bridge.id+"/inspections/"+sample.year.id+"/imports/"+sample.import.id+"/review");
  await page.getByText("构件绑定",{exact:true}).first().waitFor();
  await page.screenshot({path:join(log,"real-sample-"+index+".png"),fullPage:true});
  console.log(name,JSON.stringify(sample.review.statistics),sample.parseMs);
 }
};
