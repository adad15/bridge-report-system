import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 assert.equal(origin,"http://127.0.0.1:25173");
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 await page.getByText("病害与照片",{exact:true}).click();
 const search=page.getByRole("textbox",{name:"搜索病害",exact:true});
 await search.waitFor();
 await page.getByRole("checkbox",{name:/全选可确认项/}).waitFor();
 await page.getByText("正在加载评定树规则。",{exact:true}).first().waitFor({state:"hidden",timeout:60000});
 const all=page.getByRole("checkbox",{name:/全选可确认项/});
 if(await all.isEnabled()){
  await all.check();
  await page.getByRole("button",{name:/^批量确认/}).click();
  await page.getByRole("dialog",{name:"确认安全病害"}).getByRole("button",{name:"确认所选",exact:true}).click();
  const [saved]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/review-draft")&&r.request().method()==="PUT"),page.getByRole("button",{name:"保存草稿",exact:true}).click()]);
  assert(saved.ok(),await saved.text());
 }
 const path="/api/import-records/"+state.sourceConfirmation.importId;
 const review=await api(path+"/review"), workspace=await api(path+"/resolution-workspace");
 await writeFile(join(log,"source-confirm-after-batch-review.json"),JSON.stringify(review,null,2));
 await writeFile(join(log,"source-confirm-after-batch-workspace.json"),JSON.stringify(workspace,null,2));
 console.log("After batch:",review.statistics,workspace.progress);
 await page.getByRole("combobox",{name:"按状态筛选",exact:true}).click();
 await page.getByText(/^待处理（/).last().click();
 await page.screenshot({path:join(log,"source-confirm-review-pending.png"),fullPage:true});
};