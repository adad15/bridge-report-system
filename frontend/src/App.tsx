import { BrowserRouter, Navigate, Route, Routes, useLocation } from "react-router-dom";

import { AuthProvider, useAuth } from "./auth/AuthContext";
import { BridgeOverviewPage } from "./pages/BridgeOverviewPage";
import { BridgesPage } from "./pages/BridgesPage";
import { LegacyTriageRedirect } from "./pages/LegacyTriageRedirect";
import { ComponentArchivePage } from "./pages/ComponentArchivePage";
import { ComponentInventoryPage } from "./pages/ComponentInventoryPage";
import { ThreadTriagePage } from "./pages/ThreadTriagePage";
import { LoginPage, LoginRestoringPage } from "./pages/LoginPage";
import { ReviewWorkspacePage } from "./pages/ReviewWorkspacePage";
import { RatingTreePage } from "./pages/RatingTreePage";
import { WorkbenchPage } from "./pages/WorkbenchPage";
import { BridgeWorkspaceShell } from "./workspace/BridgeWorkspaceShell";
import { InspectionWorkspacePage } from "./pages/InspectionWorkspacePage";
import { AppLayout } from "./layouts/AppLayout";
import "./styles.css";
import "./layouts/AppLayout.css";
import "./pages/RatingTreePage.css";
import "./pages/BridgeWorkspacePages.css";
import "./review/components/DefectReviewApprovedLayout.css";

export function App() {
  return (
    <AuthProvider>
      <BrowserRouter>
        <AppShell />
      </BrowserRouter>
    </AuthProvider>
  );
}

function AppShell() {
  const { user, restoring } = useAuth();
  const location = useLocation();
  // 只有"导入记录校对工作台"使用全屏工作台壳；模块 06 的 /defect-threads/triage
  // 是普通卡片流页面，正则必须锚定 imports 段避免误匹配。
  const isReviewWorkspace = /\/imports\/[^/]+\/review$/.test(location.pathname);
  // 启动恢复会话期间不渲染登录页，避免"闪一下登录页再进入系统"。
  if (restoring) {
    return <LoginRestoringPage />;
  }

  if (user === null) {
    return <LoginPage />;
  }

  return (
    <AppLayout reviewWorkspace={isReviewWorkspace}>
      <Routes>
        <Route path="/" element={<Navigate replace to="/workbench" />} />
        <Route path="/workbench" element={<WorkbenchPage />} />
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
    </AppLayout>
  );
}
