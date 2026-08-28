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

/**
 * 为年度绑定（或切换）评定树版本。
 *
 * 响应只表示成败：绑完会重取解析工作区，那才是当前状态的唯一来源。
 * 此前这里会回一份完整概览，两处各自表达"现在是什么样"，迟早会不一致。
 */
export function bindInspectionRatingTree(
  baseUrl: string,
  importId: string,
  ratingTreeVersionId: string,
  expectedInventoryRevisionId: string,
  lockToken: string
): Promise<unknown> {
  return request<unknown>(
    `${baseUrl}/api/import-records/${encodeURIComponent(importId)}/rating-tree-binding`,
    lockedJson("POST", {
      rating_tree_version_id: ratingTreeVersionId,
      expected_inventory_revision_id: expectedInventoryRevisionId,
    }, lockToken)
  );
}
