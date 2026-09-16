import react from "@vitejs/plugin-react";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [react()],
  test: {
    environment: "jsdom",
    setupFiles: ["./src/test/setup.ts"],
    // antd 的表格、菜单、弹窗在 jsdom 里渲染得慢，整套并行跑时单个用例常超过默认的 5 秒。
    testTimeout: 20000,
  },
  server: {
    host: "127.0.0.1",
    port: 5173,
  },
});
