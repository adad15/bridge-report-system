import { describe, expect, it } from "vitest";

import {
  formatCoordinates,
  formatDms,
  gcj02ToWgs84,
  outsideChina,
  parseCoordinate,
  validCoordinates,
  wgs84ToGcj02,
} from "./coordinates";

/** 百股大桥桥位，WGS-84。 */
const BAIGU = { lng: 121.1352, lat: 41.0967 };

/** 一度纬度约 111 km；这一带一度经度约 84 km。够把度差换算成米，量级判断用。 */
function metresApart(a: { lng: number; lat: number }, b: { lng: number; lat: number }): number {
  const dx = (a.lng - b.lng) * 111_320 * Math.cos((a.lat * Math.PI) / 180);
  const dy = (a.lat - b.lat) * 110_540;
  return Math.sqrt(dx * dx + dy * dy);
}

describe("coordinates", () => {
  // 这是这套换算存在的理由：不转就偏出这么远，图钉会落在桥外面。
  it("shifts a point inside China by one to a few hundred metres", () => {
    const shifted = wgs84ToGcj02(BAIGU);
    const distance = metresApart(BAIGU, shifted);

    expect(distance).toBeGreaterThan(100);
    expect(distance).toBeLessThan(800);
  });

  // 存 WGS-84、画 GCJ-02、点选再转回来，这条链路走一圈必须回到原地。
  it("round-trips to within a centimetre", () => {
    const back = gcj02ToWgs84(wgs84ToGcj02(BAIGU));

    expect(metresApart(BAIGU, back)).toBeLessThan(0.01);
  });

  it("leaves points outside China alone", () => {
    const tokyo = { lng: 139.767, lat: 35.681 };

    expect(outsideChina(tokyo)).toBe(true);
    expect(wgs84ToGcj02(tokyo)).toEqual(tokyo);
    expect(gcj02ToWgs84(tokyo)).toEqual(tokyo);
  });

  it("treats the bridge's own region as inside China", () => {
    expect(outsideChina(BAIGU)).toBe(false);
  });

  // 经纬度填反是最常见的录入错误，反了之后纬度会超出 ±90。
  it("rejects out-of-range and swapped coordinates", () => {
    expect(validCoordinates(BAIGU)).toBe(true);
    expect(validCoordinates({ lng: BAIGU.lat, lat: BAIGU.lng })).toBe(false);
    expect(validCoordinates({ lng: Number.NaN, lat: 41 })).toBe(false);
  });
});

describe("度分秒", () => {
  // 工程资料上就是这么写的，录入要能直接拿来用。
  it("reads the notation printed on survey documents", () => {
    expect(parseCoordinate("N41°6'55.2\"", "lat")).toBeCloseTo(41.115333, 6);
    expect(parseCoordinate("E121°11'46.7\"", "lng")).toBeCloseTo(121.196306, 6);
  });

  it("still accepts plain decimal degrees", () => {
    expect(parseCoordinate("121.1352", "lng")).toBe(121.1352);
    expect(parseCoordinate("-121.1352", "lng")).toBe(-121.1352);
    expect(parseCoordinate("W121°11'", "lng")).toBeCloseTo(-121.183333, 6);
  });

  // 整串粘进任意一栏都认得，取出属于这一栏的那一半。
  it("picks its own half out of a pasted pair", () => {
    const pasted = "N41°6'55.2\",E121°11'46.7\"";
    expect(parseCoordinate(pasted, "lat")).toBeCloseTo(41.115333, 6);
    expect(parseCoordinate(pasted, "lng")).toBeCloseTo(121.196306, 6);
  });

  it("rejects a value filled into the wrong box", () => {
    expect(parseCoordinate("N41°6'55.2\"", "lng")).toBeNull();
    expect(parseCoordinate("E121°11'46.7\"", "lat")).toBeNull();
  });

  it("rejects minutes or seconds that are not minutes or seconds", () => {
    expect(parseCoordinate("41°70'12\"", "lat")).toBeNull();
    expect(parseCoordinate("没有坐标", "lat")).toBeNull();
  });

  it("writes decimals back in the same notation", () => {
    expect(formatDms(41.1153333, "lat")).toBe("N41°6'55.2\"");
    expect(formatDms(121.1963056, "lng")).toBe("E121°11'46.7\"");
    expect(formatDms(-41.1153333, "lat")).toBe("S41°6'55.2\"");
    expect(formatCoordinates(121.1963056, 41.1153333)).toBe("N41°6'55.2\",E121°11'46.7\"");
  });

  // 录入的写法转成十进制存进库，再显示回来，必须还是同一个写法。
  it("round-trips notation through storage", () => {
    const typed = "N41°6'55.2\"";
    const stored = parseCoordinate(typed, "lat") as number;
    expect(formatDms(stored, "lat")).toBe(typed);
  });

  // 贴着整度的值要进到下一度，不能印成 41°59'60"。
  it("rounds up to the next degree instead of printing sixty seconds", () => {
    expect(formatDms(41.9999999, "lat")).toBe("N42°0'0\"");
    expect(formatDms(41.9999, "lat")).toBe("N41°59'59.64\"");
  });
});
