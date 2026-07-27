import { type ReactNode, useCallback, useEffect, useMemo, useReducer, useRef, useState } from "react";
import { useNavigate, useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import { previewAssessment, type AssessmentIssue } from "../api/assessmentApi";
import type { ConfirmResponse, EditLockSummary, PreflightResponse, ReopenScope, ReviewResponse } from "../api/reviewApi";
import {
  acquireEditLock,
  cancelImport,
  confirmImport,
  fetchReview,
  forceReleaseEditLock,
  heartbeatEditLock,
  releaseEditLock,
  reopenImport,
  restoreReopenedImport,
  runPreflight,
  saveReviewDraft,
} from "../api/reviewApi";
import { useAuth } from "../auth/AuthContext";
import { backendBaseUrl } from "../config";
import { canPressConfirm, canRunPreflight, formatConfirmSuccess, parsePreflightDetails, validateRevisionForm } from "../review/confirmFlow";
import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import { bindingProgress, fetchComponentBinding, type ComponentBindingOverview } from "../api/importBindingApi";
import { ComponentBindingWorkspace } from "../review/binding/ComponentBindingWorkspace";
import { DefectsSection } from "../review/components/DefectsSection";
import type { SelectedCandidate } from "../review/components/EvidencePanel";
import { EvidencePanel } from "../review/components/EvidencePanel";
import { NeedsAttentionSection } from "../review/components/NeedsAttentionSection";
import { OverviewHeader } from "../review/components/OverviewHeader";
import { AssessmentSection } from "../review/components/AssessmentSection";
import { RawJsonSection } from "../review/components/RawJsonSection";
import { ReviewActionBar } from "../review/components/ReviewActionBar";
import type { SaveMessageState } from "../review/components/ReviewMessageDock";
import { ReviewMessageDock } from "../review/components/ReviewMessageDock";
import type { GroupKey } from "../review/components/ReviewSidebar";
import { ReviewSidebar } from "../review/components/ReviewSidebar";
import type { AttentionItem } from "../review/grouping";
import { assessmentIssueToAttention, buildStatistics, needsAttention } from "../review/grouping";
import { assessmentReducer, initialAssessmentState } from "../review/assessmentState";
import type { ReviewDraftAction } from "../review/reviewDraft";
import { reviewDraftReducer } from "../review/reviewDraft";
import { deriveReviewSession, shouldClearDirtyAfterSave } from "../review/reviewSession";
import { reviewTargetId } from "../review/reviewNavigation";
import { bridgeOverviewPath, inspectionWorkspacePath } from "../workspace/workspaceState";

export function canModifyDefectStructure(
  actionsDisabled: boolean,
  reopenScope: "warnings_only" | "full" | null | undefined,
  isAdmin: boolean,
): boolean {
  if (actionsDisabled || reopenScope === "warnings_only") return false;
  return reopenScope !== "full" || isAdmin;
}

function ReviewWorkspacePanel({
  group,
  activeGroup,
  visitedGroups,
  children,
}: {
  group: GroupKey;
  activeGroup: GroupKey;
  visitedGroups: ReadonlySet<GroupKey>;
  children: ReactNode;
}) {
  if (!visitedGroups.has(group)) return null;
  return (
    <div data-review-group={group} hidden={activeGroup !== group}>
      {children}
    </div>
  );
}

export function ReviewWorkspacePage() {
  const { importRecordId } = useParams<{ importRecordId: string }>();

  const [response, setResponse] = useState<ReviewResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  // 「放弃修改」等需要以数据库最新状态整体重建页面的操作通过 +1 触发重新拉取；
  // 拉取期间 response 置空 -> ReviewWorkspaceLoaded 卸载重挂，useReducer 重新初始化。
  const [reloadNonce, setReloadNonce] = useState(0);

  useEffect(() => {
    if (!importRecordId) return;
    let cancelled = false;

    setResponse(null);
    setError(null);
    fetchReview(backendBaseUrl, importRecordId)
      .then((result) => {
        if (cancelled) return;
        setResponse(result);
        setError(null);
      })
      .catch((caught: unknown) => {
        if (cancelled) return;
        setResponse(null);
        setError(caught instanceof ApiError ? caught.message : "加载校对数据失败");
      });

    return () => {
      cancelled = true;
    };
  }, [importRecordId, reloadNonce]);

  if (!importRecordId) {
    return (
      <section className="status-panel">
        <p className="error-text">缺少导入记录编号。</p>
      </section>
    );
  }

  if (error) {
    return (
      <section className="status-panel">
        <p className="error-text">{error}</p>
      </section>
    );
  }

  if (!response) {
    return (
      <section className="status-panel">
        <p>加载中…</p>
      </section>
    );
  }

  // response 到位之后再挂载持有 useReducer 的子组件：这样 useReducer 的初始 state
  // 永远是真实的 parsed_result，不需要在本组件里对 useReducer 做任何条件调用
  // （不满足 React hooks 规则的写法是 fetch 完成前就 useReducer(reducer, undefined)
  // 之类的占位状态，再在 effect 里想办法灌数据——那样会让 state 类型变得别扭）。
  return (
    <ReviewWorkspaceLoaded
      response={response}
      importRecordId={importRecordId}
      onReload={() => setReloadNonce((nonce) => nonce + 1)}
    />
  );
}

const NO_OP_DISPATCH: (action: ReviewDraftAction) => void = () => {};

type EditLockPhase = "not_required" | "acquiring" | "held" | "blocked" | "retrying" | "lost";

const TERMINAL_EDIT_LOCK_ERROR_CODES = new Set([
  "edit_lock_invalid",
  "edit_lock_expired",
  "edit_lock_force_released",
  "edit_lock_required",
]);

// 心跳正常续租只会改变 expires_at。页面不展示该值，也不应仅因此重渲染数百条病害；
// 真实归属、持有人或取得时间变化时才需要刷新可见锁摘要。
function hasSameVisibleLockSummary(left: EditLockSummary | null, right: EditLockSummary): boolean {
  return left !== null
    && left.owner_username === right.owner_username
    && left.owner_display_name === right.owner_display_name
    && left.owned_by_current_user === right.owned_by_current_user
    && left.acquired_at === right.acquired_at;
}

function lockSummaryFromError(error: unknown): EditLockSummary | null {
  if (!(error instanceof ApiError) || typeof error.details !== "object" || error.details === null) return null;
  const lock = (error.details as { lock?: unknown }).lock;
  if (typeof lock !== "object" || lock === null) return null;
  const candidate = lock as Partial<EditLockSummary>;
  return typeof candidate.owner_display_name === "string" && typeof candidate.acquired_at === "string"
    && typeof candidate.expires_at === "string" && typeof candidate.owner_username === "string"
    && typeof candidate.owned_by_current_user === "boolean"
    ? candidate as EditLockSummary
    : null;
}

function ReviewWorkspaceLoaded({
  response,
  importRecordId,
  onReload,
}: {
  response: ReviewResponse;
  importRecordId: string;
  onReload: () => void;
}) {
  const navigate = useNavigate();
  const { user } = useAuth();
  const [draft, rawDispatch] = useReducer(reviewDraftReducer, response.parsed_result);
  const [selected, setSelected] = useState<SelectedCandidate | null>(null);
  const [expandedDefectId, setExpandedDefectId] = useState<string | null>(null);
  const [activePhotoCandidateId, setActivePhotoCandidateId] = useState<string | null>(null);
  const [activeGroup, setActiveGroup] = useState<GroupKey>("needs_attention");
  const [visitedGroups, setVisitedGroups] = useState<ReadonlySet<GroupKey>>(
    () => new Set<GroupKey>(["needs_attention"])
  );
  const activateGroup = useCallback((group: GroupKey) => {
    setVisitedGroups((current) => {
      if (current.has(group)) return current;
      const next = new Set(current);
      next.add(group);
      return next;
    });
    setActiveGroup(group);
  }, []);
  // 侧栏要在用户点进绑定分区之前就显示待处理数，故这里先取一次概览；
  // 之后由绑定分区通过 onOverviewChange 上报，保证绑定操作后计数同步。
  const [bindingPending, setBindingPending] = useState<number | null>(null);
  const [bindingOverview, setBindingOverview] = useState<ComponentBindingOverview | null>(null);
  const [pendingNavigation, setPendingNavigation] = useState<AttentionItem | null>(null);

  useEffect(() => {
    if (!importRecordId) return;
    let cancelled = false;
    fetchComponentBinding(backendBaseUrl, importRecordId)
      .then((overview) => {
        if (cancelled) return;
        setBindingOverview(overview);
        // 台账未确认时绑定不可用，保持 null 让侧栏显示 "-"。
        setBindingPending(
          overview.inventory_confirmed ? bindingProgress(overview).pending : null
        );
      })
      .catch(() => { /* 绑定计数取不到不影响校对，侧栏显示 "-" 即可。 */ });
    return () => { cancelled = true; };
  }, [importRecordId]);
  const [navigationMessage, setNavigationMessage] = useState<string | null>(null);
  const navigationHighlightTimer = useRef<number | null>(null);
  const [evidenceOpen, setEvidenceOpen] = useState(false);

  const [saveMessage, setSaveMessage] = useState<SaveMessageState | null>(null);
  const [preflight, setPreflight] = useState<PreflightResponse | null>(null);
  const [confirmDialogOpen, setConfirmDialogOpen] = useState(false);
  const [revisionChecked, setRevisionChecked] = useState(false);
  const [revisionNote, setRevisionNote] = useState("");
  const [revisionError, setRevisionError] = useState<string | null>(null);
  const [revisionHint, setRevisionHint] = useState<string | null>(null);
  const [confirmResult, setConfirmResult] = useState<ConfirmResponse | null>(null);
  const [sessionImportStatus, setSessionImportStatus] = useState(response.import_record.import_status);
  // 重开校对现场：已确认记录被翻回待校对时非空；再确认（修订版）或放弃修改后清空。
  const [reopenState, setReopenState] = useState(response.reopen);
  const [busy, setBusy] = useState(false);
  // 草稿自上次成功保存以来是否被编辑过。入库前检查 / 确认入库端点只读数据库里已保存的
  // parsed_result_json（不读内存草稿），所以有未保存修改时必须先保存，否则用户会对着旧的
  // 已保存数据跑检查、以为通过了，实际这次编辑不会写进事实表（模块 05 §6.1 的顺序：先保存草稿）。
  const [dirty, setDirty] = useState(false);
  const draftRevision = useRef(0);
  const lockTokenRef = useRef<string | null>(null);
  const lockSummaryRef = useRef<EditLockSummary | null>(response.edit_lock);
  const heartbeatInFlightRef = useRef(false);
  const leaseExpiryTimerRef = useRef<number | null>(null);
  const [lockToken, setLockToken] = useState<string | null>(null);
  const [lockSummary, setLockSummary] = useState<EditLockSummary | null>(response.edit_lock);
  const [lockPhase, setLockPhase] = useState<EditLockPhase>("not_required");
  const [lockMessage, setLockMessage] = useState<string | null>(null);
  const [assessmentState, assessmentDispatch] = useReducer(assessmentReducer, initialAssessmentState);
  const assessmentAbortRef = useRef<AbortController | null>(null);

  // needsAttention 对上千条病害是 O(n) 级扫描；这里算一次，counts 与 attentionItems 复用同一份，
  // 避免每次 draft 变动重复计算（buildStatistics 收到长度后就不再自己算一遍）。
  const draftAttention = useMemo(
    () => needsAttention(draft, bindingOverview),
    [draft, bindingOverview],
  );
  const counts = useMemo(
    () => buildStatistics(draft, false, draftAttention.length),
    [draft, draftAttention],
  );
  const attentionItems = useMemo(() => [
    ...draftAttention.filter((item) => item.kind !== "rating"),
    ...(assessmentState.response?.issues ?? []).map(assessmentIssueToAttention),
  ], [draftAttention, assessmentState.response]);
  const displayedCounts = useMemo(() => ({
    ...counts,
    rating_item_count: assessmentState.response?.result
      ? 1 + assessmentState.response.result.structure_parts.length
      : assessmentState.response?.issues.length ?? 0,
    needs_attention_count: attentionItems.length,
  }), [counts, assessmentState.response, attentionItems.length]);
  const reviewSession = deriveReviewSession(
    sessionImportStatus,
    response.contract_compatibility,
    reopenState?.scope ?? null
  );
  const readOnly = reviewSession.readOnly;
  const isAdmin = user?.role === "admin";
  const hasWarningDefects = draft.defects.some((defect) => defect.warnings.length > 0);
  // 只有已确认的 2.0 记录允许进入受控重开流程。
  const canReopen = sessionImportStatus === "已确认" && response.contract_compatibility === "native_2_0" && !busy;
  const needsEditLock = !reviewSession.readOnly;
  const returnPath = response.inspection_year
    ? inspectionWorkspacePath(response.bridge.id, response.inspection_year.id)
    : bridgeOverviewPath(response.bridge.id);
  const returnLabel = response.inspection_year
    ? `返回 ${response.inspection_year.inspection_year} 年度工作台`
    : "返回桥梁概览";

  function clearLeaseExpiryTimer(): void {
    if (leaseExpiryTimerRef.current !== null) {
      window.clearTimeout(leaseExpiryTimerRef.current);
      leaseExpiryTimerRef.current = null;
    }
  }

  function scheduleLeaseExpiry(summary: EditLockSummary): void {
    clearLeaseExpiryTimer();
    const expiresAt = Date.parse(summary.expires_at);
    if (!Number.isFinite(expiresAt)) return;
    const expectedExpiry = summary.expires_at;
    leaseExpiryTimerRef.current = window.setTimeout(() => {
      if (lockTokenRef.current === null || lockSummaryRef.current?.expires_at !== expectedExpiry) return;
      if (Date.parse(expectedExpiry) > Date.now()) {
        scheduleLeaseExpiry(lockSummaryRef.current);
        return;
      }
      forgetLock("lost");
      setLockMessage("编辑权租约已到期，请刷新页面后重新取得编辑权。");
    }, Math.max(0, expiresAt - Date.now()) + 50);
  }

  function rememberLock(token: string, summary: EditLockSummary): void {
    lockTokenRef.current = token;
    lockSummaryRef.current = summary;
    setLockToken(token);
    setLockSummary(summary);
    scheduleLeaseExpiry(summary);
    setLockPhase("held");
    setLockMessage(null);
  }

  function forgetLock(phase: EditLockPhase = "not_required"): void {
    clearLeaseExpiryTimer();
    heartbeatInFlightRef.current = false;
    lockTokenRef.current = null;
    lockSummaryRef.current = null;
    setLockToken(null);
    setLockSummary(null);
    setLockPhase(phase);
  }

  useEffect(() => {
    if (!needsEditLock) {
      setLockPhase("not_required");
      return;
    }
    if (lockTokenRef.current !== null) {
      setLockPhase("held");
      return () => {
        const token = lockTokenRef.current;
        if (token !== null) void releaseEditLock(backendBaseUrl, importRecordId, token, true).catch(() => undefined);
      };
    }

    let cancelled = false;
    // 延后一拍可避开 React StrictMode 的首次 setup→cleanup 探测，防止开发态重复抢锁。
    const timer = window.setTimeout(() => {
      setLockPhase("acquiring");
      acquireEditLock(backendBaseUrl, importRecordId)
        .then((result) => {
          if (cancelled) {
            void releaseEditLock(backendBaseUrl, importRecordId, result.lock_token, true).catch(() => undefined);
            return;
          }
          rememberLock(result.lock_token, result.lock);
        })
        .catch((caught: unknown) => {
          if (cancelled) return;
          setLockSummary(lockSummaryFromError(caught) ?? response.edit_lock);
          setLockPhase("blocked");
          setLockMessage(caught instanceof ApiError ? caught.message : "无法取得编辑锁。");
        });
    }, 0);

    return () => {
      cancelled = true;
      window.clearTimeout(timer);
      clearLeaseExpiryTimer();
      heartbeatInFlightRef.current = false;
      const token = lockTokenRef.current;
      if (token !== null) void releaseEditLock(backendBaseUrl, importRecordId, token, true).catch(() => undefined);
    };
  }, [importRecordId, needsEditLock]);

  useEffect(() => {
    if (lockToken === null) return;
    let cancelled = false;
    const timer = window.setInterval(() => {
      if (heartbeatInFlightRef.current) return;
      heartbeatInFlightRef.current = true;
      heartbeatEditLock(backendBaseUrl, importRecordId, lockToken)
        .then((result) => {
          if (cancelled) return;
          const previousSummary = lockSummaryRef.current;
          lockSummaryRef.current = result.lock;
          scheduleLeaseExpiry(result.lock);
          if (!hasSameVisibleLockSummary(previousSummary, result.lock)) setLockSummary(result.lock);
          setLockPhase((current) => current === "retrying" ? "held" : current);
          setLockMessage((current) => current === null ? current : null);
        })
        .catch((caught: unknown) => {
          if (cancelled) return;
          if (caught instanceof ApiError && TERMINAL_EDIT_LOCK_ERROR_CODES.has(caught.code)) {
            forgetLock("lost");
          } else if (lockSummaryRef.current !== null && Date.parse(lockSummaryRef.current.expires_at) <= Date.now()) {
            forgetLock("lost");
          } else {
            setLockPhase("retrying");
          }
          setLockMessage(caught instanceof ApiError ? caught.message : "编辑锁续租失败，正在等待恢复。" );
        })
        .finally(() => {
          heartbeatInFlightRef.current = false;
        });
    }, 30_000);
    return () => {
      cancelled = true;
      heartbeatInFlightRef.current = false;
      window.clearInterval(timer);
    };
  }, [importRecordId, lockToken]);

  useEffect(() => {
    if (!dirty) return;
    const warn = (event: BeforeUnloadEvent) => {
      event.preventDefault();
      event.returnValue = "";
    };
    window.addEventListener("beforeunload", warn);
    return () => window.removeEventListener("beforeunload", warn);
  }, [dirty]);

  // "已保存"这类成功反馈 3 秒后自动消失（布局设计 §8）；错误消息常驻，由用户手动关闭。
  useEffect(() => {
    if (saveMessage?.kind !== "success") return;
    const timer = window.setTimeout(() => setSaveMessage(null), 3000);
    return () => window.clearTimeout(timer);
  }, [saveMessage]);

  function selectCandidate(item: AttentionItem) {
    setSelected({ kind: item.kind, candidateId: item.candidateId });
    setNavigationMessage(null);
    setPendingNavigation({ ...item });
    if (item.kind === "defect") {
      setExpandedDefectId(item.candidateId);
      setActivePhotoCandidateId(null);
      activateGroup("defect_photos");
    } else if (item.kind === "photo") {
      setActivePhotoCandidateId(item.candidateId);
      const linkedDefectId = draft.photos.find((photo) => photo.candidate_id === item.candidateId)?.linked_defect_candidate_id;
      if (linkedDefectId) setExpandedDefectId(linkedDefectId);
      activateGroup("defect_photos");
    } else if (item.kind === "rating") {
      activateGroup("ratings");
    }
  }

  useEffect(() => {
    if (pendingNavigation === null) return;
    const timer = window.setTimeout(() => {
      let targetId: string;
      if (pendingNavigation.kind === "defect") {
        targetId = pendingNavigation.targetField
          ? reviewTargetId("defect-field", pendingNavigation.candidateId, pendingNavigation.targetField)
          : reviewTargetId("defect", pendingNavigation.candidateId);
      } else if (pendingNavigation.kind === "photo") {
        const photo = draft.photos.find((candidate) => candidate.candidate_id === pendingNavigation.candidateId);
        targetId = reviewTargetId(photo?.linked_defect_candidate_id ? "photo" : "unlinked-photo", pendingNavigation.candidateId);
      } else if (pendingNavigation.kind === "rating") {
        targetId = reviewTargetId("rating", pendingNavigation.candidateId);
      } else {
        setPendingNavigation(null);
        return;
      }

      const target = document.getElementById(targetId);
      if (target === null) {
        setNavigationMessage("目标数据已变化，请刷新待处理列表。");
        setPendingNavigation(null);
        return;
      }
      if (navigationHighlightTimer.current !== null) window.clearTimeout(navigationHighlightTimer.current);
      document.querySelectorAll(".review-target-highlight").forEach((element) => element.classList.remove("review-target-highlight"));
      const reducedMotion = window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ?? false;
      target.scrollIntoView?.({ behavior: reducedMotion ? "auto" : "smooth", block: "center" });
      target.classList.add("review-target-highlight");
      target.focus({ preventScroll: true });
      navigationHighlightTimer.current = window.setTimeout(() => {
        target.classList.remove("review-target-highlight");
        navigationHighlightTimer.current = null;
      }, 2000);
      setPendingNavigation(null);
    }, 0);
    return () => window.clearTimeout(timer);
  }, [activeGroup, draft.photos, expandedDefectId, activePhotoCandidateId, pendingNavigation]);

  useEffect(() => () => {
    if (navigationHighlightTimer.current !== null) window.clearTimeout(navigationHighlightTimer.current);
  }, []);

  // 任何编辑草稿的 action 都让上一次入库前检查结果失效：PreflightResponse 只反映
  // "跑检查那一刻"的草稿状态，草稿改了之后旧结果里的 can_confirm 已经不可信，必须
  // 逼用户对最新草稿重新点一次"入库前检查"（canPressConfirm 依赖 preflight !== null）。
  function dispatch(action: ReviewDraftAction): void {
    draftRevision.current += 1;
    rawDispatch(action);
    setPreflight(null);
    setDirty(true);
  }

  async function handleSaveDraft(draftToSave: BridgeAnnualInspectionData): Promise<boolean> {
    if (lockToken === null) {
      setSaveMessage({ kind: "error", text: "当前页面没有编辑权，无法保存。" });
      return false;
    }
    const saveRevision = draftRevision.current;
    setBusy(true);
    try {
      await saveReviewDraft(backendBaseUrl, importRecordId, draftToSave, lockToken);
      const savedLatestRevision = shouldClearDirtyAfterSave(saveRevision, draftRevision.current);
      setSaveMessage({ kind: "success", text: savedLatestRevision ? "已保存" : "本次保存已完成，但仍有较新的修改未保存。" });
      if (savedLatestRevision) setDirty(false);
      return true;
    } catch (caught) {
      if (caught instanceof ApiError) {
        setSaveMessage({ kind: "error", text: caught.message, issues: caught.issues });
      } else {
        setSaveMessage({ kind: "error", text: "保存草稿失败。" });
      }
      return false;
    } finally {
      setBusy(false);
    }
  }

  async function handlePreflight(): Promise<void> {
    if (lockToken === null) return;
    setBusy(true);
    try {
      const result = await runPreflight(backendBaseUrl, importRecordId, lockToken);
      setPreflight(result);
      setSaveMessage(null);
    } catch (caught) {
      setPreflight(null);
      setSaveMessage({
        kind: "error",
        text: caught instanceof ApiError ? caught.message : "入库前检查失败。",
      });
    } finally {
      setBusy(false);
    }
  }

  async function submitConfirm(confirmRevision: boolean, note: string): Promise<void> {
    if (lockToken === null) return;
    setBusy(true);
    try {
      const result = await confirmImport(backendBaseUrl, importRecordId, {
        confirm_revision: confirmRevision,
        confirmation_note: note,
      }, lockToken);
      setConfirmResult(result);
      setSessionImportStatus("已确认");
      // 后端在确认事务里清空了重开列；本地同步退出重开态。
      setReopenState(null);
      setConfirmDialogOpen(false);
      setSaveMessage(null);
      forgetLock("not_required");
    } catch (caught) {
      if (caught instanceof ApiError && caught.code === "revision_confirmation_required") {
        // 后端在确认那一刻发现同桥同年已有当前有效事实，但本次请求没有带
        // confirm_revision=true——回到修订确认弹窗，保留用户已经填的勾选/说明，
        // 只追加一句提示，不清空表单。
        setRevisionHint(caught.message);
        setConfirmDialogOpen(true);
      } else if (caught instanceof ApiError && caught.code === "preflight_failed") {
        // confirm 端点自己重新跑了一遍入库前检查且 can_confirm=false，409 body 就是
        // 一份 PreflightReport（见 reviewApi.ts 顶部说明）。用它刷新检查结果面板，
        // 而不是让用户手动再点一次"入库前检查"才能看到最新的阻断项。
        const refreshed = parsePreflightDetails(caught.details);
        if (refreshed) {
          setPreflight(refreshed);
        }
        setConfirmDialogOpen(false);
        setSaveMessage({ kind: "error", text: "入库前检查未通过，请查看下方检查结果后重新处理。" });
      } else {
        setSaveMessage({
          kind: "error",
          text: caught instanceof ApiError ? caught.message : "确认入库失败。",
        });
      }
    } finally {
      setBusy(false);
    }
  }

  function handleConfirmImportClick(): void {
    if (preflight?.requires_revision_confirmation) {
      setRevisionError(null);
      setRevisionHint(null);
      setConfirmDialogOpen(true);
      return;
    }
    void submitConfirm(false, "人工校对完成");
  }

  function handleConfirmDialogSubmit(): void {
    const validation = validateRevisionForm(revisionChecked, revisionNote);
    if (!validation.valid) {
      setRevisionError(validation.error ?? "请完整填写修订确认信息。");
      return;
    }
    setRevisionError(null);
    void submitConfirm(true, revisionNote.trim());
  }

  async function handleCancelImport(): Promise<void> {
    const confirmed = window.confirm("确定要取消该导入记录吗？取消后该导入记录不再参与入库。");
    if (!confirmed) return;
    setBusy(true);
    try {
      if (lockToken === null) return;
      await cancelImport(backendBaseUrl, importRecordId, lockToken);
      forgetLock("not_required");
      navigate(returnPath);
    } catch (caught) {
      setSaveMessage({
        kind: "error",
        text: caught instanceof ApiError ? caught.message : "取消导入失败。",
      });
    } finally {
      setBusy(false);
    }
  }

  // 重开校对：已确认 -> 待校对（后端快照草稿）。成功后页面就地切回可编辑态，
  // 不需要重新拉取——草稿内容没有变化，变的只有状态与可编辑范围。
  async function handleReopen(scope: ReopenScope): Promise<void> {
    setBusy(true);
    try {
      const result = await reopenImport(backendBaseUrl, importRecordId, scope);
      rememberLock(result.lock_token, result.edit_lock);
      setSessionImportStatus("待校对");
      setReopenState({
        reopened_at: new Date().toISOString(),
        reopened_by_username: user?.username ?? "",
        scope,
      });
      setConfirmResult(null);
      setPreflight(null);
      setSaveMessage(null);
      setDirty(false);
    } catch (caught) {
      setSaveMessage({
        kind: "error",
        text: caught instanceof ApiError ? caught.message : "重开校对失败。",
      });
    } finally {
      setBusy(false);
    }
  }

  // 放弃修改：后端把草稿还原为重开快照并翻回已确认；本地内存草稿已经脏了，
  // 必须整体重新拉取重建（onReload -> 外层重新 fetch -> 本组件卸载重挂）。
  async function handleAbandonReopen(): Promise<void> {
    const confirmed = window.confirm("确定放弃本次重开修改吗？草稿将恢复为确认入库时的内容。");
    if (!confirmed) return;
    setBusy(true);
    try {
      if (lockToken === null) return;
      await restoreReopenedImport(backendBaseUrl, importRecordId, lockToken);
      forgetLock("not_required");
      onReload();
    } catch (caught) {
      setSaveMessage({
        kind: "error",
        text: caught instanceof ApiError ? caught.message : "放弃修改失败。",
      });
      setBusy(false);
    }
  }

  async function handleBackToBridge(): Promise<void> {
    if (dirty && !window.confirm("有未保存的修改，离开后将丢失。确定离开吗？")) return;
    const token = lockTokenRef.current;
    if (token !== null) {
      try {
        await releaseEditLock(backendBaseUrl, importRecordId, token);
      } catch {
        // 正常释放失败由 2 分钟租约兜底，不阻止用户离开。
      }
      forgetLock("not_required");
    }
    navigate(returnPath);
  }

  async function handleForceRelease(): Promise<void> {
    if (!isAdmin || lockSummary === null) return;
    const reason = window.prompt(`请输入强制解除“${lockSummary.owner_display_name}”编辑锁的原因：`)?.trim() ?? "";
    if (reason === "" || !window.confirm("强制解锁后，原编辑页面将不能继续保存。确定继续吗？")) return;
    setBusy(true);
    try {
      await forceReleaseEditLock(backendBaseUrl, importRecordId, reason);
      const acquired = await acquireEditLock(backendBaseUrl, importRecordId);
      rememberLock(acquired.lock_token, acquired.lock);
    } catch (caught) {
      setSaveMessage({ kind: "error", text: caught instanceof ApiError ? caught.message : "强制解锁失败。" });
    } finally {
      setBusy(false);
    }
  }

  const lockAllowsEditing = !needsEditLock || (lockToken !== null && (lockPhase === "held" || lockPhase === "retrying"));
  const effectiveReadOnly = readOnly || !lockAllowsEditing;
  const actionsDisabled = effectiveReadOnly || busy;
  const sectionDispatch = effectiveReadOnly ? NO_OP_DISPATCH : dispatch;

  const runAssessment = useCallback(() => {
    if (lockToken === null || effectiveReadOnly) return;
    assessmentAbortRef.current?.abort();
    const controller = new AbortController();
    assessmentAbortRef.current = controller;
    const revision = draftRevision.current;
    assessmentDispatch({ type: "requested", revision });
    previewAssessment(backendBaseUrl, importRecordId, draft, revision, lockToken, controller.signal)
      .then((assessmentResponse) => {
        assessmentDispatch({ type: "resolved", response: assessmentResponse, currentRevision: draftRevision.current });
      })
      .catch((caught: unknown) => {
        if (controller.signal.aborted) return;
        assessmentDispatch({
          type: "failed",
          revision,
          currentRevision: draftRevision.current,
          message: caught instanceof ApiError ? caught.message : "系统评定试算失败。",
        });
      });
  }, [draft, effectiveReadOnly, importRecordId, lockToken]);

  useEffect(() => {
    if (effectiveReadOnly || lockToken === null) return;
    const timer = window.setTimeout(runAssessment, 650);
    return () => window.clearTimeout(timer);
  }, [effectiveReadOnly, lockToken, runAssessment]);

  useEffect(() => () => assessmentAbortRef.current?.abort(), []);

  function selectAssessmentIssue(issue: AssessmentIssue): void {
    const item = assessmentIssueToAttention(issue);
    if (item.kind === "import") {
      activateGroup("needs_attention");
      setNavigationMessage(item.message);
      return;
    }
    selectCandidate(item);
  }

  // 重开 warnings_only 态：仅带警告的病害可编辑；full 态与正常待校对态全部可编辑。
  const isDefectEditable =
    reopenState?.scope === "warnings_only"
      ? (defect: BridgeAnnualInspectionData["defects"][number]) => defect.warnings.length > 0
      : undefined;

  const lockNotice = lockPhase === "held"
    ? "你正在编辑此导入记录。"
    : lockPhase === "acquiring"
      ? "正在取得编辑权……"
      : lockPhase === "retrying"
        ? "编辑锁续租暂时失败，正在重试；当前租约到期前仍可继续编辑。"
        : lockSummary !== null
          ? `${lockSummary.owned_by_current_user ? "你已在另一个页面" : lockSummary.owner_display_name}正在编辑此导入记录。`
          : lockMessage ?? (lockPhase === "lost" ? "编辑权已失效，请刷新页面。" : null);

  // 只读时整条底栏换成只读横幅（布局设计 §9），入库统计拼在横幅文字里。
  const readOnlyNotice = effectiveReadOnly
    ? `${!readOnly && lockNotice ? lockNotice : reviewSession.bannerText ?? ""}${
        confirmResult
          ? ` ${formatConfirmSuccess(confirmResult)}`
          : ""
      }`
    : undefined;

  return (
    <div className="review-workspace">
      <OverviewHeader response={response} draft={draft} counts={counts} />
      {lockNotice ? (
        <div className={`review-edit-lock-banner review-edit-lock-${lockPhase}`}>
          <span>{lockNotice}</span>
          {lockSummary ? <span className="review-reopen-meta">开始时间：{lockSummary.acquired_at}</span> : null}
          {isAdmin && lockSummary !== null && lockPhase === "blocked" ? (
            <button type="button" disabled={busy} onClick={() => void handleForceRelease()}>管理员强制解锁</button>
          ) : null}
        </div>
      ) : null}
      {/* 重开校对态横幅：可编辑态下 bannerText 非空即重开中，提示范围与后续流程。 */}
      {!readOnly && reviewSession.bannerText ? (
        <div className="review-reopen-banner">
          <span>{reviewSession.bannerText}</span>
          {reopenState ? <span className="review-reopen-meta">重开人：{reopenState.reopened_by_username}</span> : null}
        </div>
      ) : null}
      <div className="review-body">
        <ReviewSidebar
          counts={displayedCounts}
          bindingPendingCount={bindingPending}
          active={activeGroup}
          onSelect={activateGroup}
        />
        <div className="review-main">
          {/* 来源证据是针对某条病害/照片的，绑定分区里没有"当前选中候选"这个概念，
              按钮恒为禁用状态，纯占位。 */}
          {activeGroup !== "component_binding" ? (
            <div className="review-main-tools">
              <button type="button" disabled={!selected} onClick={() => setEvidenceOpen(true)}>查看来源证据</button>
            </div>
          ) : null}
          {navigationMessage ? <p className="warning-text review-navigation-message">{navigationMessage}</p> : null}
          <ReviewWorkspacePanel
            group="needs_attention"
            activeGroup={activeGroup}
            visitedGroups={visitedGroups}
          >
            <NeedsAttentionSection items={attentionItems} draft={draft} onSelect={selectCandidate} />
          </ReviewWorkspacePanel>
          {/* 不传 onEnterReview：这里已经在校对页内，绑定完直接切到别的分区即可。 */}
          <ReviewWorkspacePanel
            group="component_binding"
            activeGroup={activeGroup}
            visitedGroups={visitedGroups}
          >
            <ComponentBindingWorkspace
              importId={importRecordId}
              bridgeId={response.bridge.id}
              onOverviewChange={(overview) => {
                setBindingOverview(overview);
                setBindingPending(
                  overview.inventory_confirmed ? bindingProgress(overview).pending : null
                );
              }}
            />
          </ReviewWorkspacePanel>
          <ReviewWorkspacePanel
            group="defect_photos"
            activeGroup={activeGroup}
            visitedGroups={visitedGroups}
          >
            <DefectsSection
              draft={draft}
              importRecordId={importRecordId}
              baseUrl={backendBaseUrl}
              bridgeId={response.bridge.id}
              componentInventory={response.component_inventory}
              selectedCandidateId={expandedDefectId}
              selectedPhotoCandidateId={activePhotoCandidateId}
              onSelect={(candidateId) => {
                setExpandedDefectId((current) => current === candidateId ? null : candidateId);
                setSelected({ kind: "defect", candidateId });
                setActivePhotoCandidateId(null);
              }}
              dispatch={sectionDispatch}
              disabled={actionsDisabled}
              allowStructureChanges={canModifyDefectStructure(actionsDisabled, reopenState?.scope, isAdmin)}
              isDefectEditable={isDefectEditable}
            />
          </ReviewWorkspacePanel>
          <ReviewWorkspacePanel
            group="ratings"
            activeGroup={activeGroup}
            visitedGroups={visitedGroups}
          >
            <AssessmentSection
              phase={assessmentState.phase}
              response={assessmentState.response}
              error={assessmentState.error}
              onRetry={runAssessment}
              onSelectIssue={selectAssessmentIssue}
            />
          </ReviewWorkspacePanel>
          <ReviewWorkspacePanel
            group="raw_json"
            activeGroup={activeGroup}
            visitedGroups={visitedGroups}
          >
            <RawJsonSection draft={draft} />
          </ReviewWorkspacePanel>
        </div>
      </div>
      <div className="review-footer">
        <ReviewMessageDock saveMessage={saveMessage} preflight={preflight} onDismissSaveMessage={() => setSaveMessage(null)} />
        <ReviewActionBar
          dirty={dirty}
          readOnlyNotice={readOnlyNotice}
          onBackToBridge={() => void handleBackToBridge()}
          backLabel={returnLabel}
          onSaveDraft={actionsDisabled ? undefined : () => void handleSaveDraft(draft)}
          onPreflight={canRunPreflight(dirty, busy, effectiveReadOnly) ? () => void handlePreflight() : undefined}
          onConfirmImport={actionsDisabled || !canPressConfirm(preflight) ? undefined : handleConfirmImportClick}
          onCancelImport={actionsDisabled || reopenState !== null ? undefined : () => void handleCancelImport()}
          onAbandonReopen={!effectiveReadOnly && reopenState !== null && !busy ? () => void handleAbandonReopen() : undefined}
          onReopenWarnings={canReopen && hasWarningDefects ? () => void handleReopen("warnings_only") : undefined}
          onReopenFull={canReopen && isAdmin ? () => void handleReopen("full") : undefined}
        />
      </div>
      {confirmDialogOpen ? (
        // 修订确认是关键决策弹窗：点击遮罩不关闭（避免误触丢失已填写的修订说明），只有"取消"按钮关闭。
        <div className="review-modal-backdrop" role="presentation">
          <section className="status-panel review-confirm-dialog" role="dialog" aria-modal="true" aria-label="确认修订版入库">
            <h2>确认修订版入库</h2>
            <p>同桥同年已有当前有效事实，需显式确认为修订版才能继续入库；确认后旧版本会标记为已被修订。</p>
            {revisionHint ? <p className="warning-text">{revisionHint}</p> : null}
            <label className="review-confirm-dialog-checkbox">
              <input
                type="checkbox"
                disabled={busy}
                checked={revisionChecked}
                onChange={(event) => setRevisionChecked(event.target.checked)}
              />
              作为修订版确认
            </label>
            <textarea
              disabled={busy}
              className="review-confirm-dialog-note"
              value={revisionNote}
              onChange={(event) => setRevisionNote(event.target.value)}
              placeholder="请填写修订说明"
              rows={3}
            />
            {revisionError ? <p className="error-text">{revisionError}</p> : null}
            <div className="review-confirm-dialog-actions">
              <button type="button" onClick={() => setConfirmDialogOpen(false)} disabled={busy}>
                取消
              </button>
              <button type="button" className="review-action-primary" onClick={handleConfirmDialogSubmit} disabled={busy}>
                确认修订版入库
              </button>
            </div>
          </section>
        </div>
      ) : null}
      {evidenceOpen ? (
        <div className="review-modal-backdrop" role="presentation" onMouseDown={() => setEvidenceOpen(false)}>
          <div className="review-evidence-dialog" role="dialog" aria-modal="true" aria-label="来源证据" onMouseDown={(event) => event.stopPropagation()}>
            <button className="review-dialog-close" type="button" aria-label="关闭来源证据" onClick={() => setEvidenceOpen(false)}>×</button>
            <EvidencePanel selected={selected} draft={draft} />
          </div>
        </div>
      ) : null}
    </div>
  );
}
