/**
 * 提前打开 React Router v7 的两项行为：消除升级预告警告，也让测试和正式环境走同一套行为。
 * 项目里没有通配（splat）路由，v7_relativeSplatPath 不改变现有相对路径的解析。
 */
export const ROUTER_FUTURE = { v7_startTransition: true, v7_relativeSplatPath: true } as const;
