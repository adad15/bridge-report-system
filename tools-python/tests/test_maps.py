"""静态地图抓取与坐标换算。

地图服务是外部依赖，测试里用 httpx 的 MockTransport 顶替，不真发请求。
"""

from __future__ import annotations

import base64
import math

import httpx
import pytest

from bridge_report_tools.coordinates import outside_china, wgs84_to_gcj02
from bridge_report_tools.maps import (
    StaticMapError,
    StaticMapRequest,
    fetch_static_map,
    static_map_params,
)

#: 百股大桥桥位，WGS-84（档案里录的 N41°6'55.2",E121°11'46.7"）。
BAIGU = (121.1963056, 41.1153333)

#: 前端 coordinates.ts 对同一个点算出的 GCJ-02。两份实现必须一致，否则界面上的地图和报告里
#: 的图钉会落在不同位置。改任何一边的算法，这里都会炸。
FRONTEND_GCJ02 = (121.201809105, 41.117282444)

PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
)


def _metres(a: tuple[float, float], b: tuple[float, float]) -> float:
    dx = (a[0] - b[0]) * 111_320 * math.cos(math.radians(a[1]))
    dy = (a[1] - b[1]) * 110_540
    return math.hypot(dx, dy)


def test_conversion_matches_the_frontend_implementation() -> None:
    lng, lat = wgs84_to_gcj02(*BAIGU)

    assert lng == pytest.approx(FRONTEND_GCJ02[0], abs=1e-8)
    assert lat == pytest.approx(FRONTEND_GCJ02[1], abs=1e-8)


def test_conversion_shifts_a_point_in_china_by_a_few_hundred_metres() -> None:
    """这就是换算存在的理由：不转，图钉会落在桥外面。"""
    assert 100 < _metres(BAIGU, wgs84_to_gcj02(*BAIGU)) < 800


def test_points_outside_china_are_left_alone() -> None:
    tokyo = (139.767, 35.681)
    assert outside_china(*tokyo)
    assert wgs84_to_gcj02(*tokyo) == tokyo


def test_request_sends_the_converted_centre_and_marks_it() -> None:
    params = static_map_params(StaticMapRequest(key="k", longitude=BAIGU[0], latitude=BAIGU[1]))

    assert params["location"] == "121.201809,41.117282"
    assert params["markers"] == "mid,,A:121.201809,41.117282"
    assert params["zoom"] == "15"
    assert params["size"] == "1024*640"
    assert params["scale"] == "2"


def test_fetch_returns_the_image() -> None:
    seen: list[httpx.Request] = []

    def handler(request: httpx.Request) -> httpx.Response:
        seen.append(request)
        return httpx.Response(200, headers={"content-type": "image/png"}, content=PNG)

    client = httpx.Client(transport=httpx.MockTransport(handler))
    result = fetch_static_map(StaticMapRequest(key="k", longitude=BAIGU[0], latitude=BAIGU[1]), client)

    assert result.content_type == "image/png"
    assert base64.b64decode(result.image_base64) == PNG
    assert seen[0].url.params["key"] == "k"


def test_fetch_surfaces_the_provider_reason_when_no_image_comes_back() -> None:
    """高德出错回的是 JSON，info 才说得清是 key 不对还是配额用尽。"""

    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(200, json={"status": "0", "info": "INVALID_USER_KEY"})

    client = httpx.Client(transport=httpx.MockTransport(handler))
    with pytest.raises(StaticMapError) as raised:
        fetch_static_map(StaticMapRequest(key="bad", longitude=BAIGU[0], latitude=BAIGU[1]), client)

    assert raised.value.code == "map_static_image_failed"
    assert "INVALID_USER_KEY" in raised.value.message


def test_fetch_reports_an_unreachable_service() -> None:
    def handler(request: httpx.Request) -> httpx.Response:
        raise httpx.ConnectError("offline")

    client = httpx.Client(transport=httpx.MockTransport(handler))
    with pytest.raises(StaticMapError) as raised:
        fetch_static_map(StaticMapRequest(key="k", longitude=BAIGU[0], latitude=BAIGU[1]), client)

    assert raised.value.code == "map_service_unreachable"
