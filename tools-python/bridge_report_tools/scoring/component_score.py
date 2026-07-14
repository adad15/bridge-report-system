"""JTG/T H21-2011 第 4.1.1 条构件技术状况评分纯函数。

三语言（Python / C++ / TypeScript）逐行同构实现，共享夹具
``samples/scoring/component_score_cases.json`` 保证结果一致：

    将同一构件病害扣分 DP 降序排列：
    U1 = DP1
    Ux = DPx / (100 × sqrt(x)) × (100 - ΣUj)   (x ≥ 2)
    P  = 100 - ΣUx

计算全程使用未舍入 double；任一 DP=100 时构件评分为 0。
展示与比较使用统一自实现 round2（半数远离零），
禁用各语言内建 round 的默认平/半舍规则，防止跨语言漂移。
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Sequence

ROUNDING_SCALE = 2
STANDARD_NAME = "JTG/T H21-2011 4.1.1"


@dataclass(frozen=True)
class ComponentScoreResult:
    score: float
    """未舍入的构件评分。"""

    ordered_deductions: list[float]
    """参与计算的病害扣分，降序排列。"""


def round2(value: float) -> float:
    """两位小数、半数远离零的舍入；与 C++/TS 实现逐位一致。"""
    if value < 0:
        return -math.floor(-value * 100.0 + 0.5) / 100.0
    return math.floor(value * 100.0 + 0.5) / 100.0


def compute_component_score(deductions: Sequence[float]) -> ComponentScoreResult | None:
    """按第 4.1.1 条累计扣分计算构件评分。

    输入为空或任一扣分不在 (0, 100] 内时无法计算，返回 None。
    输入顺序不影响结果：内部先降序排序再累计。
    """
    values = [float(value) for value in deductions]
    if not values:
        return None
    for value in values:
        if not 0 < value <= 100:
            return None

    ordered = sorted(values, reverse=True)
    if ordered[0] == 100:
        return ComponentScoreResult(score=0.0, ordered_deductions=ordered)

    total = 0.0
    for index, deduction in enumerate(ordered, start=1):
        if index == 1:
            u = deduction
        else:
            u = deduction / (100.0 * math.sqrt(float(index))) * (100.0 - total)
        total += u
    return ComponentScoreResult(score=100.0 - total, ordered_deductions=ordered)


def classify_score_validation(
    source_score: float | None,
    calculated_score: float | None,
) -> str:
    """按两位小数比较来源分与复算分，返回自动校验状态。

    复算分缺失 -> 无法复算；来源分缺失 -> 不一致（必须人工显式处理）；
    round2 相等 -> 一致，否则 -> 不一致。
    """
    if calculated_score is None:
        return "无法复算"
    if source_score is None:
        return "不一致"
    if round2(source_score) == round2(calculated_score):
        return "一致"
    return "不一致"
