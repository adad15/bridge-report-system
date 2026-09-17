import { describe, expect, it } from "vitest";

import { shellMetrics } from "./density";

describe("shellMetrics", () => {
  // 笔记本屏幕是这一档的目标：1920×1080 在 125% 缩放下只有 1536×864，
  // 顶栏和上下留白每少一点，"一屏放下"的页面就多出一点内容区。
  it("gives the compact shell less chrome than the roomy one", () => {
    const roomy = shellMetrics(false);
    const compact = shellMetrics(true);

    expect(compact.headerHeight).toBeLessThan(roomy.headerHeight);
    expect(compact.siderWidth).toBeLessThan(roomy.siderWidth);
    expect(compact.contentPaddingBlock[0]).toBeLessThan(roomy.contentPaddingBlock[0]);
    expect(compact.pageOffset).toBeLessThan(roomy.pageOffset);
  });

  // 页面里那些 calc(100dvh - N) 都减这个数；它必须等于顶栏加上下留白，
  // 否则一屏放下的页面要么留白、要么被截掉一截。
  it("adds the header and the content padding into one page offset", () => {
    for (const compact of [false, true]) {
      const metrics = shellMetrics(compact);
      expect(metrics.pageOffset).toBe(
        metrics.headerHeight + metrics.contentPaddingBlock[0] + metrics.contentPaddingBlock[1],
      );
    }
  });
});
