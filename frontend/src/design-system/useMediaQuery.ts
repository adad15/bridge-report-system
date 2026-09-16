import { useEffect, useState } from "react";

/**
 * 订阅一条媒体查询。
 *
 * antd 的 Grid.useBreakpoint 只有固定的几个断点；外壳里校对工作台切换滚动方式用的是
 * 1100px，和它页面自己的样式断点对齐，所以单独订阅。
 */
export function useMediaQuery(query: string): boolean {
  const [matches, setMatches] = useState(() => window.matchMedia(query).matches);

  useEffect(() => {
    const list = window.matchMedia(query);
    const update = () => setMatches(list.matches);
    update();
    list.addEventListener("change", update);
    return () => list.removeEventListener("change", update);
  }, [query]);

  return matches;
}
