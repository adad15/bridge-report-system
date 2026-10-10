import { BrowserRouter, Navigate, Route, Routes, useLocation } from "react-router-dom";

import { AuthProvider, useAuth } from "./auth/AuthContext";
import { LegacyTriageRedirect } from "./pages/LegacyTriageRedirect";
import { LoginPage, LoginRestoringPage } from "./pages/LoginPage";
import { BridgeWorkspaceShell } from "./workspace/BridgeWorkspaceShell";
import { AppLayout } from "./layouts/AppLayout";
import "./styles.css";

import { createLazyFeature } from "./loading/createLazyFeature";

const BridgeOverviewPage = createLazyFeature(async () => ({ default: (await import("./pages/BridgeOverviewPage")).BridgeOverviewPage }), { label: "桥梁概览" });
const BridgesPage = createLazyFeature(async () => ({ default: (await import("./pages/BridgesPage")).BridgesPage }), { label: "桥梁档案" });
const ComponentArchivePage = createLazyFeature(async () => ({ default: (await import("./pages/ComponentArchivePage")).ComponentArchivePage }), { label: "构件病害档案" });
const ComponentInventoryPage = createLazyFeature(async () => ({ default: (await import("./pages/ComponentInventoryPage")).ComponentInventoryPage }), { label: "构件台账" });
const ThreadTriagePage = createLazyFeature(async () => ({ default: (await import("./pages/ThreadTriagePage")).ThreadTriagePage }), { label: "病害线索整理" });
const ReportDirectoryPage = createLazyFeature(async () => ({ default: (await import("./pages/ReportDirectoryPage")).ReportDirectoryPage }), { label: "报告人员与设备" });
const ReportGenerationPage = createLazyFeature(async () => ({ default: (await import("./pages/ReportGenerationPage")).ReportGenerationPage }), { label: "报告生成" });
const ReportTemplatesPage = createLazyFeature(async () => ({ default: (await import("./pages/ReportTemplatesPage")).ReportTemplatesPage }), { label: "报告模板" });
const ReviewWorkspacePage = createLazyFeature(async () => ({ default: (await import("./pages/ReviewWorkspacePage")).ReviewWorkspacePage }), { label: "校对工作台" });
const RatingTreePage = createLazyFeature(async () => ({ default: (await import("./pages/RatingTreePage")).RatingTreePage }), { label: "评定树" });
const StandardDocumentsPage = createLazyFeature(async () => ({ default: (await import("./pages/StandardDocumentsPage")).StandardDocumentsPage }), { label: "规范管理" });
const WorkbenchPage = createLazyFeature(async () => ({ default: (await import("./pages/WorkbenchPage")).WorkbenchPage }), { label: "工作台" });
const InspectionWorkspacePage = createLazyFeature(async () => ({ default: (await import("./pages/InspectionWorkspacePage")).InspectionWorkspacePage }), { label: "年度检测" });

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
        <Route path="/standards" element={<StandardDocumentsPage />} />
        <Route path="/settings/report-templates" element={<ReportTemplatesPage />} />
        <Route path="/settings/report-directory" element={<ReportDirectoryPage />} />
        <Route path="/rating-trees/:versionId" element={<RatingTreePage />} />
        <Route path="/bridges/:bridgeId" element={<BridgeWorkspaceShell />}>
          <Route index element={<BridgeOverviewPage />} />
          <Route path="inventory" element={<ComponentInventoryPage />} />
          <Route path="inspections" element={<InspectionWorkspacePage />} />
          <Route path="inspections/:inspectionYearId" element={<InspectionWorkspacePage />} />
          <Route
            path="inspections/:inspectionYearId/report"
            element={<ReportGenerationPage />}
          />
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
