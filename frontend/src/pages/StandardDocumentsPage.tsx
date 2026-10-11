import { FullscreenOutlined, HistoryOutlined, InfoCircleOutlined, ReadOutlined } from "@ant-design/icons";
import { Alert, Button, Card, Col, Empty, Flex, Grid, Row, Skeleton, Tabs, Typography, theme } from "antd";
import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import { useSearchParams } from "react-router-dom";

import {
  type StandardDocument,
  type StandardVersion,
  deleteStandardVersion,
  fetchStandardDocuments,
  standardDocumentsError,
} from "../api/standardDocumentsApi";
import { useAuth } from "../auth/AuthContext";
import { backendBaseUrl } from "../config";
import { useShellMetrics } from "../design-system";
import { DeleteStandardDocumentModal } from "../standard-documents/DeleteStandardDocumentModal";
import { StandardBasicInfo } from "../standard-documents/StandardBasicInfo";
import { StandardDocumentFormModal } from "../standard-documents/StandardDocumentFormModal";
import { StandardDocumentHeader } from "../standard-documents/StandardDocumentHeader";
import { StandardDocumentList } from "../standard-documents/StandardDocumentList";
import { StandardFullscreenBar } from "../standard-documents/StandardFullscreenBar";
import { StandardPdfReader } from "../standard-documents/StandardPdfReader";
import { StandardVersionFormModal, type StandardVersionFormMode } from "../standard-documents/StandardVersionFormModal";
import { StandardVersionTable } from "../standard-documents/StandardVersionTable";
import { useFullscreen } from "../standard-documents/useFullscreen";

const TABS = ["read", "versions", "info"] as const;
type Tab = (typeof TABS)[number];

type ModalState =
  | { type: "create-document" }
  | { type: "edit-document"; document: StandardDocument }
  | { type: "delete-document"; document: StandardDocument }
  | StandardVersionFormMode
  | null;

function sortDocuments(documents: StandardDocument[]): StandardDocument[] {
  return [...documents].sort((left, right) =>
    left.category.localeCompare(right.category, "zh-CN") || left.name.localeCompare(right.name, "zh-CN"));
}

/**
 * 规范管理：查阅规范原文，同一规范的各个版本并列归档
 * （设计 docs/superpowers/specs/2026-10-09-standard-documents-library-design.md §5）。
 *
 * 选中的规范、版本、页签、页码都记在查询串里（§5.8），刷新不丢，评定树以后也按这个格式链过来。
 *
 * 这一页以读原文为主：不设页头（顶栏已经写着页名），头部压成两行，把高度尽量留给阅读区；
 * 细读时可以「全屏阅读」，阅读器铺满整个屏幕（§5.4）。
 */
