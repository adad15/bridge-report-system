import { Card, Flex, Typography, theme } from "antd";
import { useEffect, useRef, useState } from "react";

import { bridgeMediaContentUrl, type BridgeMedia } from "../api/bridgeMediaApi";
import type { BridgeProfile } from "../api/bridgeProfileApi";
import { backendBaseUrl } from "../config";
import { loadAMap, type AMapInstance } from "./amapLoader";
import { validCoordinates, wgs84ToGcj02 } from "./coordinates";

/** 一座桥在图上占多大一块，这个级别看得到周边路网，和正式报告里的取景接近。 */
const DEFAULT_ZOOM = 15;

/**
 * 地理位置。
 *
 * 取代了原来的档案完整度卡片：那张卡片把上面指标行里的同一个百分比又展开印了一遍，
 * 而「这桥在哪」是每个打开档案的人都想知道、系统里却一直没有的信息。
 *
 * 几种降级都要走得通，它们都是本地部署下的常态：
 *
 * * 没录坐标：说清楚去哪儿录，不摆一张空地图；
 * * 没配 key 或机器没有外网：说明原因，有已经存下的地理位置图就显示那张；
 * * 坐标非法：不画，让人回去改，而不是把图钉甩到太平洋上。
 *
 * 地图是只读的。改坐标仍然回桥梁概况的编辑弹窗，看和改分开。报告里那张地理位置图
 * 不在这里生成，生成报告时按档案坐标自动取。
 */
export function BridgeLocationCard({
  profile,
  locationMap,
}: {
  profile: BridgeProfile | null;
  /** 已经存下的地理位置图，地图出不来时拿它兜底。 */
  locationMap?: BridgeMedia;
}) {
  const { token } = theme.useToken();
  const containerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<AMapInstance | null>(null);
  const [unavailable, setUnavailable] = useState<string | null>(null);

  const longitude = profile?.longitude ?? null;
  const latitude = profile?.latitude ?? null;
  const hasPoint =
    longitude !== null && latitude !== null && validCoordinates({ lng: longitude, lat: latitude });

  useEffect(() => {
    if (!hasPoint || longitude === null || latitude === null) return undefined;
    let cancelled = false;

    // 库里存的是 WGS-84，高德底图是 GCJ-02，不转这一下图钉会偏出一两百米。
    const center = wgs84ToGcj02({ lng: longitude, lat: latitude });

    loadAMap()
      .then((AMap) => {
        if (cancelled || !containerRef.current) return;
        const map = new AMap.Map(containerRef.current, {
          zoom: DEFAULT_ZOOM,
          center: [center.lng, center.lat],
          resizeEnable: true,
        });
        map.add(
          new AMap.Marker({
            position: [center.lng, center.lat],
            title: profile?.bridge_name ?? "",
            label: { content: profile?.bridge_name ?? "", direction: "right" },
          })
        );
        mapRef.current = map;
        setUnavailable(null);
      })
      .catch((caught: unknown) => {
        if (cancelled) return;
        const code = caught instanceof Error ? caught.message : "";
        const messages: Record<string, string> = {
          map_key_not_configured: "本机未配置地图服务，暂不显示地图。",
          map_key_rejected: "地图服务拒绝了这个 key，请检查 Web 端 key 与安全密钥。",
        };
        setUnavailable(messages[code] ?? "地图服务连不上，暂不显示地图。");
      });

    return () => {
      cancelled = true;
      mapRef.current?.destroy();
      mapRef.current = null;
    };
  }, [hasPoint, longitude, latitude, profile?.bridge_name]);

  const station = [profile?.route_number, profile?.station_mark].filter(Boolean).join(" · ");
  // 地图出不来的时候，已经存下的那张图就是这张卡片能给的最好的东西。
  const showStoredImage = Boolean(locationMap) && (!hasPoint || unavailable !== null);

  const frame = {
    flex: 1,
    minHeight: 148,
    width: "100%",
    border: `1px solid ${token.colorBorderSecondary}`,
    borderRadius: token.borderRadius,
    overflow: "hidden",
  } as const;

  return (
    // 地图填满这张卡片剩下的空间，卡片高度跟着同一行的另一张卡片走。
    <Card
      size="small"
      title="地理位置"
      extra={station ? <Typography.Text type="secondary">{station}</Typography.Text> : null}
      style={{ height: "100%" }}
      styles={{
        root: { display: "flex", flexDirection: "column" },
        body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column" },
      }}
    >
      {profile && !hasPoint && !locationMap ? (
        <Typography.Text type="secondary">
          档案里还没有经纬度。在桥梁概况里录入后，这里显示桥位地图。
        </Typography.Text>
      ) : null}

      {hasPoint && unavailable && !locationMap ? (
        <Typography.Text type="secondary">{unavailable}</Typography.Text>
      ) : null}

      {showStoredImage && locationMap ? (
        <Flex vertical gap={6} style={{ flex: 1, minHeight: 0 }}>
          <img
            src={bridgeMediaContentUrl(backendBaseUrl, locationMap)}
            alt="已存的地理位置图"
            style={{ ...frame, objectFit: "cover" }}
          />
          <Typography.Text type="secondary">
            {unavailable ?? "档案里还没有经纬度，"}这里显示的是已经存下的地理位置图。
          </Typography.Text>
        </Flex>
      ) : null}

      {hasPoint && !unavailable ? (
        <div ref={containerRef} role="img" aria-label="桥位地图" style={frame} />
      ) : null}
    </Card>
  );
}
