import { render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter, useLocation } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createStandardDocument, deleteStandardVersion, fetchStandardDocuments } from "../api/standardDocumentsApi";
import { chooseOption, emulateViewport } from "../test/antd";
import { allStandardDocuments, h21Document, standardVersion } from "../test/standardDocumentFixtures";
import { StandardDocumentsPage } from "./StandardDocumentsPage";

const authState = vi.hoisted(() => ({ role: "admin" }));

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({ user: { username: "tester", display_name: "测试用户", role: authState.role } }),
}));

vi.mock("../api/standardDocumentsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardDocumentsApi")>();
  return {
    ...original,
    fetchStandardDocuments: vi.fn(),
    deleteStandardVersion: vi.fn(),
    createStandardDocument: vi.fn(),
  };
});

function LocationProbe() {
  const location = useLocation();
  return <output aria-label="当前地址">{location.pathname + location.search}</output>;
}

/** jsdom 没有 Fullscreen API：按浏览器的行为补一个，进出全屏都发 fullscreenchange。 */
function stubFullscreen() {
  let element: Element | null = null;
  const notify = () => document.dispatchEvent(new Event("fullscreenchange"));
  const requestFullscreen = vi.fn(function (this: Element) {
    element = this;
    notify();
    return Promise.resolve();
  });
  const exitFullscreen = vi.fn(() => {
    element = null;
    notify();
    return Promise.resolve();
  });
  const stubs: [object, string, PropertyDescriptor][] = [
    [document, "fullscreenEnabled", { configurable: true, get: () => true }],
    [document, "fullscreenElement", { configurable: true, get: () => element }],
    [document, "exitFullscreen", { configurable: true, writable: true, value: exitFullscreen }],
    [HTMLElement.prototype, "requestFullscreen", { configurable: true, writable: true, value: requestFullscreen }],
  ];
  const saved = stubs.map(([target, key]) => [target, key, Object.getOwnPropertyDescriptor(target, key)] as const);
  for (const [target, key, descriptor] of stubs) Object.defineProperty(target, key, descriptor);
  return {
    requestFullscreen,
    exitFullscreen,
    current: () => element,
    restore() {
      for (const [target, key, descriptor] of saved) {
        if (descriptor) Object.defineProperty(target, key, descriptor);
        else delete (target as Record<string, unknown>)[key];
      }
    },
  };
}

function renderPage(path = "/standards") {
  return render(
    <MemoryRouter initialEntries={[path]}>
      <StandardDocumentsPage />
      <LocationProbe />
    </MemoryRouter>,
  );
}