export function StandardDocumentsPage() {
  const { user } = useAuth();
  const isAdmin = user?.role === "admin";
  const stacked = Grid.useBreakpoint().lg === false;
  const { pageOffset } = useShellMetrics();
  const { token } = theme.useToken();
  const reading = useFullscreen<HTMLDivElement>();
  const [documents, setDocuments] = useState<StandardDocument[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [actionError, setActionError] = useState<string | null>(null);
  const [modal, setModal] = useState<ModalState>(null);
  const [searchParams, setSearchParams] = useSearchParams();

  const load = useCallback(() => {
    setError(null);
    fetchStandardDocuments(backendBaseUrl)
      .then((items) => setDocuments(sortDocuments(items)))
      .catch((caught) => setError(standardDocumentsError(caught)));
  }, []);
  useEffect(load, [load]);

  const docParam = searchParams.get("doc");
  const versionParam = searchParams.get("version");
  const tabParam = searchParams.get("tab");
  const pageParam = searchParams.get("page");
  const document = documents?.find((item) => item.id === docParam) ?? documents?.[0] ?? null;
  const version = document?.versions.find((item) => item.id === versionParam)
    ?? document?.versions.find((item) => item.id === document.default_version_id)
    ?? null;
  const tab: Tab = TABS.includes(tabParam as Tab) ? (tabParam as Tab) : "read";
  const page = pageParam && /^[1-9]\d*$/.test(pageParam) ? Number(pageParam) : undefined;

  // 地址栏里指向不存在的规范或版本：按缺省显示，并把无效参数清掉（§5.8）。
  useEffect(() => {
    if (documents === null) return;
    const next = new URLSearchParams(searchParams);
    if (docParam && !documents.some((item) => item.id === docParam)) {
      next.delete("doc");
      next.delete("version");
      next.delete("page");
    } else if (versionParam && document && !document.versions.some((item) => item.id === versionParam)) {
      next.delete("version");
      next.delete("page");
    }
    if (next.toString() !== searchParams.toString()) setSearchParams(next, { replace: true });
  }, [documents, docParam, versionParam, document, searchParams, setSearchParams]);

  function show(changes: { doc?: string; version?: string | null; tab?: Tab }) {
    const next = new URLSearchParams(searchParams);
    if (changes.doc !== undefined) {
      next.set("doc", changes.doc);
      next.delete("version");
      next.delete("page");
    }
    if (changes.version !== undefined) {
      if (changes.version === null) next.delete("version");
      else next.set("version", changes.version);
      next.delete("page");
    }
    if (changes.tab !== undefined) next.set("tab", changes.tab);
    setSearchParams(next);
  }

  function upsert(updated: StandardDocument) {
    setDocuments((current) => sortDocuments([...(current ?? []).filter((item) => item.id !== updated.id), updated]));
  }

  async function deleteVersion(target: StandardVersion) {
    setActionError(null);
    try {
      upsert(await deleteStandardVersion(backendBaseUrl, target.id));
      if (target.id === version?.id) show({ version: null });
    } catch (caught) {
      setActionError(standardDocumentsError(caught));
    }
  }

  const categories = useMemo(() => [...new Set((documents ?? []).map((item) => item.category))], [documents]);
  const panelRowStyle = stacked ? undefined : { height: `max(560px, calc(100dvh - ${pageOffset}px))` };
  const panelColStyle = stacked ? undefined : { height: "100%" };

  let detail: ReactNode;
  if (documents === null && !error) {
    detail = <Skeleton active paragraph={{ rows: 8 }} />;
  } else if (document === null) {
    detail = <Empty description={isAdmin ? "还没有规范，先新增一部" : "暂无规范"} />;
  } else {
    detail = (
      <>
        <StandardDocumentHeader
          document={document}
          version={version}
          isAdmin={isAdmin}
          onSelectVersion={(id) => show({ version: id })}
          onUploadVersion={() => setModal({ type: "add", document })}
          onEditDocument={() => setModal({ type: "edit-document", document })}
          onDeleteDocument={() => setModal({ type: "delete-document", document })}
        />
        {version === null ? (
          <Empty description="暂无版本" style={{ marginBlock: 48 }}>
            {isAdmin ? <Button type="primary" onClick={() => setModal({ type: "add", document })}>上传新版本</Button> : null}
          </Empty>
        ) : (
          <>
            <Tabs
              activeKey={tab}
              onChange={(key) => show({ tab: key as Tab })}
              styles={{ header: { marginBottom: 8 } }}
              tabBarExtraContent={tab === "read" && reading.supported ? (
                <Button type="text" icon={<FullscreenOutlined />} onClick={reading.enter}>全屏阅读</Button>
              ) : undefined}
              items={[
                { key: "read", icon: <ReadOutlined />, label: "原文阅读" },
                {
                  key: "versions",
                  icon: <HistoryOutlined />,
                  label: (
                    <span>
                      版本记录<Typography.Text type="secondary" style={{ marginInlineStart: 6 }}>{document.versions.length}</Typography.Text>
                    </span>
                  ),
                },
                { key: "info", icon: <InfoCircleOutlined />, label: "基本信息" },
              ]}
            />
            <div style={{ flex: 1, minHeight: 0, overflow: tab === "read" ? "hidden" : "auto" }}>
              {tab === "read" ? (
                // 全屏的是这一层：上面加一条窄栏，阅读器占满其余高度。窄栏插在前面不会让 iframe 重建、丢掉翻页位置。
                <div
                  ref={reading.ref}
                  style={{ height: "100%", display: "flex", flexDirection: "column", background: token.colorBgContainer }}
                >
                  {reading.active ? <StandardFullscreenBar document={document} version={version} onExit={reading.exit} /> : null}
                  <div style={{ flex: 1, minHeight: 0 }}>
                    <StandardPdfReader
                      document={document}
                      version={version}
                      page={page}
                      height={stacked && !reading.active ? "80dvh" : "100%"}
                    />
                  </div>
                </div>
              ) : tab === "versions" ? (
                <StandardVersionTable
                  document={document}
                  viewingId={version.id}
                  isAdmin={isAdmin}
                  onRead={(id) => {
                    const next = new URLSearchParams(searchParams);
                    next.set("version", id);
                    next.set("tab", "read");
                    next.delete("page");
                    setSearchParams(next);
                  }}
                  onEdit={(target) => setModal({ type: "edit", document, version: target })}
                  onReplace={(target) => setModal({ type: "replace", document, version: target })}
                  onDelete={deleteVersion}
                />
              ) : (
                <StandardBasicInfo document={document} version={version} />
              )}
            </div>
          </>
        )}
      </>
    );
  }

  return (
    <Flex vertical gap={14}>
      {error ?<Alert type="error" showIcon title={error} action={<Button size="small" onClick={load}>重新加载</Button>} /> : null}
      {actionError ? <Alert type="error" showIcon closable={{ onClose: () => setActionError(null) }} title={actionError} /> : null}
      <Row gutter={[14, 14]} style={panelRowStyle}>
        <Col xs={24} lg={6} xxl={5} style={panelColStyle}>
          <StandardDocumentList
            documents={documents}
            selectedId={document?.id ?? null}
            isAdmin={isAdmin}
            onSelect={(id) => show({ doc: id })}
            onCreate={() => setModal({ type: "create-document" })}
          />
        </Col>
        <Col xs={24} lg={18} xxl={19} style={panelColStyle}>
          <Card
            style={{ height: "100%" }}
            styles={{
              root: { display: "flex", flexDirection: "column" },
              body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column", gap: 8, padding: "16px 20px" },
            }}
          >
            {detail}
          </Card>
        </Col>
      </Row>

      {modal?.type === "create-document" || modal?.type === "edit-document" ? (
        <StandardDocumentFormModal
          mode={modal.type === "create-document"
            ? { type: "create", categories }
            : { type: "edit", document: modal.document, categories }}
          documents={documents ?? []}
          onClose={() => setModal(null)}
          onSwitchToAddVersion={(target, values) => setModal({ type: "add", document: target, initialValues: values })}
          onSaved={(saved) => {
            const created = modal.type === "create-document";
            upsert(saved);
            setModal(null);
            if (created) show({ doc: saved.id });
          }}
        />
      ) : null}
      {modal?.type === "delete-document" ? (
        <DeleteStandardDocumentModal
          document={modal.document}
          onClose={() => setModal(null)}
          onDeleted={() => {
            const removedId = modal.document.id;
            setDocuments((current) => (current ?? []).filter((item) => item.id !== removedId));
            setModal(null);
            setSearchParams(new URLSearchParams(), { replace: true });
          }}
        />
      ) : null}
      {modal?.type === "add" || modal?.type === "edit" || modal?.type === "replace" ? (
        <StandardVersionFormModal
          mode={modal}
          onClose={() => setModal(null)}
          onSaved={(saved) => {
            upsert(saved);
            setModal(null);
            // 从「新增规范」转过来时，右边可能还开着另一部规范。
            if (saved.id !== document?.id) show({ doc: saved.id });
          }}
        />
      ) : null}
    </Flex>
  );
}
