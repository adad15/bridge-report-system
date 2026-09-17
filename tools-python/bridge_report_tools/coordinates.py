"""WGS-84 到 GCJ-02 的换算。

库里和接口上的经纬度一律是 WGS-84；高德的底图和静态图接口用 GCJ-02，两者在辽宁一带差
一两百米，正好够把图钉甩到桥外面去。

前端画地图时也做同一件事（frontend/src/bridges/coordinates.ts）。两份实现必须算出同一个
点，测试里有一组两边共用的对照值守着这一点。
"""

from __future__ import annotations

import math

#: 克拉索夫斯基椭球长半轴，GCJ-02 的偏移算法就是按它定义的。
_AXIS = 6378245.0
#: 同一椭球的第一偏心率平方。
_ECCENTRICITY_SQUARED = 0.006693421622965943


def outside_china(lng: float, lat: float) -> bool:
    """境外不偏移。粗略国界框，宽松到把周边海域也算进来：漏判境内才是看得出来的错。"""
    return lng < 72.004 or lng > 137.8347 or lat < 0.8293 or lat > 55.8271


def _transform_lat(lng: float, lat: float) -> float:
    x, y = lng - 105.0, lat - 35.0
    result = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * math.sqrt(abs(x))
    result += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    result += (20.0 * math.sin(y * math.pi) + 40.0 * math.sin(y / 3.0 * math.pi)) * 2.0 / 3.0
    result += (160.0 * math.sin(y / 12.0 * math.pi) + 320 * math.sin(y * math.pi / 30.0)) * 2.0 / 3.0
    return result


def _transform_lng(lng: float, lat: float) -> float:
    x, y = lng - 105.0, lat - 35.0
    result = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * math.sqrt(abs(x))
    result += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    result += (20.0 * math.sin(x * math.pi) + 40.0 * math.sin(x / 3.0 * math.pi)) * 2.0 / 3.0
    result += (150.0 * math.sin(x / 12.0 * math.pi) + 300.0 * math.sin(x / 30.0 * math.pi)) * 2.0 / 3.0
    return result


def wgs84_to_gcj02(lng: float, lat: float) -> tuple[float, float]:
    """WGS-84 转 GCJ-02：交给高德之前用。"""
    if outside_china(lng, lat):
        return lng, lat
    rad_lat = lat / 180.0 * math.pi
    magic = 1 - _ECCENTRICITY_SQUARED * math.sin(rad_lat) ** 2
    sqrt_magic = math.sqrt(magic)
    meridian = _AXIS * (1 - _ECCENTRICITY_SQUARED) / (magic * sqrt_magic)
    parallel = _AXIS / sqrt_magic * math.cos(rad_lat)
    d_lat = _transform_lat(lng, lat) * 180.0 / (meridian * math.pi)
    d_lng = _transform_lng(lng, lat) * 180.0 / (parallel * math.pi)
    return lng + d_lng, lat + d_lat
