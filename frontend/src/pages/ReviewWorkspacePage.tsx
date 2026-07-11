import { useEffect, useReducer, useRef, useState } from "react";
import { useNavigate, useParams } from "react-router-dom";

import type { ApiErrorIssue } from "../api/apiClient";
import { ApiError } from "../api/apiClient";
import type { ConfirmResponse, PreflightResponse, ReviewResponse } from "../api/reviewApi";
import { cancelImport, confirmImport, fetchReview, runPreflight, saveReviewDraft } from "../api/reviewApi";
import { backendBaseUrl } from "../config";
import { canPressConfirm, canRunPreflight, parsePreflightDetails, validateRevisionForm } from "../review/confirmFlow";
import type { BridgeAnnualInspectionData } from "../contracts/annualInspection";
import { DefectsSection } from "../review/components/DefectsSection";
import type { SelectedCandidate } from "../review/components/EvidencePanel";
import { EvidencePanel } from "../review/components/EvidencePanel";
import { NeedsAttentionSection } from "../review/components/NeedsAttentionSection";
import { OverviewHeader } from "../review/components/OverviewHeader";
import { RatingsSection } from "../review/components/RatingsSection";
import { RawJsonSection } from "../review/components/RawJsonSection";
import { ReviewActionBar } from "../review/components/ReviewActionBar";
import type { GroupKey } from "../review/components/ReviewSidebar";
import { ReviewSidebar } from "../review/components/ReviewSidebar";
import type { AttentionItem } from "../review/grouping";
import { buildStatistics, needsAttention } from "../review/grouping";
import type { ReviewDraftAction } from "../review/reviewDraft";
import { reviewDraftReducer } from "../review/reviewDraft";
import { deriveReviewSession, shouldClearDirtyAfterSave } from "../review/reviewSession";

export function ReviewWorkspacePage() {
  const { importRecordId } = useParams<{ importRecordId: string }>();

  const [response, setResponse] = useState<ReviewResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

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
  }, [importRecordId]);

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
  return <ReviewWorkspaceLoaded response={response} importRecordId={importRecordId} />;
}

// 保存草稿 / 入库前检查 / 确认入库失败后展示给用户的提示；成功也复用同一个状态
// （kind="success"）显示"已保存"这类短暂反馈。issues 只有 400 契约校验失败
// （code=contract_validation_failed）时后端才会填，逐条列出 {path, message}。
interface SaveMessageState {
  kind: "success" | "error";
  text: string;
  issues?: ApiErrorIssue[];
}

const NO_OP_DISPATCH: (action: ReviewDraftAction) => void = () => {};

