import assert from "node:assert/strict";
export default async({page,api,state,origin})=>{
 await page.goto(origin+"/bridges/"+state.bridge.id+"/inventory");
 await page.getByRole("heading",{name:"实际构件台账",exact:true}).waitFor();
 const pending=page.getByRole("button",{name:"一键确认全部待确认映射",exact:true});
 if(await pending.isVisible()) await pending.click();
 const confirm=page.getByRole("button",{name:"确认本版台账",exact:true});
 if(await confirm.isVisible()){
  await confirm.click();
  await confirm.waitFor({state:"hidden"});
 }
 state.inventory=await api("/api/bridges/"+state.bridge.id+"/component-inventories/latest/summary");
 assert.equal(state.inventory.revision.status,"已确认");
 state.inventoryEntries=await api("/api/component-inventories/"+state.inventory.revision.id+"/entries?keyword=%E6%A2%81&limit=50");
 await page.locator('a[href="/bridges/'+state.bridge.id+'/inspections"]').click();
 await page.getByRole("button",{name:"新建年度检测",exact:true}).click();
 const dialog=page.getByRole("dialog");
 await dialog.getByLabel("检测年度",{exact:true}).fill("2026");
 const created=page.waitForResponse(r=>r.request().method()==="POST"&&r.url().endsWith("/inspection-years"));
 await dialog.getByRole("button",{name:"创建年度",exact:true}).click();
 const response=await created;
 state.year=(await response.json()).inspection_year;
 assert(response.ok());
 await dialog.waitFor({state:"hidden"});
 await page.getByRole("button",{name:"导入检测资料",exact:true}).first().waitFor();
};
