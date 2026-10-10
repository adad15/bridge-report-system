import assert from "node:assert/strict";
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
export default async({page,context,browser,api,state,log})=>{
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 const second=await browser.newContext({storageState:await context.storageState()});
 try{
  const other=await second.newPage();await other.goto(page.url());
  await other.getByText("你已在另一个页面正在编辑此导入记录。",{exact:true}).first().waitFor();
  assert.equal(await other.getByRole("button",{name:"保存草稿",exact:true}).count(),0);
  state.lockIsolation=true;
 }finally{await second.close();}
 await page.getByText("病害与照片",{exact:true}).click();
 await page.getByRole("button",{name:/编\s*辑/,exact:true}).first().click();
 const editor=page.getByRole("group",{name:"病害详情维护",exact:true});
 await editor.waitFor();
 for(const [index,node] of [[0,"5.1.1-1 蜂窝、麻面"],[1,"5.1.1-2 剥落、掉角"]]){
  if(index){
   await page.getByRole("button",{name:"关闭精细维护",exact:true}).click();
   await page.getByRole("row").filter({hasText:"左侧端部"}).getByRole("button",{name:/编\s*辑/,exact:true}).click();
  }
  await editor.getByLabel("评定树病害",{exact:true}).click();
  await page.getByText(node,{exact:true}).last().click();
  await editor.getByRole("button",{name:/^确\s*认$/}).click();
 }
 const saved=page.waitForResponse(r=>r.url().endsWith("/review-draft")&&r.request().method()==="PUT");
 await page.getByRole("button",{name:"保存草稿",exact:true}).click();
 assert((await saved).ok());
 const review=await api("/api/import-records/"+state.importId+"/review");
 await writeFile(join(log,"small-review-saved.json"),JSON.stringify(review,null,2));
 console.log("Saved statistics:",review.statistics);
 const preflight=page.waitForResponse(r=>r.url().includes("preflight")&&r.request().method()==="POST");
 await page.getByRole("button",{name:"入库前检查",exact:true}).click();
 const check=await preflight;
 const body=await check.json();
 await writeFile(join(log,"small-preflight.json"),JSON.stringify(body,null,2));
 assert(check.ok()&&body.can_confirm,JSON.stringify(body));
 await page.getByText("检查通过，可以确认入库。",{exact:true}).waitFor();
 const confirmed=page.waitForResponse(r=>r.url().endsWith("/confirm")&&r.request().method()==="POST");
 await page.getByRole("button",{name:"确认年度事实入库",exact:true}).click();
 const result=await confirmed;
 const confirmedBody=await result.json();
 await writeFile(join(log,"small-confirmed.json"),JSON.stringify(confirmedBody,null,2));
 assert(result.ok(),JSON.stringify(confirmedBody));
 state.confirmed=confirmedBody;
 await page.getByRole("button",{name:"解锁全部修改",exact:true}).waitFor();
};
