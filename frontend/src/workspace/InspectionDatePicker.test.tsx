import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { useState } from "react";
import { describe, expect, it } from "vitest";

import { InspectionDatePicker, isCompleteDate } from "./InspectionDatePicker";

function Harness({ initial = "" }: { initial?: string }) {
  const [value, setValue] = useState(initial);
  return (
    <>
      <label htmlFor="date">检查日期</label>
      <InspectionDatePicker id="date" value={value} onChange={setValue} />
      <output data-testid="value">{value}</output>
    </>
  );
}

const openPanel = async () => {
  await userEvent.click(screen.getByRole("button", { name: "选择检查日期" }));
  return screen.getByRole("dialog", { name: "检查日期选择" });
};

describe("InspectionDatePicker", () => {
  // 标题拆成年、月两段，各自直达对应层级；选完都回日视图。合成一块时改年份要走
  // 四步，最后那步还强迫用户重选一个本来不想动的月份。
  it("jumps straight to the year panel and returns to the day view", async () => {
    render(<Harness initial="1992-11-06" />);
    const panel = await openPanel();

    // 打开停在已选日期所在的月份，标题是两段。
    expect(within(panel).getByRole("button", { name: "1992年" })).toBeInTheDocument();
    expect(within(panel).getByRole("button", { name: "11月" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "1992年" }));
    // 年份面板以当前年为中心展开 15 格，与参考界面的 1985—1999 一致。
    expect(within(panel).getByText("1985年 - 1999年")).toBeInTheDocument();
    expect(within(panel).getByRole("button", { name: "1985年" })).toBeInTheDocument();
    expect(within(panel).getByRole("button", { name: "1999年" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "1990年" }));
    // 直接回日视图，月份原样保留——不必为改年份再选一次月。
    expect(within(panel).getByRole("button", { name: "1990年" })).toBeInTheDocument();
    expect(within(panel).getByRole("button", { name: "11月" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "1990-11-06" }));
    expect(screen.getByTestId("value")).toHaveTextContent("1990-11-06");
    expect(screen.queryByRole("dialog", { name: "检查日期选择" })).not.toBeInTheDocument();
  });

  it("jumps straight to the month panel and returns to the day view", async () => {
    render(<Harness initial="1992-11-06" />);
    const panel = await openPanel();

    await userEvent.click(within(panel).getByRole("button", { name: "11月" }));
    expect(within(panel).getByRole("button", { name: "三月" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "三月" }));
    // 回日视图，年份原样保留。
    expect(within(panel).getByRole("button", { name: "1992年" })).toBeInTheDocument();
    expect(within(panel).getByRole("button", { name: "3月" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "1992-03-12" }));
    expect(screen.getByTestId("value")).toHaveTextContent("1992-03-12");
  });

  it("steps a page at a time per view", async () => {
    render(<Harness initial="1992-11-06" />);
    const panel = await openPanel();

    // 日视图翻一个月
    await userEvent.click(within(panel).getByRole("button", { name: "上一页" }));
    expect(within(panel).getByRole("button", { name: "10月" })).toBeInTheDocument();

    // 月视图翻一年
    await userEvent.click(within(panel).getByRole("button", { name: "10月" }));
    await userEvent.click(within(panel).getByRole("button", { name: "下一页" }));
    expect(within(panel).getByRole("button", { name: "1993年" })).toBeInTheDocument();

    // 年视图翻 15 年
    await userEvent.click(within(panel).getByRole("button", { name: "1993年" }));
    expect(within(panel).getByText("1986年 - 2000年")).toBeInTheDocument();
    await userEvent.click(within(panel).getByRole("button", { name: "上一页" }));
    expect(within(panel).getByText("1971年 - 1985年")).toBeInTheDocument();
  });

  it("keeps the field typeable and clears on demand", async () => {
    render(<Harness />);
    // 报告上的日期常常是照着纸质件敲的，比点日历快——文本框必须能直接打字。
    await userEvent.type(screen.getByLabelText("检查日期"), "2026-05-18");
    expect(screen.getByTestId("value")).toHaveTextContent("2026-05-18");

    const panel = await openPanel();
    await userEvent.click(within(panel).getByRole("button", { name: "清除" }));
    expect(screen.getByTestId("value")).toHaveTextContent("");
  });

  it("closes on escape without changing the value", async () => {
    render(<Harness initial="2026-05-18" />);
    await openPanel();
    await userEvent.keyboard("{Escape}");
    expect(screen.queryByRole("dialog", { name: "检查日期选择" })).not.toBeInTheDocument();
    expect(screen.getByTestId("value")).toHaveTextContent("2026-05-18");
  });

  // 换成文本框之后浏览器不再管格式，半截日期和不存在的日期只能自己挡。
  it("rejects partial and impossible dates", () => {
    expect(isCompleteDate("2026-05-18")).toBe(true);
    expect(isCompleteDate("2024-02-29")).toBe(true);   // 闰年

    expect(isCompleteDate("")).toBe(false);
    expect(isCompleteDate("2026-05")).toBe(false);
    expect(isCompleteDate("2026-5-8")).toBe(false);
    expect(isCompleteDate("2026-13-01")).toBe(false);
    expect(isCompleteDate("2026-02-30")).toBe(false);  // 格式对但日子不存在
    expect(isCompleteDate("2026-02-29")).toBe(false);  // 平年，2026 不是闰年
    expect(isCompleteDate("昨天")).toBe(false);
  });
});
