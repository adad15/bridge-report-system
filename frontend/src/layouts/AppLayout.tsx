import {
  ApartmentOutlined,
  BellOutlined,
  DashboardOutlined,
  DatabaseOutlined,
  FileWordOutlined,
  LeftOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  SafetyCertificateOutlined,
  SettingOutlined,
  TeamOutlined,
  UserOutlined,
} from "@ant-design/icons";
import {
  Avatar,
  Badge,
  Button,
  Divider,
  Flex,
  Grid,
  Layout,
  Menu,
  Tag,
  Tooltip,
  Typography,
  theme,
  type MenuProps,
} from "antd";
import { useState, type ReactNode } from "react";
import { Link, NavLink, useLocation, useNavigate } from "react-router-dom";

import { useAuth } from "../auth/AuthContext";
import { BridgeMark, useMediaQuery, useShellMetrics } from "../design-system";
import { StatusTag } from "../workspace/StatusTag";
import { HeaderBridgeContext, type HeaderBridge } from "./HeaderBridgeContext";

interface AppLayoutProps {
  children: ReactNode;
  reviewWorkspace?: boolean;
}

/*
 * 外壳的结构尺寸都在 design-system/density.ts：它按视口分宽松 / 紧凑两档，
 * 页面里那些"吃满一屏"的高度公式减的也是同一份（pageOffset）。
 */

const navigation = [
  { to: "/workbench", label: "工作台", icon: <DashboardOutlined /> },
  { to: "/bridges", label: "桥梁档案", icon: <DatabaseOutlined /> },
  { to: "/rating-trees", label: "评定树", icon: <ApartmentOutlined /> },
];

const STANDARDS_KEY = "/bridges?standards=1";

/** 系统管理里的维护页。只有管理员看得到（设计 §22）。 */
const systemNavigation = [
  { to: STANDARDS_KEY, label: "规范管理", icon: <SettingOutlined /> },
  { to: "/settings/report-templates", label: "报告模板", icon: <FileWordOutlined /> },
  { to: "/settings/report-directory", label: "报告人员与设备", icon: <TeamOutlined /> },
];

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
    { to: `${root}/inventory`, label: "构件台账", end: false },
    { to: `${root}/inspections`, label: "年度检测", end: false },
    { to: `${root}/components`, label: "构件病害档案", end: false },
  ];
}

/** 侧栏里哪一项亮：规范管理挂在桥梁档案页上，靠查询串区分。 */
function selectedNavigationKey(pathname: string, search: string): string {
  if (pathname === "/bridges" && search === "?standards=1") return STANDARDS_KEY;
  const all = [...navigation, ...systemNavigation].filter((item) => item.to !== STANDARDS_KEY);
  return all.find((item) => pathname === item.to || pathname.startsWith(`${item.to}/`))?.to ?? "";
}

