import { useState, type Dispatch } from "react";

import type { ComponentRatingCandidate, Ratings, ReviewStatus } from "../../contracts/annualInspection";
import { roundScoreToTwoDecimals } from "../componentScore";
import type { ReviewDraftAction } from "../reviewDraft";
import { reviewTargetId } from "../reviewNavigation";
import { ratingRows } from "./displayHelpers";

const REVIEW_STATUSES: ReviewStatus[] = ["待确认", "已确认", "已修改", "已忽略"];

interface RatingsSectionProps {
  ratings: Ratings;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled?: boolean;
}

function formatScore(value: number | null | undefined): string {
  if (value === null || value === undefined) {
    return "-";
  }
  return String(roundScoreToTwoDecimals(value));
}

function scoreDelta(rating: ComponentRatingCandidate): string {
  if (
    rating.source_score === null ||
    rating.source_score === undefined ||
    rating.calculated_score === null ||
    rating.calculated_score === undefined
  ) {
    return "-";
  }
  const delta = roundScoreToTwoDecimals(
    roundScoreToTwoDecimals(rating.calculated_score) - roundScoreToTwoDecimals(rating.source_score)
  );
  return delta > 0 ? `+${delta}` : String(delta);
}

function statusBadgeClass(status: ComponentRatingCandidate["score_validation_status"]): string {
  if (status === "一致") return "severity-badge severity-info";
  if (status === "不一致" || status === "无法复算") return "severity-badge severity-warning";
  return "severity-badge severity-info";
}

// 构件评分行（合同 1.2）：展示 Word 来源分、规范复算分、差值与校验状态。
// 「不一致/无法复算」必须填写原因后显式选择「接受 Word 值」或「采用复算值」；
// 已解决的行展示原因并允许撤销；自动「一致」时最终分为预填的来源分，只读。
function ComponentRatingRow({
  rating,
  dispatch,
  disabled,
}: {
  rating: ComponentRatingCandidate;
  dispatch: Dispatch<ReviewDraftAction>;
  disabled: boolean;
}) {
  const [reason, setReason] = useState("");
  const componentLabel = rating.component_ref.component_alias ?? rating.component_ref.component_name;
  const unresolved =
    rating.score_validation_status === "不一致" || rating.score_validation_status === "无法复算";
  const manuallyResolved =
    rating.score_validation_status === "人工接受Word值" || rating.score_validation_status === "人工采用复算值";
  const reasonReady = reason.trim() !== "";

  return (
    <tr id={reviewTargetId("rating", rating.candidate_id)} tabIndex={-1}>
      <td>{componentLabel}</td>
      <td>{formatScore(rating.source_score)}</td>
      <td title={rating.calculated_score === null || rating.calculated_score === undefined ? undefined : String(rating.calculated_score)}>
        {formatScore(rating.calculated_score)}
      </td>
      <td>{scoreDelta(rating)}</td>
      <td>
        <span className={statusBadgeClass(rating.score_validation_status)}>{rating.score_validation_status}</span>
      </td>
      <td>{formatScore(rating.confirmed_score)}</td>
      <td>
        {unresolved ? (
          <input
            aria-label={`处理原因 ${rating.candidate_id}`}
            placeholder="填写处理原因…"
            disabled={disabled}
            value={reason}
            onChange={(event) => setReason(event.target.value)}
          />
        ) : (
          <span className="component-rating-reason">{rating.score_resolution_reason ?? "-"}</span>
        )}
      </td>
      <td>
        <select
          aria-label={`构件评分校对状态 ${rating.candidate_id}`}
          disabled={disabled}
          value={rating.review_status}
          onChange={(event) =>
            dispatch({
              type: "set_component_rating_status",
              candidateId: rating.candidate_id,
              status: event.target.value as ReviewStatus,
            })
          }
        >
          {REVIEW_STATUSES.map((status) => (
            <option key={status} value={status}>
              {status}
            </option>
          ))}
        </select>
      </td>
      <td>
        {unresolved ? (
          <div className="component-rating-actions">
            <button
              type="button"
              disabled={disabled || !reasonReady || rating.source_score === null || rating.source_score === undefined}
              onClick={() => {
                dispatch({
                  type: "resolve_component_score",
                  candidateId: rating.candidate_id,
                  choice: "accept_source",
                  reason,
                });
                setReason("");
              }}
            >
              接受Word值
            </button>
            <button
              type="button"
              disabled={
                disabled || !reasonReady || rating.calculated_score === null || rating.calculated_score === undefined
              }
              onClick={() => {
                dispatch({
                  type: "resolve_component_score",
                  candidateId: rating.candidate_id,
                  choice: "adopt_calculated",
                  reason,
                });
                setReason("");
              }}
            >
              采用复算值
            </button>
          </div>
        ) : manuallyResolved ? (
          <button
            type="button"
            disabled={disabled}
            onClick={() => dispatch({ type: "reset_component_score_resolution", candidateId: rating.candidate_id })}
          >
            撤销选择
          </button>
        ) : (
          "-"
        )}
      </td>
    </tr>
  );
}

