import { BrowserRouter, Navigate, NavLink, Route, Routes, useLocation } from "react-router-dom";

import { AuthProvider, useAuth } from "./auth/AuthContext";
import { BridgeOverviewPage } from "./pages/BridgeOverviewPage";
import { BridgesPage } from "./pages/BridgesPage";
import { ComponentArchivePage } from "./pages/ComponentArchivePage";
import { ComponentInventoryPage } from "./pages/ComponentInventoryPage";
import { DefectThreadReviewPage } from "./pages/DefectThreadReviewPage";
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
function AppShell() {
  const { user, restoring } = useAuth();
  const location = useLocation();
  // 只有"导入记录校对工作台"使用全屏工作台壳；模块 06 的 /defect-threads/review
  // 是普通卡片流页面，正则必须锚定 imports 段避免误匹配。
  const isReviewWorkspace = /\/imports\/[^/]+\/review$/.test(location.pathname);

  // 启动恢复会话期间不渲染登录页，避免"闪一下登录页再进入系统"。
  if (restoring) {
    return (
      <main className="app-shell">
        <div className="app-content">
          <section className="status-panel">
            <p>正在恢复登录会话…</p>
          </section>
        </div>
      </main>
    );
  }

  if (user === null) {
    return (
      <main className="app-shell">
        <div className="app-content">
          <LoginPage />
        </div>
      </main>
    );
  }

  return (
    <main className={isReviewWorkspace ? "app-shell app-shell-workbench" : "app-shell"}>
      <div className={isReviewWorkspace ? "app-content app-content-workbench" : "app-content"}>
        <nav className="top-nav">
          <NavLink to="/bridges" className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}>
            桥梁档案
          </NavLink>
          <NavLink to="/rating-trees" className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}>
            评定树
          </NavLink>
          <CurrentUserBadge />
        </nav>
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
            <Route path="defect-threads/review" element={<DefectThreadReviewPage />} />
          </Route>
          <Route
            path="/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review"
            element={<ReviewWorkspacePage />}
          />
        </Routes>
      </div>
    </main>
  );
}
