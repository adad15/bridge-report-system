import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { useState } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { createLazyFeature } from "./createLazyFeature";

afterEach(() => vi.restoreAllMocks());

describe("feature loading boundaries", () => {
  it("shares a pending load and a successful module across mounts", async () => {
    let finish!: (value: { default: () => JSX.Element }) => void;
    const loader = vi.fn(() => new Promise<{ default: () => JSX.Element }>((resolve) => { finish = resolve; }));
    const Feature = createLazyFeature(loader, { label: "内容" });
    const view = render(<><Feature /><Feature /></>);
    expect(screen.getAllByRole("status")).toHaveLength(2);
    await waitFor(() => expect(loader).toHaveBeenCalledTimes(1));
    await act(async () => finish({ default: () => <p>已加载</p> }));
    expect(await screen.findAllByText("已加载")).toHaveLength(2);
    view.unmount();
    render(<Feature />);
    await screen.findByText("已加载");
    expect(loader).toHaveBeenCalledTimes(1);
  });

  it("replaces a rejected lazy attempt when retry is requested", async () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    const loader = vi.fn()
      .mockRejectedValueOnce(new Error("temporary network failure"))
      .mockResolvedValueOnce({ default: () => <p>恢复成功</p> });
    const Feature = createLazyFeature(loader, { label: "评分趋势图", placeholder: () => <p>2026 年：85 分</p> });
    render(<Feature />);
    await screen.findByText("评分趋势图暂时无法加载");
    expect(screen.getByText("2026 年：85 分")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: /重\s*试/ }));
    await screen.findByText("恢复成功");
    expect(loader).toHaveBeenCalledTimes(2);
  });

  it("does not reopen a cancelled panel when its module arrives late", async () => {
    let finish!: (value: { default: () => JSX.Element }) => void;
    const Feature = createLazyFeature<{ onClose: () => void }>(
      () => new Promise((resolve) => { finish = resolve; }),
      { label: "面板", cancel: (props) => props.onClose() },
    );
    function Host() {
      const [open, setOpen] = useState(true);
      return <>{open ? <Feature onClose={() => setOpen(false)} /> : <p>已取消</p>}</>;
    }
    render(<Host />);
    await waitFor(() => expect(finish).toBeDefined());
    fireEvent.click(screen.getByRole("button", { name: "取消打开" }));
    await act(async () => finish({ default: () => <p>不应出现的面板</p> }));
    expect(screen.getByText("已取消")).toBeInTheDocument();
    expect(screen.queryByText("不应出现的面板")).not.toBeInTheDocument();
  });

  it("keeps loaded child state when its parent or props change", async () => {
    function Editor({ title }: { title: string }) {
      const [draft, setDraft] = useState("");
      return <label>{title}<input aria-label="草稿" value={draft} onChange={(event) => setDraft(event.target.value)} /></label>;
    }
    const Feature = createLazyFeature(async () => ({ default: Editor }), { label: "编辑器" });
    const view = render(<Feature title="第一年" />);
    fireEvent.change(await screen.findByRole("textbox"), { target: { value: "未保存内容" } });
    view.rerender(<Feature title="第二年" />);
    expect(screen.getByRole("textbox")).toHaveValue("未保存内容");
  });

  it("isolates rendering errors and does not pretend they are resource failures", async () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    const Feature = createLazyFeature(async () => ({ default: () => { throw new Error("render failed"); } }), { label: "详情" });
    render(<><p>其他区域仍然可用</p><Feature /></>);
    await screen.findByText("详情显示异常");
    expect(screen.getByText("其他区域仍然可用")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "重新加载页面" })).not.toBeInTheDocument();
  });

  it("never automatically reloads on repeated failure and asks before a manual reload", async () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    const confirm = vi.spyOn(window, "confirm").mockReturnValue(false);
    const Feature = createLazyFeature(async () => { throw new Error("old chunk gone"); }, { label: "页面" });
    render(<Feature />);
    await screen.findByText("页面暂时无法加载");
    fireEvent.click(screen.getByRole("button", { name: /重\s*试/ }));
    await screen.findByText("页面暂时无法加载");
    expect(confirm).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole("button", { name: "重新加载页面" }));
    expect(confirm).toHaveBeenCalledWith(expect.stringContaining("未保存"));
  });
});
