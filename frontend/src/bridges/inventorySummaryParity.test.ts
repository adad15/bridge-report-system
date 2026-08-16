import { readFileSync } from "node:fs";

import { describe, expect, it } from "vitest";

import { inventoryGroupSummaries } from "./ComponentInventoryEditor";
import type { ComponentInventoryRevision } from "../api/componentInventoryApi";

/**
 * 搬迁验收：拿真实数据比对"旧的客户端汇总"与"新的服务端汇总 SQL"。
 *
 * 默认跳过——它依赖 scripts/dev/compare-inventory-summary.ps1 从真库导出的两份
 * JSON，不是 CI 用例。由该脚本设置 INVENTORY_PARITY_FIXTURE 后运行。
 *
 * 这条比对是**必要不充分**的：现网数据每构件只有 1 个生效映射、没有停用构件、
 * sort_order 不重复，多映射计数放大、编号范围含停用构件、分组顺序平局这几类问题
 * 在这份数据上根本不出现。它们由 test_component_inventory_repository.cpp 里的
 * 构造数据单测覆盖。这里证明的是"常规路径没搬错"。
 */
const fixturePath = process.env.INVENTORY_PARITY_FIXTURE;

interface SqlGroup {
  site_component_type: string;
  structure_part: string;
  active_count: number;
  first_number: string | null;
  last_number: string | null;
  confirmed_count: number;
  pending_count: number;
  unmapped_count: number;
}

describe.skipIf(!fixturePath)("构件台账汇总搬迁比对", () => {
  it("旧客户端汇总与新服务端 SQL 逐字段一致", () => {
    const fixture = JSON.parse(readFileSync(fixturePath!, "utf8")) as {
      revision: ComponentInventoryRevision;
      summary: { groups: SqlGroup[] };
    };

    // 目录传空数组：mappingLabel 在旧实现里是由 catalog 解析出的中文标签，
    // 新接口返回的是 package/category 两个 id，两者不是一一对应，无法直接比。
    // 那两个字段改由构造数据单测覆盖，这里只比其余八个能一一对应的字段。
    const legacy = inventoryGroupSummaries(fixture.revision, []);
    const fresh = fixture.summary.groups;

    expect(fresh.map((group) => group.site_component_type)).toEqual(
      legacy.map((group) => group.siteComponentType),
    );

    for (const [index, group] of legacy.entries()) {
      const actual = fresh[index];
      const where = `第 ${index + 1} 组 ${group.siteComponentType}`;
      expect(actual.structure_part, `${where} structure_part`).toBe(group.structurePart);
      expect(actual.active_count, `${where} active_count`).toBe(group.activeCount);
      expect(actual.first_number ?? "", `${where} first_number`).toBe(group.firstNumber);
      expect(actual.last_number ?? "", `${where} last_number`).toBe(group.lastNumber);
      expect(actual.confirmed_count, `${where} confirmed_count`).toBe(group.confirmedCount);
      expect(actual.pending_count, `${where} pending_count`).toBe(group.pendingCount);
      expect(actual.unmapped_count, `${where} unmapped_count`).toBe(group.unmappedCount);
    }
  });
});
