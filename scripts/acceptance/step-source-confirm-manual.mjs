import assert from "node:assert/strict";
import {readFile,writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 assert.equal(origin,"http://127.0.0.1:25173");
 await page.getByText("你正在编辑此导入记录。",{exact:true}).waitFor();
 await page.getByText("病害与照片",{exact:true}).click();
 const search=page.getByRole("textbox",{name:"搜索病害",exact:true});
 await search.waitFor();
 const path="/api/import-records/"+state.sourceConfirmation.importId;
 const before=await api(path+"/review");
 const choices={"source_defect_0123":"河床其它病害","source_defect_0159":"照明、标志其它病害","source_defect_0281":"锥坡、护坡其它病害","source_defect_0321":"锥坡、护坡其它病害","source_defect_0317":"5.1.1-14 上部其它病害","source_defect_0328":"5.1.1-14 上部其它病害"};
 for(const defect of before.parsed_result.defects.filter(d=>d.group_review_status==="待确认")){
  const close=page.getByRole("button",{name:"关闭精细维护",exact:true});
  if(await close.isVisible())await close.click();
  await search.fill(defect.component_number);
  const row=page.getByRole("row").filter({hasText:defect.component_number}).filter({hasText:defect.defect_type}).first();
  await row.getByRole("button",{name:/^编\s*辑$/}).click();
  const editor=page.getByRole("group",{name:"病害详情维护",exact:true});
  await editor.waitFor();
  if(choices[defect.candidate_id]){
   await editor.getByRole("combobox",{name:"评定树病害",exact:true}).click();
   await page.locator(".ant-select-dropdown:visible .ant-select-dropdown-list-holder").evaluate(el=>{el.scrollTop=el.scrollHeight;el.dispatchEvent(new Event("scroll"));});
   const option=page.locator(".ant-select-item-option").filter({hasText:choices[defect.candidate_id]}).filter({hasText:"暂不计分"}).first();
   const [response]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/rating-resolution")&&r.request().method()==="PUT"),option.click()]);
   assert(response.ok(),await response.text());
  }
  await editor.getByRole("button",{name:/^确\s*认$/}).click();
  const [saved]=await Promise.all([page.waitForResponse(r=>r.url().endsWith("/review-draft")&&r.request().method()==="PUT"),page.getByRole("button",{name:"保存草稿",exact:true}).click()]);
  assert(saved.ok(),await saved.text());
  console.log("Reviewed:",defect.candidate_id,defect.component_number,choices[defect.candidate_id]??"保留原始量值并复核提示");
 }
 const review=await api(path+"/review"),workspace=await api(path+"/resolution-workspace");
 await writeFile(join(log,"source-confirm-reviewed.json"),JSON.stringify(review,null,2));
 await writeFile(join(log,"source-confirm-reviewed-workspace.json"),JSON.stringify(workspace,null,2));
 assert.equal(review.statistics.pending_count,0);
 assert.equal(review.statistics.confirmed_count+review.statistics.modified_count,381);
 const [preflight]=await Promise.all([page.waitForResponse(r=>r.url().includes("preflight")&&r.request().method()==="POST"),page.getByRole("button",{name:"入库前检查",exact:true}).click()]);
 const check=await preflight.json();
 await writeFile(join(log,"source-confirm-reviewed-preflight.json"),JSON.stringify(check,null,2));
 console.log("Preflight:",{can_confirm:check.can_confirm,blocking_errors:check.blocking_errors.length,warnings:check.warnings.length});
};
