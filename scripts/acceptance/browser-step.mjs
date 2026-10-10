import { createRequire } from "node:module";
const { chromium } = createRequire(import.meta.url)(process.env.PLAYWRIGHT_PACKAGE || "playwright");
import { readFile, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { resolve, join } from "node:path";
import { pathToFileURL } from "node:url";
const [manifestPath,actionPath]=process.argv.slice(2);
const environment=JSON.parse(await readFile(manifestPath,"utf8"));
const root=environment.run_directory, log=environment.log_directory;
const statePath=join(root,"browser-state.json"), scenarioPath=join(root,"scenario.json");
const state=existsSync(scenarioPath)?JSON.parse(await readFile(scenarioPath,"utf8")):{};
const origin="http://127.0.0.1:"+environment.ports.frontend;
const browser=await chromium.launch({headless:true,executablePath:process.env.BROWSER_EXECUTABLE||"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"});
const context=await browser.newContext({viewport:{width:1600,height:1050},acceptDownloads:true,...(existsSync(statePath)?{storageState:statePath}:{})});
const page=await context.newPage();
page.setDefaultTimeout(20000);
const records=[],errors=[],lockResponses=[],responseStatus=new Map();
page.on("response",response=>{
 responseStatus.set(response.request(),response.status());
 if(response.request().method()==="POST"&&new URL(response.url()).pathname.endsWith("/edit-lock")&&response.ok()){
  lockResponses.push(response.json().then(data=>({path:new URL(response.url()).pathname,token:data.lock_token})));
 }
});
page.on("pageerror",error=>errors.push(error.message));
page.on("requestfinished",request=>{
 if(request.url().includes("/api/")){
  const timing=request.timing();
  records.push({method:request.method(),path:new URL(request.url()).pathname,status:responseStatus.get(request),ms:Math.round(timing.responseEnd)});
 }
});
const api=async(path,method="GET",body)=>{
 const token=await page.evaluate(()=>localStorage.getItem("bridge_report_auth_token"));
 const response=await context.request.fetch(origin+path,{method,headers:{Authorization:"Bearer "+token},...(body===undefined?{}:{data:body})});
 const result=await response.json();
 if(!response.ok()) throw new Error(method+" "+path+" "+response.status()+" "+JSON.stringify(result));
 return result;
};
const action=(await import(pathToFileURL(resolve(actionPath)).href)).default;
let success=false;
try {
 await page.goto(origin+(state.path||"/workbench"));
 await action({page,context,browser,api,state,environment,root,log,origin});
 if(errors.length)throw new Error("Unexpected browser errors: "+errors.join("; "));
 success=true;
} catch(error) {
 console.error(error.stack); process.exitCode=1;
 await page.screenshot({path:join(log,"browser-failure.png"),fullPage:true});
} finally {
 state.path=new URL(page.url()).pathname;
 await writeFile(scenarioPath,JSON.stringify(state,null,2));
 await context.storageState({path:statePath});
 const body=await page.locator("body").innerText().catch(()=>"");
 await writeFile(join(log,"browser-last-body.txt"),body);
 await writeFile(join(log,"browser-step-"+Date.now()+".json"),JSON.stringify({actionPath,success,errors,records},null,2));
 console.log(JSON.stringify({success,url:page.url(),errors,body:body.slice(0,9000)},null,2));
 const auth=await page.evaluate(()=>localStorage.getItem("bridge_report_auth_token")).catch(()=>null);
 for(const held of await Promise.all(lockResponses)){
  if(auth&&held.token) await context.request.delete(origin+held.path,{headers:{Authorization:"Bearer "+auth,"X-Edit-Lock-Token":held.token}}).catch(()=>{});
 }
 await browser.close();
}
