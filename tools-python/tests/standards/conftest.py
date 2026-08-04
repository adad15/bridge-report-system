from pathlib import Path

import pytest

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture()
def h21_package_root() -> Path:
    """取版本号最大的 H21 包；写死版本号会在升级规范包时静默失效。"""
    versions = sorted((REPOSITORY_ROOT / "standards" / "technical-condition" / "jtg-t-h21-2011").iterdir())
    return versions[-1]


@pytest.fixture()
def rating_tree_package_root() -> Path:
    versions = sorted((REPOSITORY_ROOT / "standards" / "rating-tree" / "organization-bridge").iterdir())
    return versions[-1]