function ReviewWorkspaceLoaded({ response, importRecordId }: { response: ReviewResponse; importRecordId: string }) {
  const navigate = useNavigate();
  const [draft, rawDispatch] = useReducer(reviewDraftReducer, response.parsed_result);
  const [selected, setSelected] = useState<SelectedCandidate | null>(null);
  const [expandedDefectId, setExpandedDefectId] = useState<string | null>(null);
  const [activePhotoCandidateId, setActivePhotoCandidateId] = useState<string | null>(null);
  const [activeGroup, setActiveGroup] = useState<GroupKey>("needs_attention");

  const [saveMessage, setSaveMessage] = useState<SaveMessageState | null>(null);
  const [preflight, setPreflight] = useState<PreflightResponse | null>(null);
  const [confirmDialogOpen, setConfirmDialogOpen] = useState(false);
  const [revisionChecked, setRevisionChecked] = useState(false);
  const [revisionNote, setRevisionNote] = useState("");
  const [revisionError, setRevisionError] = useState<string | null>(null);
  const [revisionHint, setRevisionHint] = useState<string | null>(null);
  const [confirmResult, setConfirmResult] = useState<ConfirmResponse | null>(null);
  const [sessionImportStatus, setSessionImportStatus] = useState(response.import_record.import_status);
  const [busy, setBusy] = useState(false);
  // 草稿自上次成功保存以来是否被编辑过。入库前检查 / 确认入库端点只读数据库里已保存的
  // parsed_result_json（不读内存草稿），所以有未保存修改时必须先保存，否则用户会对着旧的
  // 已保存数据跑检查、以为通过了，实际这次编辑不会写进事实表（模块 05 §6.1 的顺序：先保存草稿）。
  const [dirty, setDirty] = useState(false);
  const draftRevision = useRef(0);

  const counts = buildStatistics(draft);
  const attentionItems = needsAttention(draft);
  const reviewSession = deriveReviewSession(sessionImportStatus, response.contract_compatibility);
  const readOnly = reviewSession.readOnly;

  function selectCandidate(kind: AttentionItem["kind"], candidateId: string) {
    setSelected({ kind, candidateId });
    if (kind === "defect") {
      setExpandedDefectId(candidateId);
      setActiveGroup("defect_photos");
    } else if (kind === "photo") {
      setActivePhotoCandidateId(candidateId);
      const linkedDefectId = draft.photos.find((photo) => photo.candidate_id === candidateId)?.linked_defect_candidate_id;
      if (linkedDefectId) setExpandedDefectId(linkedDefectId);
      setActiveGroup("defect_photos");
    }
  }

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
    const saveRevision = draftRevision.current;
    setBusy(true);
    try {
      await saveReviewDraft(backendBaseUrl, importRecordId, draftToSave);
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

  // 批量确认要把"确认后的草稿"发给后端保存，但 dispatch 是异步排队的——dispatch 完
  // 之后，本次函数体里的 draft 变量仍然是旧值，只有下一次渲染才会拿到新 state。
  // 这里改用同一个 reviewDraftReducer 在本地先手算出 nextDraft：dispatch 只用来让
  // React state（继而是 UI）与之同步，真正发给后端保存的是这个手算值，而不是闭包里
  // 还没更新的 draft。
  async function handleBatchConfirmNormal(): Promise<void> {
    const nextDraft = reviewDraftReducer(draft, { type: "batch_confirm_normal_ratings" });
    dispatch({ type: "batch_confirm_normal_ratings" });
    await handleSaveDraft(nextDraft);
  }

  async function handlePreflight(): Promise<void> {
    setBusy(true);
    try {
      const result = await runPreflight(backendBaseUrl, importRecordId);
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
    setBusy(true);
    try {
      const result = await confirmImport(backendBaseUrl, importRecordId, {
        confirm_revision: confirmRevision,
        confirmation_note: note,
      });
      setConfirmResult(result);
      setSessionImportStatus("已确认");
      setConfirmDialogOpen(false);
      setSaveMessage(null);
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
      await cancelImport(backendBaseUrl, importRecordId);
      navigate(`/bridges/${response.bridge.id}`);
    } catch (caught) {
      setSaveMessage({
        kind: "error",
        text: caught instanceof ApiError ? caught.message : "取消导入失败。",
      });
    } finally {
      setBusy(false);
    }
  }

  const actionsDisabled = readOnly || busy;
  const sectionDispatch = readOnly ? NO_OP_DISPATCH : dispatch;

  return (
    <div className="review-workspace">
      {readOnly ? (
        <section className="status-panel review-readonly-banner">
          <p>
            {reviewSession.bannerText}
            {confirmResult
              ? ` 已入库：病害 ${confirmResult.written.defect_observations}、尺寸 ${confirmResult.written.defect_measurements}、` +
                `照片 ${confirmResult.written.defect_photos}、评分 ${confirmResult.written.condition_ratings}；` +
                `年度版本 v${confirmResult.version_number}。`
              : null}
          </p>
          <button type="button" onClick={() => navigate(`/bridges/${response.bridge.id}`)}>
            返回桥梁详情
          </button>
        </section>
      ) : null}
      <OverviewHeader response={response} draft={draft} counts={counts} />
      <ReviewActionBar
        onSaveDraft={actionsDisabled ? undefined : () => void handleSaveDraft(draft)}
        onBatchConfirmNormal={actionsDisabled ? undefined : () => void handleBatchConfirmNormal()}
        onPreflight={canRunPreflight(dirty, busy, readOnly) ? () => void handlePreflight() : undefined}
        onConfirmImport={actionsDisabled || !canPressConfirm(preflight) ? undefined : handleConfirmImportClick}
        onCancelImport={actionsDisabled ? undefined : () => void handleCancelImport()}
      />
      {dirty && !readOnly ? (
        <section className="status-panel review-dirty-hint">
          <p className="warning-text">有未保存的修改，请先点击“保存草稿”，再进行入库前检查。</p>
        </section>
      ) : null}
      {saveMessage ? (
        <section className="status-panel review-save-message">
          <p className={saveMessage.kind === "error" ? "error-text" : undefined}>{saveMessage.text}</p>
          {saveMessage.issues && saveMessage.issues.length > 0 ? (
            <ul className="review-warning-list">
              {saveMessage.issues.map((issue, index) => (
                <li key={`${issue.path}-${index}`} className="error-text">
                  {issue.path}: {issue.message}
                </li>
              ))}
            </ul>
          ) : null}
        </section>
      ) : null}
      {preflight ? (
        <section className="status-panel review-preflight-panel">
          <h2>入库前检查结果</h2>
          <p>{preflight.can_confirm ? "检查通过，可以确认入库。" : "仍有阻断项，暂不能入库。"}</p>
          {preflight.blocking_errors.length > 0 ? (
            <ul className="review-warning-list">
              {preflight.blocking_errors.map((issue, index) => (
                <li key={`blocking-${index}`} className="error-text">
                  {issue.code}：{issue.message}
                  {issue.target_candidate_id ? `（${issue.target_candidate_id}）` : ""}
                </li>
              ))}
            </ul>
          ) : null}
          {preflight.warnings.length > 0 ? (
            <ul className="review-warning-list">
              {preflight.warnings.map((issue, index) => (
                <li key={`warning-${index}`} className="warning-text">
                  {issue.code}：{issue.message}
                  {issue.target_candidate_id ? `（${issue.target_candidate_id}）` : ""}
                </li>
              ))}
            </ul>
          ) : null}
        </section>
      ) : null}
      {confirmDialogOpen ? (
        <section className="status-panel review-confirm-dialog">
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
            <button type="button" onClick={handleConfirmDialogSubmit} disabled={busy}>
              确认修订版入库
            </button>
            <button type="button" onClick={() => setConfirmDialogOpen(false)} disabled={busy}>
              取消
            </button>
          </div>
        </section>
      ) : null}
      <div className="review-columns">
        <ReviewSidebar counts={counts} active={activeGroup} onSelect={setActiveGroup} />
        <div className="review-main">
          {activeGroup === "needs_attention" ? (
            <NeedsAttentionSection items={attentionItems} draft={draft} onSelect={selectCandidate} />
          ) : null}
          {activeGroup === "defect_photos" ? (
            <DefectsSection
              draft={draft}
              importRecordId={importRecordId}
              baseUrl={backendBaseUrl}
              selectedCandidateId={expandedDefectId}
              selectedPhotoCandidateId={activePhotoCandidateId}
              onSelect={(candidateId) => {
                setExpandedDefectId((current) => current === candidateId ? null : candidateId);
                setSelected({ kind: "defect", candidateId });
                setActivePhotoCandidateId(null);
              }}
              dispatch={sectionDispatch}
              disabled={actionsDisabled}
            />
          ) : null}
          {activeGroup === "ratings" ? <RatingsSection ratings={draft.ratings} dispatch={sectionDispatch} disabled={actionsDisabled} /> : null}
          {activeGroup === "source_evidence" ? (
            <section className="status-panel">
              <h2>来源证据</h2>
              <p>请选择左侧候选后，在右侧证据面板查看来源章节、表名、行号和原文。</p>
            </section>
          ) : null}
          {activeGroup === "raw_json" ? <RawJsonSection draft={draft} /> : null}
        </div>
        <EvidencePanel selected={selected} draft={draft} />
      </div>
    </div>
  );
}
