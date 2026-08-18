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
  // 原生 date 输入只能逐位改数字，翻到几年前要按很久。补的就是这条钻取路径。
  it("drills up from day to month to year and back down to a date", async () => {
    render(<Harness initial="1992-11-06" />);
    let panel = await openPanel();

    // 打开停在已选日期所在的月份，不是今天。
    expect(within(panel).getByRole("button", { name: "1992年11月" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "1992年11月" }));
    expect(within(panel).getByRole("button", { name: "1992年" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "1992年" }));
    // 年份面板以当前年为中心展开 15 格，与截图里的 1985—1999 一致。
    expect(within(panel).getByText("1985年 - 1999年")).toBeInTheDocument();
    expect(within(panel).getByRole("button", { name: "1985年" })).toBeInTheDocument();
    expect(within(panel).getByRole("button", { name: "1999年" })).toBeInTheDocument();

    await userEvent.click(within(panel).getByRole("button", { name: "1990年" }));
    await userEvent.click(within(panel).getByRole("button", { name: "三月" }));
    await userEvent.click(within(panel).getByRole("button", { name: "1990-03-12" }));

    expect(screen.getByTestId("value")).toHaveTextContent("1990-03-12");
    // 选到"日"即落定并收起浮层。
    expect(screen.queryByRole("dialog", { name: "检查日期选择" })).not.toBeInTheDocument();
  });

  it("steps a page at a time per view", async () => {
    render(<Harness initial="1992-11-06" />);
    const panel = await openPanel();

    // 日视图翻一个月
    await userEvent.click(within(panel).getByRole("button", { name: "上一页" }));
    expect(within(panel).getByRole("button", { name: "1992年10月" })).toBeInTheDocument();

    // 月视图翻一年
    await userEvent.click(within(panel).getByRole("button", { name: "1992年10月" }));
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
