import { request } from "./apiClient";

// 5.0：构件绑定、标记缺失、取消绑定、批量替换与区间展开已全部改走解析链路
// （resolutionApi.ts）。这里只剩年度评定树绑定——它绑的是年度用哪一版评定树，
// 与"某条病害挂到哪件构件"无关，不属于构件解析那套状态。
//
// 旧的绑定函数不是"没人调就留着"：它们会往草稿 JSON 里写 bridge_component_id、
// rating_tree_node_id 等 5.0 已删字段，调一次就把草稿写成非法契约。连同后端那九个
// 路由一起下线了。

/** 写操作的请求体 + 编辑锁令牌。绑评定树改的是年度锁定的台账版本，必须持锁。 */
const lockedJson = (method: string, body: unknown, lockToken: string): RequestInit => ({
  method,
  headers: { "Content-Type": "application/json", "X-Edit-Lock-Token": lockToken },
  body: JSON.stringify(body),
});

function bindingUrl(baseUrl: string, importId: string, suffix = ""): string {
  return `${baseUrl}/api/import-records/${encodeURIComponent(importId)}/component-binding${suffix}`;
}

/**
 * 响应里仍带一份旧形状的概览，但调用方已不再消费它——绑完评定树会重取解析工作区。
 * 这里只关心请求是否成功。
 */
async function overviewRequest(url: string, init?: RequestInit): Promise<unknown> {
  return request<unknown>(url, init);
}

export function bindInspectionRatingTree(
  baseUrl: string,
  importId: string,
  ratingTreeVersionId: string,
  expectedInventoryRevisionId: string,
  lockToken: string
) {
  return overviewRequest(
    bindingUrl(baseUrl, importId, "/rating-tree"),
    lockedJson("POST", {
      rating_tree_version_id: ratingTreeVersionId,
      expected_inventory_revision_id: expectedInventoryRevisionId,
    }, lockToken)
  );
}
