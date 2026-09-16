import "@testing-library/jest-dom/vitest";
import { cleanup, configure } from "@testing-library/react";
import { afterEach } from "vitest";

// Ant Design 的响应式 Form/Grid 会订阅 matchMedia；jsdom 默认没有实现它。
Object.defineProperty(window, "matchMedia", {
  writable: true,
  value: (query: string): MediaQueryList => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: () => undefined,
    removeListener: () => undefined,
    addEventListener: () => undefined,
    removeEventListener: () => undefined,
    dispatchEvent: () => false,
  }),
});

// Image、Typography 省略号等会订阅元素尺寸变化；jsdom 同样没有 ResizeObserver。
class ResizeObserverStub {
  observe() {}
  unobserve() {}
  disconnect() {}
}
if (!("ResizeObserver" in window)) {
  Object.defineProperty(window, "ResizeObserver", { writable: true, value: ResizeObserverStub });
}

// Modal 打开时量滚动条宽度会带上伪元素参数，jsdom 不支持就刷一屏 Not implemented。
const computedStyle = window.getComputedStyle.bind(window);
window.getComputedStyle = (element: Element) => computedStyle(element);

// findBy / waitFor 默认只等 1 秒；整套并行跑、antd 组件又重时，异步渲染偶尔会慢过这个数。
configure({ asyncUtilTimeout: 4000 });

afterEach(() => cleanup());
