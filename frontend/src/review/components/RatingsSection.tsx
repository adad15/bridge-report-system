import type { Dispatch } from "react";

import type { Ratings, ReviewStatus } from "../../contracts/annualInspection";
import type { ReviewDraftAction } from "../reviewDraft";
import { ratingRows } from "./displayHelpers";

const REVIEW_STATUSES: ReviewStatus[] = ["待确认", "已确认", "已修改", "已忽略"];

interface RatingsSectionProps {
  ratings: Ratings;
  dispatch: Dispatch<ReviewDraftAction>;
}

// "技术状况评定" 分组（模块 05 §7.3/§8.3）。用 ratingRows 把全桥/结构分部/评价部件
// 摊平成一张表：全桥总分和等级可编辑；结构分部评分/权重只读展示、等级可编辑；
// 评价部件评分可编辑（没有等级/权重）。三级校对状态都可以单独下拉切换。
// 第一版不重算总分，纯粹接受人工填写的数值。
export function RatingsSection({ ratings, dispatch }: RatingsSectionProps) {
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
              <tr key={`${row.level}-${row.name}-${index}`}>
                <td>{row.level}</td>
                <td>{row.name}</td>
                <td>
                  {row.level === "结构分部" ? (
                    row.score
                  ) : (
                    <input
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
    </section>
  );
}
