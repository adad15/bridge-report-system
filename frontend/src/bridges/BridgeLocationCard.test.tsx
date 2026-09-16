import { render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { BridgeProfile } from "../api/bridgeProfileApi";
import { BridgeLocationCard } from "./BridgeLocationCard";

const { loadAMap } = vi.hoisted(() => ({ loadAMap: vi.fn() }));

vi.mock("./amapLoader", () => ({ loadAMap }));

/** 假的高德命名空间：只要够这张卡片用。 */
function fakeAMap() {
  const map = {
    add: vi.fn(),
    destroy: vi.fn(),
    setCenter: vi.fn(),
    setZoom: vi.fn(),
    // 高德地图 2.0 的缩放可以是小数，这里故意给一个。
    getCenter: vi.fn(() => ({ lng: 121.2019, lat: 41.1137 })),
    getZoom: vi.fn(() => 15.4),
  };
  const options: Record<string, unknown>[] = [];
  const markers: Record<string, unknown>[] = [];
  return {
    map,
    options,
    markers,
    namespace: {
      Map: vi.fn((_container: HTMLElement, opts: Record<string, unknown>) => {
        options.push(opts);
        return map;
      }),
      Marker: vi.fn((opts: Record<string, unknown>) => {
        markers.push(opts);
        return opts;
      }),
    },
  };
}

function profile(overrides: Partial<BridgeProfile>): BridgeProfile {
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
    ...overrides,
  };
}

/** 百股大桥桥位，WGS-84。 */
const BAIGU = { longitude: 121.1352, latitude: 41.0967 };

describe("BridgeLocationCard", () => {
  beforeEach(() => {
    loadAMap.mockReset();
  });

  it("tells the user where to record coordinates instead of showing an empty map", () => {
    render(<BridgeLocationCard profile={profile({})} />);

    expect(screen.getByText(/还没有经纬度/)).toBeInTheDocument();
    expect(loadAMap).not.toHaveBeenCalled();
  });

  // 库里存 WGS-84，高德底图是 GCJ-02。不转这一下，图钉会偏出一两百米。
  it("centres the map on the GCJ-02 position, not the stored one", async () => {
    const amap = fakeAMap();
    loadAMap.mockResolvedValue(amap.namespace);

    render(<BridgeLocationCard profile={profile(BAIGU)} />);

    await waitFor(() => expect(amap.options).toHaveLength(1));
    const [lng, lat] = amap.options[0].center as [number, number];
    expect(Math.abs(lng - BAIGU.longitude)).toBeGreaterThan(0.001);
    expect(Math.abs(lng - BAIGU.longitude)).toBeLessThan(0.02);
    expect(Math.abs(lat - BAIGU.latitude)).toBeLessThan(0.02);
    expect(amap.markers[0].title).toBe("百股大桥");
  });

  // 本地部署的机器可能根本没有外网，这时要说明原因，不能把卡片卡在空白上。
  it("explains itself when the map service is unreachable", async () => {
    loadAMap.mockImplementation(() => Promise.reject(new Error("map_script_unreachable")));

    render(<BridgeLocationCard profile={profile(BAIGU)} />);

    expect(await screen.findByText(/地图服务连不上/)).toBeInTheDocument();
  });

  it("says so when no map key is configured", async () => {
    loadAMap.mockImplementation(() => Promise.reject(new Error("map_key_not_configured")));

    render(<BridgeLocationCard profile={profile(BAIGU)} />);

    expect(await screen.findByText(/未配置地图服务/)).toBeInTheDocument();
  });

  // key 被拒和机器没网是两回事，提示要把人指向 key 而不是网络。
  it("points at the key when the map service rejects it", async () => {
    loadAMap.mockImplementation(() => Promise.reject(new Error("map_key_rejected")));

    render(<BridgeLocationCard profile={profile(BAIGU)} />);

    expect(await screen.findByText(/拒绝了这个 key/)).toBeInTheDocument();
  });

  // 经纬度填反了就不画，让人回去改，而不是把图钉甩到太平洋上。
  it("refuses to plot coordinates that are out of range", () => {
    render(<BridgeLocationCard profile={profile({ longitude: 41.0967, latitude: 121.1352 })} />);

    expect(screen.getByText(/还没有经纬度/)).toBeInTheDocument();
    expect(loadAMap).not.toHaveBeenCalled();
  });

  it("shows the route and station in the header when the archive has them", () => {
    render(<BridgeLocationCard profile={profile({ route_number: "S320", station_mark: "K109+747" })} />);

    expect(screen.getByText("S320 · K109+747")).toBeInTheDocument();
  });
  // 地图出不来时，已经存下的那张图就是这张卡片能给的最好的东西。
  it("falls back to the stored location map when the live map is unavailable", async () => {
    loadAMap.mockImplementation(() => Promise.reject(new Error("map_script_unreachable")));
    const stored = {
      id: "media-1", bridge_id: "bridge-1", slot: "LOCATION_MAP" as const, slot_label: "地理位置图",
      original_file_name: "x", file_extension: ".png", file_size_bytes: 1, source: "按坐标生成" as const,
      created_at: "t", updated_at: "t",
    };

    render(<BridgeLocationCard profile={profile(BAIGU)} locationMap={stored} />);

    expect(await screen.findByRole("img", { name: "已存的地理位置图" })).toBeInTheDocument();
    expect(screen.getByText(/这里显示的是已经存下的地理位置图/)).toBeInTheDocument();
  });
});