export function AppLayout({ children, reviewWorkspace = false }: AppLayoutProps) {
  const { user, logout } = useAuth();
  const location = useLocation();
  const navigate = useNavigate();
  const { token } = theme.useToken();
  // 宽松 / 紧凑两档的结构尺寸：顶栏高度、侧栏宽度、内容区留白。
  const shell = useShellMetrics();
  const screens = Grid.useBreakpoint();
  const isAdmin = user?.role === "admin";
  const isWorkbench = location.pathname === "/workbench";
  const workspaceTabs = bridgeWorkspaceTabs(location.pathname);
  const [collapsedByUser, setCollapsedByUser] = useState(false);
  // 桥梁工作区外壳报上来的当前桥梁；只在桥梁页签下显示。
  const [headerBridge, setHeaderBridge] = useState<HeaderBridge | null>(null);
  const bridge = workspaceTabs ? headerBridge : null;
  // 顶栏在桥梁工作区里最挤：屏幕不够宽时先藏编号与线路，再藏用户名。
  const showBridgeNumber = useMediaQuery("(min-width: 1366px)");
  const showUserName = bridge ? screens.xl !== false : screens.lg !== false;

  // 窄屏一律收起侧栏，也不给展开按钮；useBreakpoint 首次渲染前是空对象，按宽屏处理。
  const narrow = screens.lg === false;
  const collapsed = narrow || collapsedByUser;
  const siderWidth = collapsed ? shell.siderCollapsedWidth : shell.siderWidth;
  // 校对工作台是应用式页面：整屏不滚，只有它自己的面板滚。屏幕窄到放不下时退回整页滚动。
  const reviewScrollsWithPage = useMediaQuery("(max-width: 1100px)");
  const fixedHeight = reviewWorkspace && !reviewScrollsWithPage;

  const selectedKey = selectedNavigationKey(location.pathname, location.search);
  const menuItem = (item: { to: string; label: string; icon: ReactNode }) => ({
    key: item.to,
    icon: item.icon,
    label: <Link to={item.to}>{item.label}</Link>,
  });
  const menuItems: MenuProps["items"] = [
    {
      type: "group",
      key: "business",
      label: collapsed ? null : "业务中心",
      children: navigation.map(menuItem),
    },
    ...(isAdmin
      ? [
          ...(collapsed ? [{ type: "divider" as const, key: "system-divider" }] : []),
          {
            type: "group" as const,
            key: "system",
            label: collapsed ? null : "系统管理",
            children: systemNavigation.map(menuItem),
          },
        ]
      : []),
  ];

  const activeTab = workspaceTabs?.find((item) =>
    item.end ? location.pathname === item.to : location.pathname.startsWith(item.to)
  );

  return (
    <Layout style={fixedHeight ? { height: "100dvh", overflow: "hidden" } : { minHeight: "100dvh" }}>
      <Layout.Header
        style={{
          position: "sticky",
          top: 0,
          zIndex: 20,
          borderBottom: `1px solid ${token.colorBorderSecondary}`,
        }}
      >
        <Flex align="center" gap={20} style={{ height: "100%" }}>
          <NavLink to="/workbench" aria-label="桥梁检测报告系统工作台">
            <Flex align="center" gap={12} style={{ width: siderWidth - 24 }}>
              <BridgeMark color={token.colorPrimary} />
              {collapsed ? null : (
                <Flex vertical>
                  <Typography.Text strong style={{ fontSize: 17, lineHeight: 1.25, whiteSpace: "nowrap" }}>
                    桥梁检测报告系统
                  </Typography.Text>
                  {/* 品牌字标：固定文案的小号字距，属于品牌图形的一部分，不走正文字号。 */}
                  <Typography.Text
                    type="secondary"
                    strong
                    style={{ fontSize: 9, lineHeight: 1.25, letterSpacing: "0.18em" }}
                  >
                    BRIDGE INSPECTION
                  </Typography.Text>
                </Flex>
              )}
            </Flex>
          </NavLink>

          <Flex flex={1} align="center" gap={20} style={{ minWidth: 0, height: "100%" }}>
            {workspaceTabs ? (
              <>
                <Divider vertical />
                {bridge ? (
                  <>
                    <Flex align="center" gap={6} style={{ minWidth: 0, flex: "0 1 auto" }}>
                      <Tooltip title="返回桥梁档案">
                        <Button
                          type="text"
                          size="small"
                          icon={<LeftOutlined />}
                          aria-label="返回桥梁档案"
                          onClick={() => navigate("/bridges")}
                        />
                      </Tooltip>
                      <Typography.Title level={4} ellipsis style={{ margin: 0, maxWidth: 220 }}>
                        {bridge.name}
                      </Typography.Title>
                      <StatusTag status={bridge.status} />
                      {showBridgeNumber ? (
                        <Typography.Text type="secondary" style={{ whiteSpace: "nowrap" }}>
                          {bridge.systemNumber} · {bridge.routeName ?? "路线未填写"}
                        </Typography.Text>
                      ) : null}
                    </Flex>
                    <Divider vertical />
                  </>
                ) : null}
                <Menu
                  mode="horizontal"
                  aria-label="桥梁工作区"
                  selectedKeys={activeTab ? [activeTab.to] : []}
                  items={workspaceTabs.map((item) => ({
                    key: item.to,
                    label: <Link to={item.to}>{item.label}</Link>,
                  }))}
                  style={{ flex: 1, minWidth: 0 }}
                />
              </>
            ) : !isWorkbench && screens.lg !== false ? (
              <>
                <Divider vertical />
                <Typography.Text type="secondary" strong>
                  {currentPageName(location.pathname)}
                </Typography.Text>
              </>
            ) : null}
          </Flex>

          <Flex align="center" gap={8} style={{ minWidth: 0 }}>
            <Badge count={0} showZero={false}>
              <Button type="text" shape="circle" icon={<BellOutlined />} aria-label="通知" />
            </Badge>
            {screens.sm !== false ? (
              <Avatar
                size={34}
                icon={<UserOutlined />}
                style={{ color: token.colorPrimary, backgroundColor: token.colorPrimaryBg }}
              />
            ) : null}
            {showUserName ? (
              <>
                <Typography.Text ellipsis style={{ maxWidth: 120 }}>{user?.display_name}</Typography.Text>
                <Tag color={isAdmin ? "blue" : "default"}>{isAdmin ? "管理员" : "普通用户"}</Tag>
              </>
            ) : null}
            <Button type="text" icon={<LogoutOutlined />} onClick={() => void logout()}>
              {screens.sm !== false ? "退出登录" : null}
            </Button>
          </Flex>
        </Flex>
      </Layout.Header>

      <Layout hasSider>
        <Layout.Sider
          theme="light"
          width={shell.siderWidth}
          collapsedWidth={shell.siderCollapsedWidth}
          collapsed={collapsed}
          trigger={null}
          aria-label="主导航"
          style={{
            position: "sticky",
            top: shell.headerHeight,
            height: `calc(100dvh - ${shell.headerHeight}px)`,
            borderRight: `1px solid ${token.colorBorderSecondary}`,
          }}
        >
          {narrow ? null : (
            <Tooltip title={collapsed ? "展开侧边栏" : "收起侧边栏"} placement="right">
              <Button
                shape="circle"
                icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
                aria-label={collapsed ? "展开侧边栏" : "收起侧边栏"}
                aria-expanded={!collapsed}
                onClick={() => setCollapsedByUser((current) => !current)}
                style={{ position: "absolute", top: 14, right: -16, zIndex: 1, boxShadow: token.boxShadowSecondary }}
              />
            </Tooltip>
          )}
          <Flex vertical style={{ height: "100%" }}>
            <Menu
              mode="inline"
              selectedKeys={selectedKey ? [selectedKey] : []}
              items={menuItems}
              style={{ flex: 1, minHeight: 0, overflowY: "auto", overflowX: "hidden", paddingTop: 16 }}
            />
            {isWorkbench ? null : (
              <Flex
                align="center"
                justify={collapsed ? "center" : "start"}
                gap={10}
                style={{ padding: collapsed ? "18px 0" : "18px 26px", borderTop: `1px solid ${token.colorSplit}` }}
              >
                <Typography.Text type="secondary"><SafetyCertificateOutlined /></Typography.Text>
                {collapsed ? null : <Typography.Text type="secondary">数据安全 · 本地部署</Typography.Text>}
              </Flex>
            )}
          </Flex>
        </Layout.Sider>

        <Layout.Content
          style={
            reviewWorkspace
              ? { minWidth: 0, minHeight: 0, overflow: fixedHeight ? "hidden" : "auto" }
              : {
                minWidth: 0,
                padding: screens.sm === false
                  ? "22px 16px 32px"
                  : `${shell.contentPaddingBlock[0]}px ${shell.contentPaddingInline} ${shell.contentPaddingBlock[1]}px`,
              }
          }
        >
          <div style={reviewWorkspace ? { height: "100%" } : { width: "min(1480px, 100%)", minHeight: "100%", margin: "0 auto" }}>
            <HeaderBridgeContext.Provider value={setHeaderBridge}>
              {children}
            </HeaderBridgeContext.Provider>
          </div>
        </Layout.Content>
      </Layout>
    </Layout>
  );
}
