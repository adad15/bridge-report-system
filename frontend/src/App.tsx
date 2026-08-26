import { BrowserRouter, Navigate, NavLink, Route, Routes, useLocation } from "react-router-dom";

import { AuthProvider, useAuth } from "./auth/AuthContext";
import { BridgeOverviewPage } from "./pages/BridgeOverviewPage";
import { BridgesPage } from "./pages/BridgesPage";
import { LegacyTriageRedirect } from "./pages/LegacyTriageRedirect";
import { ComponentArchivePage } from "./pages/ComponentArchivePage";
import { ComponentInventoryPage } from "./pages/ComponentInventoryPage";
import { ThreadTriagePage } from "./pages/ThreadTriagePage";
import { LoginPage } from "./pages/LoginPage";
import { ReviewWorkspacePage } from "./pages/ReviewWorkspacePage";
import { RatingTreePage } from "./pages/RatingTreePage";
import { BridgeWorkspaceShell } from "./workspace/BridgeWorkspaceShell";
import { InspectionWorkspacePage } from "./pages/InspectionWorkspacePage";
import "./styles.css";

export function App() {
  return (
    <AuthProvider>
      <BrowserRouter>
        <AppShell />
      </BrowserRouter>
    </AuthProvider>
  );
}

// 顶栏右侧的当前用户信息 + 退出登录。角色徽章帮助用户确认当前权限
// （管理员才有"解锁全部修改"等入口）。
function CurrentUserBadge() {
  const { user, logout } = useAuth();
  if (user === null) return null;
  return (
    <div className="top-nav-user">
      <span>{user.display_name}</span>
      <span className={user.role === "admin" ? "role-badge role-admin" : "role-badge role-normal"}>
        {user.role === "admin" ? "管理员" : "普通用户"}
      </span>
      <button type="button" className="top-nav-logout" onClick={() => void logout()}>
        退出登录
      </button>
    </div>
  );
}

// 校对工作台是应用式工作台页面（占满视口、页眉/侧栏/底栏固定），需要突破 .app-content
// 给普通页面用的 880px 居中卡片流。useLocation 只能在 Router 的子组件里调用，所以拆出
// 这一层，而不是在 App() 里直接判断。
//
// 顶栏放在 .app-shell 之外的整宽 .app-header 里：它以前是 .app-content 的子元素，
// 于是宽度跟着页面走（卡片流 1180px、评定树 1680px、工作台满宽），切页时导航栏会
// 整条横向瞬移两百多像素。常驻 chrome 不该跟着内容宽度跑。
function AppShell() {
  const { user, restoring } = useAuth();
  const location = useLocation();
  // 只有"导入记录校对工作台"使用全屏工作台壳；模块 06 的 /defect-threads/triage
  // 是普通卡片流页面，正则必须锚定 imports 段避免误匹配。
  const isReviewWorkspace = /\/imports\/[^/]+\/review$/.test(location.pathname);
  // 评定树是"树 + 节点详情"的双栏浏览页，和校对工作台一样吃得下整块屏幕：500 个节点
  // 的树要显示深层编号，右侧标度表有三列。但它保留卡片外观，所以只放宽 .app-content
  // 的 1180px 上限，不套全屏工作台壳。
  const isWidePage = location.pathname.startsWith("/rating-trees");
  const contentClass = isReviewWorkspace
    ? "app-content app-content-workbench"
    : isWidePage
      ? "app-content app-content-wide"
      : "app-content";

  // 启动恢复会话期间不渲染登录页，避免"闪一下登录页再进入系统"。
  if (restoring) {
    return (
      <div className="app-frame">
        <main className="app-shell">
          <div className="app-content">
            <section className="status-panel">
              <p>正在恢复登录会话…</p>
            </section>
          </div>
        </main>
      </div>
    );
  }

  if (user === null) {
    return (
      <div className="app-frame">
        <main className="app-shell">
          <div className="app-content">
            <LoginPage />
          </div>
        </main>
      </div>
    );
  }

  return (
    <div className="app-frame">
      <header className="app-header">
        <nav className="top-nav">
          <NavLink to="/bridges" className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}>
            桥梁档案
          </NavLink>
          <NavLink to="/rating-trees" className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}>
            评定树
          </NavLink>
          <CurrentUserBadge />
        </nav>
      </header>
      <main className={isReviewWorkspace ? "app-shell app-shell-workbench" : "app-shell"}>
        <div className={contentClass}>
          <Routes>
            <Route path="/" element={<Navigate replace to="/bridges" />} />
            <Route path="/bridges" element={<BridgesPage />} />
            <Route path="/rating-trees" element={<RatingTreePage />} />
            <Route path="/rating-trees/:versionId" element={<RatingTreePage />} />
            <Route path="/bridges/:bridgeId" element={<BridgeWorkspaceShell />}>
              <Route index element={<BridgeOverviewPage />} />
              <Route path="inventory" element={<ComponentInventoryPage />} />
              <Route path="inspections" element={<InspectionWorkspacePage />} />
              <Route path="inspections/:inspectionYearId" element={<InspectionWorkspacePage />} />
              <Route path="components" element={<ComponentArchivePage />} />
              <Route path="components/:componentId" element={<ComponentArchivePage />} />
              <Route path="defect-threads/triage" element={<ThreadTriagePage />} />
              {/* 旧整理页已下线：整理只剩工作台一套流程，旧地址重定向而非 404。 */}
              <Route path="defect-threads/review" element={<LegacyTriageRedirect />} />
            </Route>
            <Route
              path="/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review"
              element={<ReviewWorkspacePage />}
            />
          </Routes>
        </div>
      </main>
    </div>
  );
}
