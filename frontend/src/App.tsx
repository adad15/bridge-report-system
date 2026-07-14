import { BrowserRouter, NavLink, Route, Routes, useLocation } from "react-router-dom";

import { BridgeDetailPage } from "./pages/BridgeDetailPage";
import { BridgesPage } from "./pages/BridgesPage";
import { ComponentArchivePage } from "./pages/ComponentArchivePage";
import { DefectThreadReviewPage } from "./pages/DefectThreadReviewPage";
import { HomePage } from "./pages/HomePage";
import { ReviewWorkspacePage } from "./pages/ReviewWorkspacePage";
import "./styles.css";

export function App() {
  return (
    <BrowserRouter>
      <AppShell />
    </BrowserRouter>
  );
}

// 校对工作台是应用式工作台页面（占满视口、页眉/侧栏/底栏固定），需要突破 .app-content
// 给普通页面用的 880px 居中卡片流。useLocation 只能在 Router 的子组件里调用，所以拆出
// 这一层，而不是在 App() 里直接判断。
function AppShell() {
  const location = useLocation();
  // 只有"导入记录校对工作台"使用全屏工作台壳；模块 06 的 /defect-threads/review
  // 是普通卡片流页面，正则必须锚定 imports 段避免误匹配。
  const isReviewWorkspace = /\/imports\/[^/]+\/review$/.test(location.pathname);

  return (
    <main className={isReviewWorkspace ? "app-shell app-shell-workbench" : "app-shell"}>
      <div className={isReviewWorkspace ? "app-content app-content-workbench" : "app-content"}>
        <nav className="top-nav">
          <NavLink to="/" end className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}>
            首页
          </NavLink>
          <NavLink to="/bridges" className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}>
            桥梁列表
          </NavLink>
        </nav>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/bridges" element={<BridgesPage />} />
          <Route path="/bridges/:bridgeId" element={<BridgeDetailPage />} />
          <Route path="/bridges/:bridgeId/components" element={<ComponentArchivePage />} />
          <Route path="/bridges/:bridgeId/components/:componentId" element={<ComponentArchivePage />} />
          <Route path="/bridges/:bridgeId/defect-threads/review" element={<DefectThreadReviewPage />} />
          <Route
            path="/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review"
            element={<ReviewWorkspacePage />}
          />
        </Routes>
      </div>
    </main>
  );
}
