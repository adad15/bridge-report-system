import assert from "node:assert/strict";
import {writeFile} from "node:fs/promises";
import {join} from "node:path";

export default async({page,api,state,origin,log})=>{
  const year=state.sourceDatabase.years["2026"].year.id;
  const bridge=state.sourceDatabase.bridge.id;
  const entry=state.sourceReport??={};
  assert.equal(state.sourceConfirmation.confirmation.inspection_year_id,year);
  await page.goto(origin+"/bridges/"+bridge+"/inspections/"+year+"/report");
  await page.getByText("报告配置",{exact:true}).waitFor();
  const before=await api("/api/inspection-years/"+year+"/report-preflight");
  assert.equal(before.summary.defect_observation_count,430);
  assert.equal(before.summary.source_defect_count,381);
  assert.equal(before.summary.photo_count,455);
  assert.equal(before.summary.overall_grade,"2类");
  await writeFile(join(log,"source-report-preflight-before.json"),JSON.stringify(before,null,2));

  if(!entry.job){
    const settings=await api("/api/inspection-years/"+year+"/report-settings");
    if(!settings.template_id){
      await page.getByRole("combobox").nth(0).click();
      await page.getByText("集成验收模板（默认）",{exact:true}).last().click();
      const roles=["批准","审核","检测负责人","编制"];
      for(let index=0;index<4;index++){
        await page.getByRole("button",{name:"添加一行",exact:true}).nth(0).click();
        await page.getByRole("combobox").nth(2+index*2).click();
        await page.getByText(state.personnel[index].full_name+"（测试机构）",{exact:true}).last().click();
        if(index){
          await page.getByRole("combobox").nth(3+index*2).click();
          await page.getByText(roles[index],{exact:true}).last().click();
        }
      }
      await page.getByRole("button",{name:"添加一行",exact:true}).nth(1).click();
      await page.getByRole("combobox").nth(10).click();
      await page.getByText("验收裂缝尺（TEST-01）",{exact:true}).last().click();
      await page.getByPlaceholder("用途，如裂缝宽度").fill("病害尺寸测量");
      const [response]=await Promise.all([
        page.waitForResponse(r=>r.url().endsWith("/report-settings")&&r.request().method()==="PUT"),
        page.getByRole("button",{name:/保存配置/}).click()
      ]);
      assert(response.ok(),await response.text());
    }
    const configured=await api("/api/inspection-years/"+year+"/report-settings");
    assert.equal(configured.template_id,state.template.id);
    assert.equal(configured.personnel.length,4);
    assert.equal(configured.equipment.length,1);
    await writeFile(join(log,"source-report-settings.json"),JSON.stringify(configured,null,2));
    const preflight=await api("/api/inspection-years/"+year+"/report-preflight");
    await writeFile(join(log,"source-report-preflight-ready.json"),JSON.stringify(preflight,null,2));
    assert.equal(preflight.can_generate,true,JSON.stringify(preflight));
    await page.screenshot({path:join(log,"source-report-ready-to-generate.png"),fullPage:true});
    const responsePromise=page.waitForResponse(r=>r.url().endsWith("/report-jobs")&&r.request().method()==="POST");
    await page.getByRole("button",{name:/生成报告/}).click();
    const response=await responsePromise;
    const job=await response.json();
    assert(response.ok(),JSON.stringify(job));
    assert.equal(job.inspection_year_id,year);
    entry.job=job;
    entry.startedAt=new Date().toISOString();
    await writeFile(join(log,"source-report-job-started.json"),JSON.stringify(job,null,2));
  }
  console.log(JSON.stringify({year,configured:true,preflight:true,job:entry.job},null,2));
};

