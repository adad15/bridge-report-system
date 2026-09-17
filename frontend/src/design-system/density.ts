import { useMediaQuery } from "./useMediaQuery";

/**
 * 紧凑档的触发条件。
 *
 * 本机 32 寸 2560×1440 上宽松档正合适；笔记本才是拥挤的那一头——1920×1080 的屏幕在
 * Windows 默认 125%/150% 缩放下，页面实际只有 1536×864 或 1280×720。高度比宽度更要紧：
 * 校对工作台、构件台账、评定树都是"一屏放下"的页面，少 200px 高就要开始滚动。
 */
const COMPACT_QUERY = "(max-width: 1600px), (max-height: 900px)";

export function useCompactDensity(): boolean {
  return useMediaQuery(COMPACT_QUERY);
}

/**
 * 外壳的结构尺寸。
 *
 * 顶栏高度、侧栏宽度、内容区留白三者是联动的：页面里那些"吃满一屏"的高度公式都要
 * 减掉同一组数值，所以集中在这里给，不再各处抄一遍常数。
 */
export interface ShellMetrics {
  compact: boolean;
  headerHeight: number;
  siderWidth: number;
  siderCollapsedWidth: number;
  /** 内容区上下留白，[上, 下]。 */
  contentPaddingBlock: [number, number];
  /** 内容区左右留白，直接写进 style 的 padding。 */
  contentPaddingInline: string;
  /** 顶栏 + 内容区上下留白之和：页面高度公式统一减它。 */
  pageOffset: number;
}

export function shellMetrics(compact: boolean): ShellMetrics {
  const contentPaddingBlock: [number, number] = compact ? [16, 24] : [30, 44];
  const headerHeight = compact ? 52 : 68;
  return {
    compact,
    headerHeight,
    siderWidth: compact ? 208 : 248,
    siderCollapsedWidth: compact ? 64 : 76,
    contentPaddingBlock,
    contentPaddingInline: compact ? "clamp(16px, 2vw, 28px)" : "clamp(24px, 3vw, 52px)",
    pageOffset: headerHeight + contentPaddingBlock[0] + contentPaddingBlock[1],
  };
}

export function useShellMetrics(): ShellMetrics {
  return shellMetrics(useCompactDensity());
}
