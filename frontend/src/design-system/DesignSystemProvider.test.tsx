import { render } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { DesignSystemProvider } from "./DesignSystemProvider";

describe("DesignSystemProvider", () => {
  afterEach(() => {
    document.documentElement.removeAttribute("style");
  });

  // 文档根的字体、文字色、底色曾经在 styles.css 里另抄一份，和主题 Token 对不上。
  it("writes the theme's text colour, layout background and font onto the document root", () => {
    render(<DesignSystemProvider><span>内容</span></DesignSystemProvider>);

    const root = document.documentElement.style;
    expect(root.color).toBe("rgb(23, 32, 51)"); // colorText #172033
    expect(root.backgroundColor).toBe("rgb(241, 244, 249)"); // colorBgLayout #F1F4F9
    expect(root.fontFamily).toContain("Microsoft YaHei");
  });
});
