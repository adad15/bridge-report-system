import {
  ApartmentOutlined,
  BellOutlined,
  DashboardOutlined,
  DatabaseOutlined,
  FileWordOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  SafetyCertificateOutlined,
  SettingOutlined,
  TeamOutlined,
  UserOutlined,
} from "@ant-design/icons";
import { Avatar, Badge, Button, Tag, Tooltip } from "antd";
import { useState, type ReactNode } from "react";
import { NavLink, useLocation } from "react-router-dom";

import { useAuth } from "../auth/AuthContext";

interface AppLayoutProps {
  children: ReactNode;
  reviewWorkspace?: boolean;
}

const navigation = [
  { to: "/workbench", label: "工作台", icon: <DashboardOutlined /> },
  { to: "/bridges", label: "桥梁档案", icon: <DatabaseOutlined /> },
  { to: "/rating-trees", label: "评定树", icon: <ApartmentOutlined /> },
];

/** 系统管理里的报告相关维护页。只有管理员看得到（设计 §22）。 */
const systemNavigation = [
  { to: "/settings/report-templates", label: "报告模板", icon: <FileWordOutlined /> },
  { to: "/settings/report-directory", label: "报告人员与设备", icon: <TeamOutlined /> },
];

function BridgeBrandMark() {
  return (
    <span className="application-brand-mark" aria-hidden="true">
      <span className="application-brand-pylon" />
      <span className="application-brand-deck" />
      <span className="application-brand-cable application-brand-cable-left" />
      <span className="application-brand-cable application-brand-cable-right" />
    </span>
  );
}

function currentPageName(pathname: string): string {
  if (pathname.startsWith("/settings/report-templates")) return "报告模板";
  if (pathname.startsWith("/settings/report-directory")) return "报告人员与设备";
  if (pathname.startsWith("/rating-trees")) return "评定树";
  if (pathname.startsWith("/bridges/")) return "桥梁业务空间";
  if (pathname === "/bridges") return "桥梁档案";
  return "工作台";
}

function bridgeWorkspaceTabs(pathname: string) {
  const match = pathname.match(/^\/bridges\/([^/]+)/);
  if (!match) return null;
  const root = `/bridges/${match[1]}`;
  return [
    { to: root, label: "桥梁概览", end: true },
    { to: `${root}/inventory`, label: "构件台账" },
    { to: `${root}/inspections`, label: "年度检测" },
    { to: `${root}/components`, label: "构件病害档案" },
  ];
}

export function AppLayout({ children, reviewWorkspace = false }: AppLayoutProps) {
  const { user, logout } = useAuth();
  const location = useLocation();
  const isAdmin = user?.role === "admin";
  const workspaceTabs = bridgeWorkspaceTabs(location.pathname);
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);

  return (
    <div className={[
      "application-layout",
      location.pathname === "/workbench" ? "application-layout-workbench" : "",
      reviewWorkspace ? "application-layout-review" : "",
      sidebarCollapsed ? "is-sidebar-collapsed" : "",
    ].filter(Boolean).join(" ")}>
      <header className="application-header">
        <NavLink to="/workbench" className="application-brand" aria-label="桥梁检测报告系统工作台">
          <BridgeBrandMark />
          <span className="application-brand-copy">
            <strong>桥梁检测报告系统</strong>
            <small>BRIDGE INSPECTION</small>
          </span>
        </NavLink>

        {workspaceTabs ? (
          <nav className="application-bridge-tabs" aria-label="桥梁工作区">
            <span className="application-header-divider" aria-hidden="true" />
            {workspaceTabs.map((item) => (
              <NavLink key={item.to} end={item.end} to={item.to}>{item.label}</NavLink>
            ))}
          </nav>
        ) : location.pathname !== "/workbench" ? (
          <div className="application-header-context">
            <span className="application-header-divider" />
            <span>{currentPageName(location.pathname)}</span>
          </div>
        ) : null}

        <div className="application-user-area">
          <Badge count={0} showZero={false}>
            <Button type="text" shape="circle" icon={<BellOutlined />} aria-label="通知" />
          </Badge>
          <Avatar size={34} icon={<UserOutlined />} className="application-user-avatar" />
          <span className="application-user-name">{user?.display_name}</span>
          <Tag className="application-user-role" color={isAdmin ? "blue" : "default"}>
            {isAdmin ? "管理员" : "普通用户"}
          </Tag>
          <Button type="text" icon={<LogoutOutlined />} onClick={() => void logout()}>
            退出登录
          </Button>
        </div>
      </header>

      <aside className="application-sidebar" aria-label="主导航">
        <Tooltip title={sidebarCollapsed ? "展开侧边栏" : "收起侧边栏"} placement="right">
          <Button
            className="application-sidebar-toggle"
            type="text"
            shape="circle"
            icon={sidebarCollapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
            aria-label={sidebarCollapsed ? "展开侧边栏" : "收起侧边栏"}
            aria-expanded={!sidebarCollapsed}
            onClick={() => setSidebarCollapsed((current) => !current)}
          />
        </Tooltip>
        <nav className="application-navigation">
          <p className="application-navigation-label">业务中心</p>
          {navigation.map((item) => (
            <NavLink
              key={item.to}
              to={item.to}
              aria-label={item.label}
              className={({ isActive }) =>
                `application-navigation-item${isActive ? " is-active" : ""}`
              }
            >
              <span className="application-navigation-icon">{item.icon}</span>
              <span className="application-navigation-text">{item.label}</span>
            </NavLink>
          ))}

          {isAdmin ? (
            <>
              <p className="application-navigation-label application-navigation-label-secondary">系统管理</p>
              <NavLink
                to="/bridges?standards=1"
                aria-label="规范管理"
                className={() =>
                  `application-navigation-item${location.search === "?standards=1" ? " is-active" : ""}`
                }
              >
                <span className="application-navigation-icon"><SettingOutlined /></span>
                <span className="application-navigation-text">规范管理</span>
              </NavLink>
              {systemNavigation.map((item) => (
                <NavLink
                  key={item.to}
                  to={item.to}
                  aria-label={item.label}
                  className={({ isActive }) =>
                    `application-navigation-item${isActive ? " is-active" : ""}`
                  }
                >
                  <span className="application-navigation-icon">{item.icon}</span>
                  <span className="application-navigation-text">{item.label}</span>
                </NavLink>
              ))}
            </>
          ) : null}
        </nav>

        <div className="application-sidebar-footer">
          <SafetyCertificateOutlined />
          <span className="application-navigation-text">数据安全 · 本地部署</span>
        </div>
      </aside>

      <main
        className={[
          "application-main",
          reviewWorkspace ? "application-main-review" : "",
        ].filter(Boolean).join(" ")}
      >
        <div className={reviewWorkspace ? "application-content application-content-review" : "application-content"}>
          {children}
        </div>
      </main>
    </div>
  );
}