// "技术状况评定" 分组（模块 05 §7.3/§8.3）。用 ratingRows 把全桥/结构分部/评价部件
// 摊平成一张表：全桥总分和等级可编辑；结构分部评分/权重只读展示、等级可编辑；
// 评价部件评分可编辑（没有等级/权重）。三级校对状态都可以单独下拉切换。
// 部件/结构分部/全桥总分不重算；构件评分按 JTG/T H21-2011 4.1.1 复算校验（见下方子表）。
export function RatingsSection({ ratings, dispatch, disabled = false }: RatingsSectionProps) {
  const rows = ratingRows(ratings);

  return (
    <section className="status-panel">
      <h2>技术状况评定</h2>
      <div className="table-scroll">
        <table className="data-table">
          <thead>
            <tr>
              <th>层级</th>
              <th>名称</th>
              <th>评分</th>
              <th>等级</th>
              <th>权重</th>
              <th>校对状态</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row, index) => (
              <tr id={reviewTargetId("rating", row.candidateId)} tabIndex={-1} key={`${row.level}-${row.name}-${index}`}>
                <td>{row.level}</td>
                <td>{row.name}</td>
                <td>
                  {row.level === "结构分部" ? (
                    row.score
                  ) : (
                    <input
                      disabled={disabled}
                      type="number"
                      value={row.score}
                      onChange={(event) => {
                        const next = event.target.valueAsNumber;
                        if (Number.isNaN(next)) return;
                        if (row.target === "overall") {
                          dispatch({ type: "edit_rating_field", target: "overall", field: "total_score", value: next });
                        } else if ("evaluation" in row.target) {
                          dispatch({ type: "edit_rating_field", target: row.target, field: "part_score", value: next });
                        }
                      }}
                    />
                  )}
                </td>
                <td>
                  {row.grade === null ? (
                    "-"
                  ) : (
                    <input
                      disabled={disabled}
                      type="text"
                      value={row.grade}
                      onChange={(event) => {
                        const value = event.target.value;
                        if (row.target === "overall") {
                          dispatch({ type: "edit_rating_field", target: "overall", field: "overall_grade", value });
                        } else if ("part" in row.target) {
                          dispatch({ type: "edit_rating_field", target: row.target, field: "grade", value });
                        }
                      }}
                    />
                  )}
                </td>
                <td>{row.weight === null ? "-" : row.weight}</td>
                <td>
                  <select
                    disabled={disabled}
                    value={row.status}
                    onChange={(event) =>
                      dispatch({ type: "set_rating_status", target: row.target, status: event.target.value as ReviewStatus })
                    }
                  >
                    {REVIEW_STATUSES.map((status) => (
                      <option key={status} value={status}>
                        {status}
                      </option>
                    ))}
                  </select>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {ratings.component_ratings.length > 0 ? (
        <>
          <h3>构件评分（JTG/T H21-2011 4.1.1 复算校验）</h3>
          <div className="table-scroll">
            <table className="data-table component-rating-table">
              <thead>
                <tr>
                  <th>构件</th>
                  <th>来源分</th>
                  <th>复算分</th>
                  <th>差值</th>
                  <th>校验状态</th>
                  <th>最终确认分</th>
                  <th>处理原因</th>
                  <th>校对状态</th>
                  <th>操作</th>
                </tr>
              </thead>
              <tbody>
                {ratings.component_ratings.map((rating) => (
                  <ComponentRatingRow
                    key={rating.candidate_id}
                    rating={rating}
                    dispatch={dispatch}
                    disabled={disabled}
                  />
                ))}
              </tbody>
            </table>
          </div>
        </>
      ) : null}
    </section>
  );
}
