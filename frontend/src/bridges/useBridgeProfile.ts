import { useCallback, useEffect, useState } from "react";

import {
  bridgeProfileError,
  fetchBridgeProfile,
  type BridgeProfile,
} from "../api/bridgeProfileApi";
import { backendBaseUrl } from "../config";

/**
 * 桥梁档案，供总览页的几张卡片共用。
 *
 * 取数放在页面这一层而不是各张卡片里：地理位置和桥梁概况读的是同一份档案，各取各的
 * 会发两次请求，改完坐标后还会出现一张卡片更新了、另一张还是旧值的情况。
 */
export function useBridgeProfile(bridgeId: string): {
  profile: BridgeProfile | null;
  error: string | null;
  setProfile: (profile: BridgeProfile) => void;
} {
  const [profile, setProfile] = useState<BridgeProfile | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setProfile(null);
    setError(null);
    fetchBridgeProfile(backendBaseUrl, bridgeId)
      .then((body) => {
        if (!cancelled) setProfile(body);
      })
      .catch((caught: unknown) => {
        if (!cancelled) setError(bridgeProfileError(caught));
      });
    return () => {
      cancelled = true;
    };
  }, [bridgeId]);

  const replace = useCallback((next: BridgeProfile) => setProfile(next), []);
  return { profile, error, setProfile: replace };
}
