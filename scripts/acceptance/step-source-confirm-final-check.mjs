import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";
export default async({page,api,state,origin,log})=>{
 assert.equal(origin,"http://127.0.0.1:25173");
 await page.getByRole("button",{name:"解锁全部修改",exact:true}).waitFor();
 const header=page.locator("header").filter({has:page.getByRole("heading",{name:"2026 年度检测 · 接口同步校对",exact:true})});
 await header.getByText("已确认",{exact:true}).first().waitFor();
 assert.equal(await header.getByText("待校对",{exact:true}).count(),0);
 assert.match(await header.innerText(),/已修改\s*6/);
 assert.equal(await page.getByRole("button",{name:"确认年度事实入库",exact:true}).count(),0);
 const review=await api("/api/import-records/"+state.sourceConfirmation.importId+"/review");
 assert.equal(review.import_record.import_status,"已确认");
 assert.equal(review.statistics.pending_count,0);
 assert.equal(review.statistics.confirmed_count+review.statistics.modified_count,381);
 await writeFile(join(log,"source-confirm-final-ui-check.json"),JSON.stringify({passed:true,header:await header.innerText(),statistics:review.statistics},null,2));
 await page.screenshot({path:join(log,"source-confirm-final.png"),fullPage:true});
};