import { useCallback, useEffect, useState } from "react";
import { Link, useParams } from "react-router-dom";

import type { UnboundObservation } from "../api/componentArchiveApi";
import { fetchUnboundObservations } from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { ThreadBindingCard } from "../archive/ThreadBindingCard";
import { backendBaseUrl } from "../config";

// 模块 06 线索整理页（§7.3）：集中处理全桥未绑定线索的当前有效正式观测。
// 未绑定不是错误状态；"暂不确定"仅本地折叠，不产生任何服务端写入。
export function DefectThreadReviewPage() {
  const { bridgeId } = useParams<{ bridgeId: string }>();
  const [observations, setObservations] = useState<UnboundObservation[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [dismissedIds, setDismissedIds] = useState<Set<string>>(new Set());

  const reload = useCallback(() => {
    if (!bridgeId) return;
    fetchUnboundObservations(backendBaseUrl, bridgeId)
      .then((items) => {
        setObservations(items);
        setError(null);
      })
      .catch((caught) => {
        setError(caught instanceof ApiError ? caught.message : "未绑定观测加载失败。");
      });
  }, [bridgeId]);

  useEffect(() => {
    reload();
  }, [reload]);

  if (!bridgeId) {
    return <p>缺少桥梁标识。</p>;
  }

  const visible = (observations ?? []).filter((observation) => !dismissedIds.has(observation.id));

  return (
    <section className="status-panel">
      <h1>病害线索整理</h1>
      <p>
        为未绑定的正式病害观测选择归属线索。系统按同构件、同类型、相近位置给出候选，
        只有你能确认它们是否为同一病害。
        <Link to={`/bridges/${bridgeId}/components`}>返回构件病害档案</Link>
      </p>
      {error ? <p className="error-text">{error}</p> : null}
      {observations === null && !error ? <p>正在加载未绑定观测…</p> : null}
      {observations !== null && observations.length === 0 ? (
        <p className="archive-empty-hint">当前有效观测均已绑定病害线索。</p>
      ) : null}
      {observations !== null && observations.length > 0 && visible.length === 0 ? (
        <p className="archive-empty-hint">
          剩余 {observations.length} 条观测已标记"暂不确定"（仅本次会话折叠，不写入任何状态）。
          <button type="button" onClick={() => setDismissedIds(new Set())}>
            重新展开
          </button>
        </p>
      ) : null}
      {visible.map((observation) => (
        <ThreadBindingCard
          key={observation.id}
          observation={observation}
          onResolved={reload}
          onDismiss={() =>
            setDismissedIds((current) => {
              const next = new Set(current);
              next.add(observation.id);
              return next;
            })
          }
        />
      ))}
    </section>
  );
}
