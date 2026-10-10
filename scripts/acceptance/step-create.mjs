export default async({page,api,state,origin})=>{
 if(state.bridge){state.inventory=await api("/api/bridges/"+state.bridge.id+"/component-inventories/latest/summary");await page.goto(origin+"/bridges/"+state.bridge.id+"/inventory");return;}
 const packages=(await api("/api/standards")).packages.filter(p=>p.family==="technical_condition"&&p.is_enabled);
 state.catalogs=await Promise.all(packages.map(p=>api("/api/standards/"+p.id+"/catalog")));

 await page.getByRole("button",{name:"添加桥梁",exact:true}).click();
 const dialog=page.getByRole("dialog");
 await dialog.getByLabel("桥梁名称",{exact:true}).fill("验收小样桥");
 await dialog.getByLabel("路线编号",{exact:true}).fill("G001");
 await dialog.getByLabel("路线名称",{exact:true}).fill("验收路线");
 await dialog.getByLabel("跨数",{exact:true}).fill("2");
 await dialog.getByLabel("桥型",{exact:true}).click();
 await page.getByText("梁式桥",{exact:true}).click();
 await dialog.getByRole("button",{name:"下一步：构件台账",exact:true}).click();
 const checkboxes=dialog.getByRole("checkbox",{name:/^启用 /});
 await checkboxes.first().waitFor();
 for(const box of await checkboxes.all()) await box.check();
 for(const input of await dialog.getByRole("spinbutton").all()) if(await input.isEnabled()) await input.fill("1");
 console.log("Inventory plan:",await dialog.innerText());
 const created=page.waitForResponse(r=>r.request().method()==="POST"&&new URL(r.url()).pathname==="/api/bridges");
 await dialog.getByRole("button",{name:/创建桥梁/}).click();
 const response=await created;
 const body=await response.json();
 if(!response.ok())throw new Error(JSON.stringify(body));
 state.bridge=body.bridge;
 await dialog.waitFor({state:"hidden"});
 await page.waitForLoadState("networkidle");
 state.inventory=await api("/api/bridges/"+state.bridge.id+"/component-inventories/latest/summary");

};
