import { fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { BridgeMedia } from "../api/bridgeMediaApi";
import { BridgeMediaDialog } from "./BridgeMediaDialog";

const { uploadBridgeMedia, deleteBridgeMedia } = vi.hoisted(() => ({
  uploadBridgeMedia: vi.fn(),
  deleteBridgeMedia: vi.fn(),
}));

vi.mock("../api/bridgeMediaApi", async () => {
  const actual = await vi.importActual<typeof import("../api/bridgeMediaApi")>("../api/bridgeMediaApi");
  return { ...actual, uploadBridgeMedia, deleteBridgeMedia };
});

function item(slot: BridgeMedia["slot"], overrides: Partial<BridgeMedia> = {}): BridgeMedia {
  return {
    id: `media-${slot}`,
    bridge_id: "bridge-1",
    slot,
    slot_label: "",
    original_file_name: `${slot}.png`,
    file_extension: ".png",
    file_size_bytes: 1024,
    source: "人工上传",
    created_at: "2026-09-13 00:00:00",
    updated_at: "2026-09-13 00:00:00",
    ...overrides,
  };
}

function renderDialog(media: BridgeMedia[], canEdit: boolean) {
  const props = { onReplaced: vi.fn(), onRemoved: vi.fn(), onClose: vi.fn() };
  render(
    <BridgeMediaDialog bridgeId="bridge-1" bridgeName="百股大桥" media={media} canEdit={canEdit} {...props} />
  );
  return props;
}

function names(group: string): string[] {
  return within(screen.getByRole("region", { name: group }))
    .getAllByRole("listitem")
    .map((entry) => entry.getAttribute("aria-label") ?? "");
}

describe("BridgeMediaDialog", () => {
  beforeEach(() => {
    uploadBridgeMedia.mockReset();
    deleteBridgeMedia.mockReset();
  });

  // 两行和报告里的两条编号对应：图和示意图共用图号，照片另编。
  it("puts figures and photos in two rows, each in report order", () => {
    renderDialog([], false);

    expect(names("图")).toEqual(["地理位置图", "桥型布置图", "横断面图"]);
    expect(names("照片")).toEqual(["桥梁全貌", "桥面", "桥下"]);
  });

  it("counts how many slots have an image", () => {
    renderDialog([item("LOCATION_MAP"), item("DECK_PHOTO")], false);

    expect(screen.getByText("2 / 6")).toBeInTheDocument();
    expect(screen.getByRole("img", { name: "地理位置图" })).toBeInTheDocument();
  });

  // 界面上只留图和图名，不写说明文字。
  it("shows no explanatory text", () => {
    renderDialog([item("LOCATION_MAP", { source: "按坐标生成" })], true);

    const dialog = screen.getByRole("dialog");
    expect(dialog).not.toHaveTextContent("自动生成");
    expect(dialog).not.toHaveTextContent("未上传");
    expect(dialog).not.toHaveTextContent("图 1-1");
  });

  it("is read-only for a normal user", () => {
    renderDialog([item("DECK_PHOTO")], false);

    expect(screen.queryByRole("button", { name: /^添加/ })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /^替\s?换$/ })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /^删\s?除$/ })).not.toBeInTheDocument();
  });

  it("uploads into the chosen slot and hands the result back", async () => {
    const saved = item("CROSS_SECTION");
    uploadBridgeMedia.mockResolvedValue(saved);
    const props = renderDialog([], true);

    const file = new File(["png"], "section.png", { type: "image/png" });
    await userEvent.upload(screen.getByLabelText("上传横断面图"), file);

    await waitFor(() => expect(uploadBridgeMedia).toHaveBeenCalledTimes(1));
    const [, bridgeId, slot, sent] = uploadBridgeMedia.mock.calls[0];
    expect(bridgeId).toBe("bridge-1");
    expect(slot).toBe("CROSS_SECTION");
    expect(sent).toBe(file);
    expect(props.onReplaced).toHaveBeenCalledWith(saved);
  });

  // 拖到哪一格就传到哪一格。
  it("uploads a file dropped onto a slot", async () => {
    uploadBridgeMedia.mockResolvedValue(item("DECK_PHOTO"));
    renderDialog([], true);

    const file = new File(["png"], "deck.png", { type: "image/png" });
    fireEvent.drop(screen.getByRole("button", { name: "添加桥面照片" }), { dataTransfer: { files: [file] } });

    await waitFor(() => expect(uploadBridgeMedia).toHaveBeenCalledTimes(1));
    expect(uploadBridgeMedia.mock.calls[0][2]).toBe("DECK_PHOTO");
  });

  it("asks before deleting and does nothing if the user backs out", async () => {
    renderDialog([item("DECK_PHOTO")], true);

    await userEvent.click(screen.getByRole("button", { name: /^删\s?除$/ }));
    const confirm = await screen.findByRole("tooltip");
    expect(confirm).toHaveTextContent("删除「桥面照片」？");
    await userEvent.click(within(confirm).getByRole("button", { name: /^取\s?消$/ }));

    expect(deleteBridgeMedia).not.toHaveBeenCalled();
  });

  it("removes the slot once confirmed", async () => {
    deleteBridgeMedia.mockResolvedValue(undefined);
    const props = renderDialog([item("DECK_PHOTO")], true);

    await userEvent.click(screen.getByRole("button", { name: /^删\s?除$/ }));
    const confirm = await screen.findByRole("tooltip");
    await userEvent.click(within(confirm).getByRole("button", { name: /^删\s?除$/ }));

    await waitFor(() => expect(props.onRemoved).toHaveBeenCalledWith("DECK_PHOTO"));
  });
});
