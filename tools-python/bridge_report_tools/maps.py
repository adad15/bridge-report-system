"""静态地图抓取：报告 §1.1「图 1-1 地理位置图」的来源。

生成报告时由 C++ 流水线调用，按档案里的桥位坐标取一张静态图存进归档。Word 里放不下一张
活地图，所以这一步必须在出报告之前做完。

**为什么这件事在 Python 这边做。** 本机这套 drogon 的 HTTPS 客户端连不出去；而工具服务
本来就带着 httpx，也本来就是报告链路上必需的一环。

**坐标进来是 WGS-84**，就是库里存的那一对。到 GCJ-02 的换算在这里做：高德的静态图接口
要 GCJ-02，不换算图钉会偏出一两百米。
"""

from __future__ import annotations

import base64
from typing import Literal

import httpx
from pydantic import BaseModel, ConfigDict, Field

from bridge_report_tools.coordinates import wgs84_to_gcj02

#: 高德静态图接口。单边上限 1024，scale=2 出两倍像素。
AMAP_STATIC_MAP_URL = "https://restapi.amap.com/v3/staticmap"


class StaticMapError(Exception):
    """地图服务没给回图片。`message` 直接面向用户，说清楚是哪一步不对。"""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


class StaticMapRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    provider: Literal["amap"] = "amap"
    #: 服务端 key。它只在服务之间传，永远不下发到浏览器。
    key: str = Field(min_length=1)
    #: 桥位坐标，WGS-84，就是档案里存的那一对。
    longitude: float = Field(ge=-180, le=180)
    latitude: float = Field(ge=-90, le=90)
    #: 这个级别看得到周边路网，和正式报告里那张图的取景接近。
    zoom: int = Field(default=15, ge=3, le=19)
    width: int = Field(default=1024, ge=200, le=1024)
    height: int = Field(default=640, ge=200, le=1024)
    scale: int = Field(default=2, ge=1, le=2)
    timeout_seconds: float = Field(default=20.0, gt=0, le=60)


class StaticMapResponse(BaseModel):
    content_type: str
    image_base64: str
    bytes: int


def static_map_params(request: StaticMapRequest) -> dict[str, str]:
    """拼高德静态图接口的查询参数。单独拿出来，测试不必真发请求就能看坐标换算对不对。"""
    lng, lat = wgs84_to_gcj02(request.longitude, request.latitude)
    center = f"{lng:.6f},{lat:.6f}"
    return {
        "location": center,
        "zoom": str(request.zoom),
        "size": f"{request.width}*{request.height}",
        "scale": str(request.scale),
        # 图钉标在桥位上，和正式报告里那张图一样。
        "markers": f"mid,,A:{center}",
        "key": request.key,
    }


def fetch_static_map(
    request: StaticMapRequest, client: httpx.Client | None = None
) -> StaticMapResponse:
    params = static_map_params(request)
    owned = client is None
    http = client or httpx.Client(timeout=request.timeout_seconds)
    try:
        response = http.get(AMAP_STATIC_MAP_URL, params=params)
    except httpx.HTTPError as error:
        raise StaticMapError(
            "map_service_unreachable", f"连不上地图服务：{error.__class__.__name__}。"
        ) from error
    finally:
        if owned:
            http.close()

    content_type = response.headers.get("content-type", "")
    # 出错时高德回的是一段 JSON 而不是图片，里面的 info 才说得清是 key 不对还是配额用尽。
    if response.status_code != 200 or not content_type.startswith("image/"):
        detail = "地图服务没有返回图片。"
        try:
            info = response.json().get("info")
        except ValueError:
            info = None
        if info:
            detail = f"地图服务返回：{info}。"
        raise StaticMapError("map_static_image_failed", detail)

    return StaticMapResponse(
        content_type=content_type.split(";")[0].strip(),
        image_base64=base64.b64encode(response.content).decode("ascii"),
        bytes=len(response.content),
    )
