import json
import math
from pathlib import Path

from bridge_report_tools.scoring.component_score import (
    classify_score_validation,
    compute_component_score,
    round2,
)


FIXTURE_PATH = (
    Path(__file__).resolve().parents[2] / "samples" / "scoring" / "component_score_cases.json"
)


def load_cases() -> list[dict]:
    fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    assert fixture["standard"] == "JTG/T H21-2011 4.1.1"
    return fixture["cases"]


def test_shared_fixture_cases_all_match() -> None:
    cases = load_cases()
    assert len(cases) >= 10

    for case in cases:
        result = compute_component_score(case["deductions"])
        if case["expected_unrounded"] is None:
            assert result is None, case["name"]
            continue
        assert result is not None, case["name"]
        assert abs(result.score - case["expected_unrounded"]) < 1e-9, case["name"]
        assert round2(result.score) == case["expected_rounded"], case["name"]
        assert result.ordered_deductions == case["expected_ordered_deductions"], case["name"]


def test_key_values_match_change_proposal() -> None:
    assert compute_component_score([35]).score == 65.0
    two = compute_component_score([35, 20])
    assert round2(two.score) == 55.81
    # 计算全程不提前舍入：未舍入值与公式手算逐位一致。
    manual = 100.0 - 35.0 - 20.0 / (100.0 * math.sqrt(2.0)) * 65.0
    assert two.score == manual
    assert compute_component_score([20, 35]).score == two.score
    assert compute_component_score([100]).score == 0.0
    assert compute_component_score([50, 100]).score == 0.0


def test_ordered_deductions_are_descending() -> None:
    result = compute_component_score([15, 25, 20])
    assert result.ordered_deductions == [25.0, 20.0, 15.0]


def test_classify_score_validation() -> None:
    assert classify_score_validation(None, None) == "无法复算"
    assert classify_score_validation(55.81, None) == "无法复算"
    assert classify_score_validation(None, 55.80761184457488) == "不一致"
    assert classify_score_validation(55.81, 55.80761184457488) == "一致"
    assert classify_score_validation(65.0, 55.80761184457488) == "不一致"


def test_round2_is_deterministic_and_symmetric() -> None:
    # 55.805 的最近 double 乘以 100 恰为 5580.5，半数远离零进位得 55.81；
    # 该断言锚定三语言在同一 IEEE 754 运算序列下的一致行为。
    assert round2(55.805) == 55.81
    assert round2(55.8076118445748) == 55.81
    assert round2(65.0) == 65.0
    assert round2(-1.005) == -round2(1.005)
    assert round2(0.125) == 0.13  # 0.125 精确可表示，半数远离零进位
