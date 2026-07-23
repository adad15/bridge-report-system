// 页签切换会卸载路由组件，组件内 state 随之丢弃，再挂载时只能从头拉一遍——
// 台账有数千条构件，每次切回都要等几秒。这里把结果留在模块作用域：
// 再次挂载先用上次的数据立即渲染，同时后台重新拉取并覆盖，避免停留在过期数据上。
//
// 仅为同一次会话内的导航加速，不做持久化、不设过期：数据的权威来源始终是后端，
// 每次挂载都会重新校验一次。

const cache = new Map<string, unknown>();

export function readCached<T>(key: string): T | undefined {
  return cache.get(key) as T | undefined;
}

export function writeCached<T>(key: string, value: T): void {
  cache.set(key, value);
}

// 资源已不存在（如台账被删、桥梁被删）时必须丢弃，否则下次挂载会先闪出旧内容。
export function dropCached(key: string): void {
  cache.delete(key);
}

// 仅供测试：缓存是模块作用域的，用例之间会残留，导致执行顺序影响结果。
export function clearCachedForTests(): void {
  cache.clear();
}

export const inventoryCacheKey = (bridgeId: string) => `inventory:${bridgeId}`;
// 规范目录与桥无关，是全局参考数据，只需一个键。
export const standardCatalogsCacheKey = "standard-catalogs";
export const inspectionYearsCacheKey = (bridgeId: string) => `inspection-years:${bridgeId}`;
export const inspectionWorkspaceCacheKey = (inspectionYearId: string) =>
  `inspection-workspace:${inspectionYearId}`;
