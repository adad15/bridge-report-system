import { useCallback, useEffect, useRef, useState } from "react";

/**
 * 让一个元素进出浏览器全屏（Fullscreen API）。
 *
 * active 跟着 fullscreenchange 走：用户按 Esc 退出时也能及时收起全屏专用的界面。
 * 浏览器不支持或被策略禁用时 supported 为 false，调用方别显示入口。
 */
export function useFullscreen<T extends HTMLElement>() {
  const ref = useRef<T>(null);
  const [active, setActive] = useState(false);

  useEffect(() => {
    const sync = () => setActive(ref.current !== null && document.fullscreenElement === ref.current);
    document.addEventListener("fullscreenchange", sync);
    return () => document.removeEventListener("fullscreenchange", sync);
  }, []);

  const enter = useCallback(() => {
    // 被浏览器拒绝（例如不是用户手势触发）时什么也不做，页面保持原样。
    ref.current?.requestFullscreen().catch(() => undefined);
  }, []);

  const exit = useCallback(() => {
    if (document.fullscreenElement) document.exitFullscreen().catch(() => undefined);
  }, []);

  return { ref, active, supported: document.fullscreenEnabled === true, enter, exit };
}
