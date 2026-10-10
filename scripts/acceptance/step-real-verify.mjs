import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,context,api,state,origin,log})=>{
 const evidence=[];
 for(const [index,sample] of state.realSamples.entries()){
  const review=await api("/api/import-records/"+sample.import.id+"/review");
  assert.equal(review.statistics.defect_count,[381,374][index]);
  assert.equal(review.statistics.photo_count,[455,406][index]);
  assert.equal(review.bridge.id,sample.bridge.id);
  assert.equal(review.inspection_year.id,sample.year.id);
  const check=await api("/api/inspection-years/"+sample.year.id+"/report-preflight");
  assert.equal(check.can_generate,false);
  const token=await page.evaluate(()=>localStorage.getItem("bridge_report_auth_token"));
  const photos=[];
  for(const photo of [review.parsed_result.photos[0],review.parsed_result.photos.at(-1)]){
   const response=await context.request.get(origin+"/api/import-records/"+sample.import.id+"/photos/"+photo.candidate_id+"/content",{headers:{Authorization:"Bearer "+token}});
   assert(response.ok(),await response.text());const bytes=(await response.body()).length;
   assert(bytes>100);photos.push({candidate_id:photo.candidate_id,bytes});
  }
  evidence.push({bridge:sample.bridge.bridge_name,statistics:review.statistics,photo_fetches:photos,unconfirmed_generation_blocked:true,parse_ms:sample.parseMs,upload_ms:sample.uploadMs});
 }
 await writeFile(join(log,"real-samples-verified.json"),JSON.stringify(evidence,null,2));
};
