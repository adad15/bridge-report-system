import { useCallback, useEffect, useState } from "react";

import { bridgeMediaError, fetchBridgeMedia, type BridgeMedia } from "../api/bridgeMediaApi";
import { backendBaseUrl } from "../config";

/**
 * 桥梁图件，供总览页的几张卡片共用。
 *
 * 地理位置卡片要拿地理位置图当离线兜底，桥梁概况卡片要列全部图件，读的是同一份，
 * 所以取数放在页面这一层。
 */
export function useBridgeMedia(bridgeId: string): {
  media: BridgeMedia[];
  error: string | null;
  replace: (item: BridgeMedia) => void;
  remove: (slot: BridgeMedia["slot"]) => void;
} {
  const [media, setMedia] = useState<BridgeMedia[]>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setMedia([]);
    setError(null);
    fetchBridgeMedia(backendBaseUrl, bridgeId)
      .then((items) => {
        if (!cancelled) setMedia(items);
      })
      .catch((caught: unknown) => {
        if (!cancelled) setError(bridgeMediaError(caught));
      });
    return () => {
      cancelled = true;
    };
  }, [bridgeId]);

  // 一个槽位一张图：新的进来就顶掉同槽位的旧的。
  const replace = useCallback((item: BridgeMedia) => {
    setMedia((current) => [...current.filter((existing) => existing.slot !== item.slot), item]);
  }, []);

  const remove = useCallback((slot: BridgeMedia["slot"]) => {
    setMedia((current) => current.filter((existing) => existing.slot !== slot));
  }, []);

  return { media, error, replace, remove };
}
