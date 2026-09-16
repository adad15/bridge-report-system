// 高德 JS API 的按需加载。
//
// 三条纪律：
//
// * **按需**。只有确实要画地图的页面才发这个请求，没录坐标的桥一个字节都不下载。
// * **只加载一次**。整个会话共用一个 promise，来回切桥不会反复插 script 标签。
// * **失败是正常情况**。本地部署的机器可能根本没有外网，这时 promise 拒绝，
//   调用方换成已经存下的地理位置图，而不是把页面卡在加载态。

import { fetchMapConfig } from "../api/mapApi";
import { backendBaseUrl } from "../config";

/** 拿不到脚本就别一直等，用户看到的应该是降级内容而不是空框。 */
const LOAD_TIMEOUT_MS = 8000;

/** 高德 JS API 的全局对象。只用到地图、标记和视野三样，不引入完整类型包。 */
export interface AMapNamespace {
  Map: new (container: HTMLElement, options: Record<string, unknown>) => AMapInstance;
  Marker: new (options: Record<string, unknown>) => unknown;
  Pixel: new (x: number, y: number) => unknown;
}

export interface AMapInstance {
  setCenter(position: [number, number]): void;
  setZoom(zoom: number): void;
  getCenter(): { lng: number; lat: number };
  getZoom(): number;
  add(overlay: unknown): void;
  destroy(): void;
}

declare global {
  interface Window {
    AMap?: AMapNamespace;
    _AMapSecurityConfig?: { securityJsCode: string };
  }
}

let pending: Promise<AMapNamespace> | null = null;

/**
 * 加载高德 JS API，拿到全局 AMap。
 *
 * 后端说没配 key 时直接拒绝，连脚本都不去请求——那种部署本来就不打算联网。
 */
export function loadAMap(): Promise<AMapNamespace> {
  if (pending) return pending;
  pending = (async () => {
    if (window.AMap) return window.AMap;

    const config = await fetchMapConfig(backendBaseUrl);
    if (!config.available) throw new Error("map_key_not_configured");
    if (config.security_js_code) {
      window._AMapSecurityConfig = { securityJsCode: config.security_js_code };
    }

    await new Promise<void>((resolve, reject) => {
      const script = document.createElement("script");
      script.src = `https://webapi.amap.com/maps?v=2.0&key=${encodeURIComponent(config.js_key)}`;
      script.async = true;
      const timer = window.setTimeout(() => reject(new Error("map_script_timeout")), LOAD_TIMEOUT_MS);
      script.onload = () => {
        window.clearTimeout(timer);
        resolve();
      };
      script.onerror = () => {
        window.clearTimeout(timer);
        reject(new Error("map_script_unreachable"));
      };
      document.head.appendChild(script);
    });

    // 脚本下到了却没有 AMap，意味着高德拒了这个 key（控制台里会看到
    // "Error key!"）。这和机器没网是两回事，要让用户知道该去改 key 而不是查网络。
    if (!window.AMap) throw new Error("map_key_rejected");
    return window.AMap;
  })();

  // 失败之后允许下次重试：机器可能只是暂时没网。
  pending.catch(() => {
    pending = null;
  });
  return pending;
}

/** 测试用：清掉缓存的 promise。 */
export function resetAMapLoaderForTests(): void {
  pending = null;
}
