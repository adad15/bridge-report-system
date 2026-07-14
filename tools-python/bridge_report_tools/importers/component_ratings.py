"""把表 2.x-1 的构件组转换为合同 1.2 的构件评分候选。

来源分来自 Word 构件评分列；复算分按 JTG/T H21-2011 第 4.1.1 条
由组内病害扣分计算。任一病害缺扣分即“无法复算”，保留来源分、
不编造扣分；自动“一致”时预填最终确认分，“不一致/无法复算”留空，
由模块 05 人工显式处理。
"""

from __future__ import annotations

from bridge_report_tools.contracts.annual_inspection import (
    ComponentRatingCandidate,
    ComponentRef,
    ComponentScoreCalculationDetails,
    DefectCandidate,
    WarningItem,
)
from bridge_report_tools.importers.defect_tables import ComponentScoreGroup
from bridge_report_tools.scoring.component_score import (
    ROUNDING_SCALE,
    STANDARD_NAME,
    classify_score_validation,
    compute_component_score,
)


def build_component_rating_candidates(
    groups: list[ComponentScoreGroup],
    defects: list[DefectCandidate],
) -> list[ComponentRatingCandidate]:
    defects_by_id = {defect.candidate_id: defect for defect in defects}
    candidates: list[ComponentRatingCandidate] = []
    for index, group in enumerate(groups, start=1):
        candidate_id = f"component_rating_{index:04d}"
        warnings: list[WarningItem] = [
            warning.model_copy(update={"target_candidate_id": candidate_id})
            for warning in group.warnings
        ]

        deductions = [
            defects_by_id[defect_id].defect_deduction
            for defect_id in group.defect_candidate_ids
            if defect_id in defects_by_id
        ]
        calculated_score: float | None = None
        calculation_details: ComponentScoreCalculationDetails | None = None
        if deductions and all(value is not None for value in deductions):
            result = compute_component_score([value for value in deductions if value is not None])
            if result is not None:
                calculated_score = result.score
                calculation_details = ComponentScoreCalculationDetails(
                    standard=STANDARD_NAME,
                    ordered_deductions=result.ordered_deductions,
                    rounding_scale=ROUNDING_SCALE,
                )

        status = classify_score_validation(group.source_score, calculated_score)
        confirmed_score = group.source_score if status == "一致" else None

        candidates.append(
            ComponentRatingCandidate(
                candidate_id=candidate_id,
                component_ref=ComponentRef(
                    structure_part=group.structure_part,
                    component_name=group.component_name,
                    component_alias=group.component_alias,
                ),
                source_score=group.source_score,
                calculated_score=calculated_score,
                confirmed_score=confirmed_score,
                score_validation_status=status,
                score_resolution_reason=None,
                deduction_defect_candidate_ids=list(group.defect_candidate_ids),
                calculation_details=calculation_details,
                review_status="待确认",
                warnings=warnings,
            )
        )
    return candidates