describe("StandardDocumentsPage", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    authState.role = "admin";
    emulateViewport(1920);
    vi.mocked(fetchStandardDocuments).mockResolvedValue(allStandardDocuments);
  });

  it("groups standards by category and opens the current version by default", async () => {
    renderPage();

    const reader = await screen.findByTitle("规范原文：JTG/T H21—2011 公路桥梁技术状况评定标准");
    expect(reader).toHaveAttribute("src", expect.stringContaining("/api/standard-document-versions/h21-2011/content"));
    expect(screen.getByRole("group", { name: "技术状况评定" })).toBeInTheDocument();
    expect(screen.getByRole("group", { name: "养护与检查" })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "公路桥梁技术状况评定标准" })).toBeInTheDocument();
  });

  it("switches to a draft, warns about it and goes back to the current version", async () => {
    renderPage();
    await chooseOption(await screen.findByRole("combobox", { name: "版本" }), /2026 报批稿/);

    expect(await screen.findByText(/2026 报批稿尚未正式发布/)).toBeInTheDocument();
    expect(screen.getByTitle(/规范原文：JTG\/T 52XX—2026/)).toBeInTheDocument();
    expect(screen.getByLabelText("当前地址")).toHaveTextContent("version=h21-2026");

    await userEvent.click(screen.getByRole("button", { name: "查看现行版本" }));
    expect(await screen.findByTitle(/规范原文：JTG\/T H21—2011/)).toBeInTheDocument();
  });

  it("opens the standard, version and tab named in the address bar", async () => {
    renderPage("/standards?doc=maintenance&version=h11&tab=info");

    expect(await screen.findByText(/JTG H11—2004 已于 2021-11-01 废止，由 JTG 5120—2021 替代/)).toBeInTheDocument();
    expect(screen.getByRole("tab", { name: /基本信息/, selected: true })).toBeInTheDocument();
    expect(screen.getByText("本地上传")).toBeInTheDocument();
  });

  it("drops address-bar parameters that point nowhere", async () => {
    renderPage("/standards?doc=missing&version=nothing");

    expect(await screen.findByTitle(/规范原文：JTG\/T H21—2011/)).toBeInTheDocument();
    await waitFor(() => expect(screen.getByLabelText("当前地址").textContent).toBe("/standards"));
  });

  it("keeps maintenance actions away from normal users", async () => {
    authState.role = "normal";
    renderPage("/standards?tab=versions");

    expect(await screen.findByRole("table")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /新增规范/ })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /上传新版本/ })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "更多" })).not.toBeInTheDocument();
    expect(screen.queryByText("替换原文")).not.toBeInTheDocument();
  });

  // 阅读区再怎么挤也有限，细读时让阅读器铺满整个屏幕；Esc 或「退出全屏」回来。
  it("reads the standard in fullscreen and comes back", async () => {
    const fullscreen = stubFullscreen();
    try {
      renderPage();
      await userEvent.click(await screen.findByRole("button", { name: /全屏阅读/ }));

      expect(fullscreen.requestFullscreen).toHaveBeenCalledTimes(1);
      expect(fullscreen.current()).toContainElement(screen.getByTitle(/规范原文：JTG\/T H21—2011/));
      expect(await screen.findByText("按 Esc 退出")).toBeInTheDocument();

      await userEvent.click(screen.getByRole("button", { name: /退出全屏/ }));
      expect(fullscreen.exitFullscreen).toHaveBeenCalledTimes(1);
      await waitFor(() => expect(screen.queryByText("按 Esc 退出")).not.toBeInTheDocument());

      await userEvent.click(screen.getByRole("tab", { name: /版本记录/ }));
      expect(screen.queryByRole("button", { name: /全屏阅读/ })).not.toBeInTheDocument();
    } finally {
      fullscreen.restore();
    }
  });

  it("opens the upload dialog from the header on any tab", async () => {
    renderPage("/standards?tab=info");
    await userEvent.click(await screen.findByRole("button", { name: /上传新版本/ }));
    expect(await screen.findByText("上传新版本 · 公路桥梁技术状况评定标准")).toBeInTheDocument();
  });

  it("turns adding an existing standard into uploading a version of it", async () => {
    renderPage();
    await userEvent.click(await screen.findByRole("button", { name: /新增规范/ }));
    const dialog = await screen.findByRole("dialog");
    await userEvent.type(within(dialog).getByLabelText("规范名称"), "公路桥涵养护规范");
    await userEvent.type(within(dialog).getByLabelText("版本名称"), "2027 版");
    await userEvent.click(await within(dialog).findByRole("button", { name: "改为上传新版本" }));

    expect(await screen.findByText("上传新版本 · 公路桥涵养护规范")).toBeInTheDocument();
    expect(screen.getByLabelText("版本名称")).toHaveValue("2027 版");
  });

  it("deletes a version after confirmation and refreshes the table", async () => {
    vi.mocked(deleteStandardVersion).mockResolvedValue({
      ...h21Document,
      versions: h21Document.versions.filter((version) => version.id !== "h21-2017"),
    });
    renderPage("/standards?tab=versions");

    const row = (await screen.findByText("2017 征求意见稿")).closest("tr") as HTMLElement;
    await userEvent.click(within(row).getByText("删除"));
    await userEvent.click(await screen.findByRole("button", { name: /^删\s?除$/ }));

    expect(deleteStandardVersion).toHaveBeenCalledWith(expect.any(String), "h21-2017");
    await waitFor(() => expect(screen.queryByText("2017 征求意见稿")).not.toBeInTheDocument());
  });

  it("creates a standard and opens it", async () => {
    const created = {
      id: "j21", name: "公路桥梁承载能力检测评定规程", english_name: null, category: "技术状况评定",
      default_version_id: "j21-draft",
      versions: [standardVersion({ id: "j21-draft", label: "征求意见稿", code: "JTG/T J21—20XX", kind: "draft_comment" as const, status: "draft_comment" as const })],
    };
    vi.mocked(createStandardDocument).mockResolvedValue(created);
    renderPage();

    await userEvent.click(await screen.findByRole("button", { name: /新增规范/ }));
    const dialog = await screen.findByRole("dialog");
    await userEvent.type(within(dialog).getByLabelText("规范名称"), created.name);
    await userEvent.type(within(dialog).getByRole("combobox", { name: "分类" }), "技术状况评定");
    await userEvent.type(within(dialog).getByLabelText("版本名称"), "征求意见稿");
    await userEvent.type(within(dialog).getByLabelText("标准编号"), "JTG/T J21—20XX");
    await chooseOption(within(dialog).getByRole("combobox", { name: "稿件类型" }), "征求意见稿");
    await userEvent.upload(
      document.querySelector<HTMLInputElement>('input[type="file"]') as HTMLInputElement,
      new File(["%PDF-1.4"], "j21.pdf", { type: "application/pdf" }),
    );
    await userEvent.click(within(dialog).getByRole("button", { name: /^上\s?传$/ }));

    expect(await screen.findByRole("heading", { name: created.name })).toBeInTheDocument();
    expect(screen.getByLabelText("当前地址")).toHaveTextContent("doc=j21");
  });
});
