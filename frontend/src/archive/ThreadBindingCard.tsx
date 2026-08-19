import { useEffect, useState } from "react";

import type { ThreadSuggestion, UnboundObservation } from "../api/componentArchiveApi";
import {
  bindObservationThread,
  createDefectThread,
  defectPhotoContentUrl,
  fetchThreadSuggestions,
  threadBindingErrorMessage,
} from "../api/componentArchiveApi";
import { ApiError } from "../api/apiClient";
import { backendBaseUrl } from "../config";

interface ThreadBindingCardProps {
  observation: UnboundObservation;
  /** 绑定或创建成功后由父页面刷新未绑定列表。 */
  onResolved: () => void;
  /** 暂不确定：仅本地折叠，不发任何请求，未绑定不是错误状态。 */
  onDismiss: () => void;
}

function matchBasisBadges(suggestion: ThreadSuggestion): string[] {
  const badges = ["同构件"];
  if (suggestion.match_basis.same_defect_type) badges.push("同类型");
  if (suggestion.match_basis.location_exact) badges.push("位置全等");
  if (suggestion.match_basis.location_contains) badges.push("位置相近");
  return badges;
}

// 线索整理页的未绑定观测卡片（模块 06 §7.3）：展示年份/构件/类型/详细位置/尺寸/照片，
// 同构件候选建议附匹配依据；三个动作 = 绑定已有线索 / 创建新线索 / 暂不确定。
// 系统只给建议，绑定一律由用户显式发起。
export function ThreadBindingCard({ observation, onResolved, onDismiss }: ThreadBindingCardProps) {
  const [suggestions, setSuggestions] = useState<ThreadSuggestion[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [createOpen, setCreateOpen] = useState(false);
  // 创建弹窗预填标准病害类型与标准详细位置（来自年度观测原文，可编辑后提交）。
  const [threadType, setThreadType] = useState(observation.defect_type);
  const [threadLocation, setThreadLocation] = useState(observation.defect_location ?? "");

  useEffect(() => {
    let cancelled = false;
    fetchThreadSuggestions(backendBaseUrl, observation.id)
      .then((items) => {
        if (!cancelled) setSuggestions(items);
      })
      .catch(() => {
        if (!cancelled) setSuggestions([]);
      });
    return () => {
      cancelled = true;
    };
  }, [observation.id]);

  function reportError(caught: unknown, fallback: string): void {
    if (caught instanceof ApiError) {
      setError(threadBindingErrorMessage(caught.code, caught.message));
      return;
    }
    setError(fallback);
  }

  async function bindTo(threadId: string): Promise<void> {
    setBusy(true);
    setError(null);
    try {
      await bindObservationThread(backendBaseUrl, observation.id, {
        defect_thread_id: threadId,
        expected_observation_updated_at: observation.updated_at,
        confirm_rebind: false,
      });
      onResolved();
    } catch (caught) {
      reportError(caught, "绑定失败，请稍后重试。");
    } finally {
      setBusy(false);
    }
  }

  async function createThread(): Promise<void> {
    if (threadType.trim() === "" || threadLocation.trim() === "") {
      setError("标准病害类型与标准详细位置为必填项。");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      await createDefectThread(backendBaseUrl, {
        bridge_component_id: observation.component.id,
        defect_type: threadType.trim(),
        defect_location: threadLocation.trim(),
        first_observation_id: observation.id,
        expected_observation_updated_at: observation.updated_at,
      });
      onResolved();
    } catch (caught) {
      reportError(caught, "创建线索失败，请稍后重试。");
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="archive-binding-card">
      <header className="archive-binding-head">
        <strong>{observation.inspection_year}</strong>
        <span>
          {observation.component.structure_part}｜{observation.component.component_type}
        </span>
        <span>{observation.defect_type}</span>
        <span>位置：{observation.defect_location || "未记录"}</span>
        <span>标度 {observation.scale ?? "-"}</span>
      </header>
      {observation.measurements.length > 0 ? (
        <p className="archive-binding-measurements">
          {observation.measurements.map((item) => item.raw_text).join("；")}
        </p>
      ) : null}
      {observation.photos.length > 0 ? (
        <div className="archive-photo-strip archive-photo-strip-compact">
          {observation.photos.map((photo) => (
            <img
              key={photo.id}
              src={defectPhotoContentUrl(backendBaseUrl, photo.id)}
              alt={`照片 ${photo.photo_number}`}
            />
          ))}
        </div>
      ) : null}

      <div className="archive-binding-suggestions">
        <h4>同构件线索候选</h4>
        {suggestions === null ? (
          <p>正在加载候选…</p>
        ) : suggestions.length === 0 ? (
          <p className="archive-empty-hint">没有匹配的既有线索，可创建新线索或暂不确定。</p>
        ) : (
          <ul>
            {suggestions.map((suggestion) => (
              <li key={suggestion.id}>
                <span>
                  <strong>{suggestion.defect_type}</strong>｜标准位置：{suggestion.defect_location || "未记录"}
                  {matchBasisBadges(suggestion).map((badge) => (
                    <em key={badge} className="archive-match-badge">
                      {badge}
                    </em>
                  ))}
                </span>
                <button type="button" disabled={busy} onClick={() => void bindTo(suggestion.id)}>
                  绑定该线索
                </button>
              </li>
            ))}
          </ul>
        )}
      </div>

      {createOpen ? (
        <div className="archive-binding-create">
          <label>
            标准病害类型
            <input
              aria-label={`标准病害类型 ${observation.id}`}
              value={threadType}
              onChange={(event) => setThreadType(event.target.value)}
            />
          </label>
          <label>
            标准详细位置
            <input
              aria-label={`标准详细位置 ${observation.id}`}
              value={threadLocation}
              onChange={(event) => setThreadLocation(event.target.value)}
            />
          </label>
          <button type="button" disabled={busy} onClick={() => void createThread()}>
            确认创建并绑定
          </button>
          <button type="button" disabled={busy} onClick={() => setCreateOpen(false)}>
            取消
          </button>
        </div>
      ) : (
        <div className="archive-binding-actions">
          <button type="button" disabled={busy} onClick={() => setCreateOpen(true)}>
            创建新线索
          </button>
          <button type="button" disabled={busy} onClick={onDismiss}>
            暂不确定
          </button>
        </div>
      )}
      {error ? <p className="error-text">{error}</p> : null}
    </section>
  );
}
