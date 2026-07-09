import { BrowserRouter, NavLink, Route, Routes } from "react-router-dom";

import { BridgeDetailPage } from "./pages/BridgeDetailPage";
import { BridgesPage } from "./pages/BridgesPage";
import { HomePage } from "./pages/HomePage";
import { ReviewWorkspacePlaceholder } from "./pages/ReviewWorkspacePlaceholder";
import "./styles.css";

export function App() {
  return (
    <BrowserRouter>
      <main className="app-shell">
        <div className="app-content">
          <nav className="top-nav">
            <NavLink to="/" end className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}>
              首页
            </NavLink>
            <NavLink
              to="/bridges"
              className={({ isActive }) => (isActive ? "top-nav-link active" : "top-nav-link")}
            >
              桥梁列表
            </NavLink>
          </nav>
          <Routes>
            <Route path="/" element={<HomePage />} />
            <Route path="/bridges" element={<BridgesPage />} />
            <Route path="/bridges/:bridgeId" element={<BridgeDetailPage />} />
            <Route
              path="/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review"
              element={<ReviewWorkspacePlaceholder />}
            />
          </Routes>
        </div>
      </main>
    </BrowserRouter>
  );
}
