import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 assert.equal(origin,"http://127.0.0.1:25173");
 const data=state.sourceConfirmation,path="/api/import-records/"+data.importId;
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 const [preflight]=await Promise.all([page.waitForResponse(r=>r.url().includes("preflight")&&r.request().method()==="POST"),page.getByRole("button",{name:"入库前检查",exact:true}).click()]);
 const check=await preflight.json();assert(check.can_confirm,JSON.stringify(check.blocking_errors));
 const [response]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/confirm")&&r.request().method()==="POST",{timeout:60000}),page.getByRole("button",{name:"确认年度事实入库",exact:true}).click()]);
 const result=await response.json();
 await writeFile(join(log,"source-confirm-written.json"),JSON.stringify(result,null,2));
 assert(response.ok(),JSON.stringify(result));
 data.confirmation=result;
 console.log("Confirmed:",JSON.stringify(result));
 await page.getByRole("button",{name:"解锁全部修改",exact:true}).waitFor();
 await writeFile(join(log,"source-confirm-final-review.json"),JSON.stringify(await api(path+"/review"),null,2));
 await page.screenshot({path:join(log,"source-confirm-final.png"),fullPage:true});
};