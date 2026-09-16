import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { BridgeProfile } from "../api/bridgeProfileApi";
import { BridgeProfileCard } from "./BridgeProfileCard";

const { saveBridgeProfile } = vi.hoisted(() => ({ saveBridgeProfile: vi.fn() }));

vi.mock("../api/bridgeProfileApi", async () => {
  const actual = await vi.importActual<typeof import("../api/bridgeProfileApi")>(
    "../api/bridgeProfileApi"
  );
  return { ...actual, saveBridgeProfile };
});

/** 档案里一项都没录的一座桥。 */
function emptyProfile(): BridgeProfile {
  return {
    bridge_id: "bridge-1",
    bridge_name: "百股大桥",
    business_code: null,
    route_number: null,
    route_name: null,
    administrative_region: null,
    station_mark: null,
    longitude: null,
    latitude: null,
    bridge_type: null,
    bridge_scale: null,
    span_combination: null,
    bridge_length_m: null,
    bridge_width_m: null,
    built_year: null,
    skew_angle_deg: null,
    carriageway_width_m: null,
    sidewalk_width_m: null,
    deck_pavement: null,
    expansion_joint_type: null,
    expansion_joint_piers: null,
    bearing_type: null,
    superstructure_form: null,
    girders_per_span: null,
    girder_height_m: null,
    abutment_form: null,
    pier_form: null,
    foundation_form: null,
    design_load: null,
    design_org: null,
    construction_org: null,
    maintenance_org: null,
    supervision_org: null,
  };
}

function profile(overrides: Partial<BridgeProfile>): BridgeProfile {
  return { ...emptyProfile(), ...overrides };
}

function renderCard(value: BridgeProfile | null, canEdit = false, onSaved = vi.fn()) {
  return render(
    <BridgeProfileCard profile={value} error={null} canEdit={canEdit} onSaved={onSaved} />
  );
}

describe("BridgeProfileCard", () => {
  beforeEach(() => {
    saveBridgeProfile.mockReset();
  });

  // 报告的规矩：没依据就不输出。界面照同一条规矩，免得录入的人以为「—」也是一种值。
  it("shows only the fields that are actually recorded", () => {
    renderCard(profile({ station_mark: "K109+747", built_year: 2002, bridge_length_m: 664.6 }));

    expect(screen.getByText("K109+747")).toBeInTheDocument();
    expect(screen.getByText("664.6m")).toBeInTheDocument();
    expect(screen.queryByText("设计单位")).not.toBeInTheDocument();
    expect(screen.queryByText("未知")).not.toBeInTheDocument();
    expect(screen.getByText("已录 3 / 31 项")).toBeInTheDocument();
  });

  it("says where the data goes when nothing has been recorded yet", () => {
    renderCard(emptyProfile());

    expect(screen.getByText(/报告第 1.1 节/)).toBeInTheDocument();
  });

  it("offers no edit button to a normal user", () => {
    renderCard(profile({ station_mark: "K109+747" }));

    expect(screen.queryByRole("button", { name: /^编\s?辑$/ })).not.toBeInTheDocument();
  });

  // 写是整体覆盖：清空一项必须真的传空串过去，否则那项永远删不掉。
  it("submits the whole record, so clearing a field actually clears it", async () => {
    const saved = profile({ station_mark: "K109+747" });
    saveBridgeProfile.mockResolvedValue(saved);
    const onSaved = vi.fn();

    renderCard(profile({ station_mark: "K109+747", design_org: "某某设计院" }), true, onSaved);
    await userEvent.click(screen.getByRole("button", { name: /^编\s?辑$/ }));
    await userEvent.clear(screen.getByLabelText("设计单位"));
    await userEvent.click(screen.getByRole("button", { name: "保存档案" }));

    await waitFor(() => expect(saveBridgeProfile).toHaveBeenCalledTimes(1));
    const [, bridgeId, input] = saveBridgeProfile.mock.calls[0];
    expect(bridgeId).toBe("bridge-1");
    expect(input.design_org).toBe("");
    expect(input.station_mark).toBe("K109+747");
    // 没碰过的项也要原样带上，不能因为没改就漏传。经纬度就是这样被漏掉过一次的。
    expect(Object.keys(input)).toHaveLength(31);
    // 保存后的档案交回页面，地理位置卡片读的是同一份，跟着一起更新。
    await waitFor(() => expect(onSaved).toHaveBeenCalledWith(saved));
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
  });

  // 库里是 numeric(10,7)，填个四位数过去后端只会报「数字字段溢出」，
  // 用户看不出是哪一栏。在提交前挡住，连请求都不发。
  // 错误挂在出问题的那一栏下面，用户不必猜是哪个框。
  it("stops an out-of-range coordinate before it reaches the server", async () => {
    renderCard(profile({ station_mark: "K109+747" }), true);
    await userEvent.click(screen.getByRole("button", { name: /^编\s?辑$/ }));
    await userEvent.type(screen.getByLabelText("经度"), "1210812");
    await userEvent.type(screen.getByLabelText("纬度"), "410967");
    await userEvent.click(screen.getByRole("button", { name: "保存档案" }));

    expect(await screen.findByText("经度要在 -180 到 180 之间，单位是度。")).toBeInTheDocument();
    expect(screen.getByText("纬度要在 -90 到 90 之间，单位是度。")).toBeInTheDocument();
    expect(saveBridgeProfile).not.toHaveBeenCalled();
  });

  it("refuses to save just one half of a coordinate pair", async () => {
    renderCard(profile({ station_mark: "K109+747" }), true);
    await userEvent.click(screen.getByRole("button", { name: /^编\s?辑$/ }));
    await userEvent.type(screen.getByLabelText("经度"), "121.1352");
    await userEvent.click(screen.getByRole("button", { name: "保存档案" }));

    expect(await screen.findByText("经度和纬度要一起填，只填一个定不了位。")).toBeInTheDocument();
    expect(screen.getByLabelText("纬度")).toHaveAttribute("aria-invalid", "true");
    expect(saveBridgeProfile).not.toHaveBeenCalled();
  });

  it("reports a rejected measure instead of pretending the save worked", async () => {
    saveBridgeProfile.mockRejectedValue(new Error("boom"));

    renderCard(profile({ station_mark: "K109+747" }), true);
    await userEvent.click(screen.getByRole("button", { name: /^编\s?辑$/ }));
    await userEvent.click(screen.getByRole("button", { name: "保存档案" }));

    expect(await screen.findByRole("alert")).toBeInTheDocument();
    expect(screen.getByRole("dialog")).toBeInTheDocument();
  });
});
