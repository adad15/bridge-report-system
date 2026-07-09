import type { ReviewResponse } from "../../api/reviewApi";
import type { BridgeAnnualInspectionData } from "../../contracts/annualInspection";
import type { ReviewCounts } from "../grouping";

interface OverviewHeaderProps {
  response: ReviewResponse;
  draft: BridgeAnnualInspectionData;
  counts: ReviewCounts;
}

// 顶部导入概览（模块 05 §7.1）：桥梁/年度/导入记录基本信息 + 六项计数 + 顶层
// warnings/errors（红/黄区分）。计数用 counts（来自实时 draft 的 buildStatistics），
// 不用 response.statistics（那是拉取时的快照，编辑后会过期）。
export function OverviewHeader({ response, draft, counts }: OverviewHeaderProps) {
  const { bridge, inspection_year, import_record } = response;

  return (
    <section className="status-panel review-overview">
      <h1>校对工作台</h1>
      <div className="status-grid">
        <span>桥梁名称</span>
        <span>{bridge.bridge_name}</span>
        <span>检测年度</span>
        <span>{inspection_year ? `${inspection_year.inspection_year} 年` : "-"}</span>
        <span>导入记录编号</span>
        <span>{import_record.system_number}</span>
        <span>来源类型</span>
        <span>{import_record.source_type}</span>
        <span>解析规则</span>
        <span>{import_record.importer_name ?? "-"}</span>
        <span>导入状态</span>
        <span>{import_record.import_status}</span>
        <span>病害候选数量</span>
        <span>{counts.defect_count}</span>
        <span>照片候选数量</span>
        <span>{counts.photo_count}</span>
        <span>评分项数量</span>
        <span>{counts.rating_item_count}</span>
        <span>需要处理数量</span>
        <span>{counts.needs_attention_count}</span>
        <span>已确认数量</span>
        <span>{counts.confirmed_count}</span>
        <span>已忽略数量</span>
        <span>{counts.ignored_count}</span>
      </div>
      {draft.errors.length > 0 ? (
        <ul className="review-warning-list">
          {draft.errors.map((item, index) => (
            <li key={`error-${index}`} className="error-text">
              {item.message}
            </li>
          ))}
        </ul>
      ) : null}
      {draft.warnings.length > 0 ? (
        <ul className="review-warning-list">
          {draft.warnings.map((item, index) => (
            <li key={`warning-${index}`} className="warning-text">
              {item.message}
            </li>
          ))}
        </ul>
      ) : null}
    </section>
  );
}
