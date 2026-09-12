import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { BridgeProfile } from "../api/bridgeProfileApi";
import { BridgeProfileCard } from "./BridgeProfileCard";

const { fetchBridgeProfile, saveBridgeProfile } = vi.hoisted(() => ({
  fetchBridgeProfile: vi.fn(),
  saveBridgeProfile: vi.fn(),
}));

vi.mock("../api/bridgeProfileApi", async () => {
  const actual = await vi.importActual<typeof import("../api/bridgeProfileApi")>(
    "../api/bridgeProfileApi"
  );
  return { ...actual, fetchBridgeProfile, saveBridgeProfile };
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

describe("BridgeProfileCard", () => {
  beforeEach(() => {
    fetchBridgeProfile.mockReset();
    saveBridgeProfile.mockReset();
  });

  // 报告的规矩：没依据就不输出。界面照同一条规矩，免得录入的人以为「—」也是一种值。
  it("shows only the fields that are actually recorded", async () => {
    fetchBridgeProfile.mockResolvedValue(
      profile({ station_mark: "K109+747", built_year: 2002, bridge_length_m: 664.6 })
    );

    render(<BridgeProfileCard bridgeId="bridge-1" canEdit={false} />);

    expect(await screen.findByText("K109+747")).toBeInTheDocument();
    expect(screen.getByText("664.6m")).toBeInTheDocument();
    expect(screen.queryByText("设计单位")).not.toBeInTheDocument();
    expect(screen.queryByText("未知")).not.toBeInTheDocument();
    expect(screen.getByText("已录 3 / 29 项")).toBeInTheDocument();
  });

  it("says where the data goes when nothing has been recorded yet", async () => {
    fetchBridgeProfile.mockResolvedValue(emptyProfile());

    render(<BridgeProfileCard bridgeId="bridge-1" canEdit={false} />);

    expect(await screen.findByText(/报告第 1.1 节/)).toBeInTheDocument();
  });

  it("offers no edit button to a normal user", async () => {
    fetchBridgeProfile.mockResolvedValue(profile({ station_mark: "K109+747" }));

    render(<BridgeProfileCard bridgeId="bridge-1" canEdit={false} />);

    await screen.findByText("K109+747");
    expect(screen.queryByRole("button", { name: "编辑" })).not.toBeInTheDocument();
  });

  // 写是整体覆盖：清空一项必须真的传空串过去，否则那项永远删不掉。
  it("submits the whole record, so clearing a field actually clears it", async () => {
    fetchBridgeProfile.mockResolvedValue(
      profile({ station_mark: "K109+747", design_org: "某某设计院" })
    );
    saveBridgeProfile.mockResolvedValue(profile({ station_mark: "K109+747" }));

    render(<BridgeProfileCard bridgeId="bridge-1" canEdit />);
    await userEvent.click(await screen.findByRole("button", { name: "编辑" }));
    await userEvent.clear(screen.getByLabelText("设计单位"));
    await userEvent.click(screen.getByRole("button", { name: "保存档案" }));

    await waitFor(() => expect(saveBridgeProfile).toHaveBeenCalledTimes(1));
    const [, bridgeId, input] = saveBridgeProfile.mock.calls[0];
    expect(bridgeId).toBe("bridge-1");
    expect(input.design_org).toBe("");
    expect(input.station_mark).toBe("K109+747");
    // 没碰过的项也要原样带上，不能因为没改就漏传。
    expect(Object.keys(input)).toHaveLength(29);
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
    expect(screen.queryByText("某某设计院")).not.toBeInTheDocument();
  });

  it("reports a rejected measure instead of pretending the save worked", async () => {
    fetchBridgeProfile.mockResolvedValue(profile({ station_mark: "K109+747" }));
    saveBridgeProfile.mockRejectedValue(new Error("boom"));

    render(<BridgeProfileCard bridgeId="bridge-1" canEdit />);
    await userEvent.click(await screen.findByRole("button", { name: "编辑" }));
    await userEvent.click(screen.getByRole("button", { name: "保存档案" }));

    expect(await screen.findByRole("alert")).toBeInTheDocument();
    expect(screen.getByRole("dialog")).toBeInTheDocument();
  });
});
