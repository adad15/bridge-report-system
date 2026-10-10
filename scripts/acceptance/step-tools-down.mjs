import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 await page.goto(origin+"/bridges/"+state.bridge.id+"/inspections/"+state.year.id+"/report");
 await page.getByRole("button",{name:/重新生成/}).waitFor();
 const [response]=await Promise.all([
  page.waitForResponse(r=>r.request().method()==="POST"&&r.url().endsWith("/report-jobs")),
  page.getByRole("button",{name:/重新生成/}).click()
 ]);
 let result=await response.json();
 const deadline=Date.now()+30000;
 if(response.ok()){
  while(result.is_running&&Date.now()<deadline){
   await new Promise(r=>setTimeout(r,1000));result=await api("/api/report-jobs/"+result.id);
  }
  assert.equal(result.status,"failed");
 }else assert(response.status()>=400);
 await writeFile(join(log,"tools-unavailable.json"),JSON.stringify({status:response.status(),result},null,2));
 await page.screenshot({path:join(log,"tools-unavailable.png"),fullPage:true});
 state.toolsUnavailableHandled=true;
};
