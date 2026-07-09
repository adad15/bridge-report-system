// 后端 API 基地址：统一从这里读取，避免每个页面各自拼一份同样的 fallback 逻辑。
export const backendBaseUrl =
  import.meta.env.VITE_BACKEND_BASE_URL ?? "http://127.0.0.1:18080";
