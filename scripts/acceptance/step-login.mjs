import assert from "node:assert/strict";
export default async({page,api,state})=>{
 if(await page.getByRole("heading",{name:"欢迎登录"}).isVisible()){
  await page.getByLabel("用户名",{exact:true}).fill("admin");
  await page.getByLabel("密码",{exact:true}).fill("admin123");
  await page.getByRole("button",{name:/登\s*录/,exact:true}).click();
 }
 await page.getByText("在册桥梁",{exact:true}).waitFor();
 const bridges=await api("/api/bridges");
 assert.equal(bridges.bridges.length,0,"New acceptance database must start empty");
 state.standards=await api("/api/standards");
 state.trees=await api("/api/rating-trees");
 await page.locator('a[href="/bridges"]').first().click();
 await page.getByRole("button",{name:"添加桥梁",exact:true}).click();
 await page.getByRole("dialog").waitFor();
};
