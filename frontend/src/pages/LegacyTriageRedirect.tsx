import { Navigate, useParams } from "react-router-dom";

/**
 * 旧整理页地址 `/defect-threads/review` 的重定向。
 *
 * 旧页面已下线（整理只剩工作台一套流程），但书签和浏览器历史里还留着旧地址，直接删掉
 * 路由会让它们 404。
 *
 * 这里刻意拼绝对路径而不是写 `<Navigate to="../defect-threads/triage">`：相对路径按
 * **路由层级**上溯而不是按 URL 段，读代码的人得先想清楚这条路由的 path 占了几段才敢下
 * 结论。改成绝对路径后对错一眼看得出，也能脱离鉴权壳单独测。
 */
export function LegacyTriageRedirect() {
  const { bridgeId } = useParams<{ bridgeId: string }>();
  return <Navigate to={`/bridges/${bridgeId}/defect-threads/triage`} replace />;
}
