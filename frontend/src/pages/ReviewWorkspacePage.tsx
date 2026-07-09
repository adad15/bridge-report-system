import { useEffect, useReducer, useState } from "react";
import { useParams } from "react-router-dom";

import { ApiError } from "../api/apiClient";
import type { ReviewResponse } from "../api/reviewApi";
import { fetchReview } from "../api/reviewApi";
import { backendBaseUrl } from "../config";
import { DefectsSection } from "../review/components/DefectsSection";
import type { SelectedCandidate } from "../review/components/EvidencePanel";
import { EvidencePanel } from "../review/components/EvidencePanel";
import { NeedsAttentionSection } from "../review/components/NeedsAttentionSection";
import { OverviewHeader } from "../review/components/OverviewHeader";
import { PhotosSection } from "../review/components/PhotosSection";
import { RatingsSection } from "../review/components/RatingsSection";
import { RawJsonSection } from "../review/components/RawJsonSection";
import { ReviewActionBar } from "../review/components/ReviewActionBar";
import type { GroupKey } from "../review/components/ReviewSidebar";
import { ReviewSidebar } from "../review/components/ReviewSidebar";
import type { AttentionItem } from "../review/grouping";
import { buildStatistics, needsAttention } from "../review/grouping";
import { reviewDraftReducer } from "../review/reviewDraft";

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
  return <ReviewWorkspaceLoaded response={response} />;
}

function ReviewWorkspaceLoaded({ response }: { response: ReviewResponse }) {
  const [draft, dispatch] = useReducer(reviewDraftReducer, response.parsed_result);
  const [selected, setSelected] = useState<SelectedCandidate | null>(null);
  const [activeGroup, setActiveGroup] = useState<GroupKey>("needs_attention");

  const counts = buildStatistics(draft);
  const attentionItems = needsAttention(draft);

  function selectCandidate(kind: AttentionItem["kind"], candidateId: string) {
    setSelected({ kind, candidateId });
  }

  return (
    <div className="review-workspace">
      <OverviewHeader response={response} draft={draft} counts={counts} />
      <ReviewActionBar />
      <div className="review-columns">
        <ReviewSidebar counts={counts} active={activeGroup} onSelect={setActiveGroup} />
        <div className="review-main">
          {activeGroup === "needs_attention" ? (
            <NeedsAttentionSection items={attentionItems} draft={draft} onSelect={selectCandidate} />
          ) : null}
          {activeGroup === "defects" ? (
            <DefectsSection
              defects={draft.defects}
              selectedCandidateId={selected?.kind === "defect" ? selected.candidateId : null}
              onSelect={(candidateId) => selectCandidate("defect", candidateId)}
              dispatch={dispatch}
            />
          ) : null}
          {activeGroup === "photos" ? (
            <PhotosSection
              photos={draft.photos}
              defects={draft.defects}
              selectedCandidateId={selected?.kind === "photo" ? selected.candidateId : null}
              onSelect={(candidateId) => selectCandidate("photo", candidateId)}
              dispatch={dispatch}
            />
          ) : null}
          {activeGroup === "ratings" ? <RatingsSection ratings={draft.ratings} dispatch={dispatch} /> : null}
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
