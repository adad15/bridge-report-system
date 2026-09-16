// WGS-84 与 GCJ-02 之间的换算。
//
// 库里和接口上的经纬度**一律是 WGS-84**：现场 GPS、设计图和测绘资料给的都是这个基准。
// 高德、百度的底图用的是 GCJ-02，两者在辽宁一带差一两百米，正好够把图钉甩到桥外面去。
//
// 所以换算只在这一处做：画图前把 WGS-84 转成 GCJ-02，从地图上取回坐标时转回来。
// 后端不碰这件事，它只存 WGS-84。

const PI = Math.PI;
/** 克拉索夫斯基椭球长半轴，GCJ-02 的偏移算法就是按它定义的。 */
const AXIS = 6378245.0;
/** 同一椭球的第一偏心率平方。 */
const ECCENTRICITY_SQUARED = 0.006_693_421_622_965_943;

export interface LngLat {
  lng: number;
  lat: number;
}

/**
 * 境外不偏移。
 *
 * 这个矩形是业界通用的粗略国界框，宽松到把周边海域也算进来。它不需要精确：落在框内
 * 而实际在境外的点，偏移量也只有几百米，而把境内的点漏判成境外才是真正看得出来的错。
 */
export function outsideChina({ lng, lat }: LngLat): boolean {
  return lng < 72.004 || lng > 137.8347 || lat < 0.8293 || lat > 55.8271;
}

function transformLat({ lng, lat }: LngLat): number {
  const x = lng - 105.0;
  const y = lat - 35.0;
  let result =
    -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * Math.sqrt(Math.abs(x));
  result += ((20.0 * Math.sin(6.0 * x * PI) + 20.0 * Math.sin(2.0 * x * PI)) * 2.0) / 3.0;
  result += ((20.0 * Math.sin(y * PI) + 40.0 * Math.sin((y / 3.0) * PI)) * 2.0) / 3.0;
  result += ((160.0 * Math.sin((y / 12.0) * PI) + 320 * Math.sin((y * PI) / 30.0)) * 2.0) / 3.0;
  return result;
}

function transformLng({ lng, lat }: LngLat): number {
  const x = lng - 105.0;
  const y = lat - 35.0;
  let result = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * Math.sqrt(Math.abs(x));
  result += ((20.0 * Math.sin(6.0 * x * PI) + 20.0 * Math.sin(2.0 * x * PI)) * 2.0) / 3.0;
  result += ((20.0 * Math.sin(x * PI) + 40.0 * Math.sin((x / 3.0) * PI)) * 2.0) / 3.0;
  result += ((150.0 * Math.sin((x / 12.0) * PI) + 300.0 * Math.sin((x / 30.0) * PI)) * 2.0) / 3.0;
  return result;
}

/** 这一点上 GCJ-02 相对 WGS-84 的偏移量，单位度。 */
function offset(point: LngLat): LngLat {
  const radLat = (point.lat / 180.0) * PI;
  let magic = Math.sin(radLat);
  magic = 1 - ECCENTRICITY_SQUARED * magic * magic;
  const sqrtMagic = Math.sqrt(magic);
  const meridian = (AXIS * (1 - ECCENTRICITY_SQUARED)) / (magic * sqrtMagic);
  const parallel = (AXIS / sqrtMagic) * Math.cos(radLat);
  return {
    lng: (transformLng(point) * 180.0) / (parallel * PI),
    lat: (transformLat(point) * 180.0) / (meridian * PI),
  };
}

/** WGS-84 转 GCJ-02：画在高德底图上之前用。 */
export function wgs84ToGcj02(point: LngLat): LngLat {
  if (outsideChina(point)) return point;
  const delta = offset(point);
  return { lng: point.lng + delta.lng, lat: point.lat + delta.lat };
}

/**
 * GCJ-02 转回 WGS-84：从地图上点选取回坐标时用。
 *
 * 没有解析解，用迭代逼近：先按目标点估一次偏移减掉，再拿结果正算回去看差多少，
 * 依次修正。三轮之后残差在厘米级，远小于人在地图上点选的精度。
 */
export function gcj02ToWgs84(point: LngLat): LngLat {
  if (outsideChina(point)) return point;
  let guess = { lng: point.lng, lat: point.lat };
  for (let round = 0; round < 3; round += 1) {
    const forward = wgs84ToGcj02(guess);
    guess = {
      lng: guess.lng + (point.lng - forward.lng),
      lat: guess.lat + (point.lat - forward.lat),
    };
  }
  return guess;
}

/** 经纬度是否落在合法区间。数据库也有同样的约束，这里只是让用户早点看到。 */
export function validCoordinates({ lng, lat }: LngLat): boolean {
  return (
    Number.isFinite(lng) && Number.isFinite(lat)
    && lng >= -180 && lng <= 180 && lat >= -90 && lat <= 90
  );
}

export type Axis = "lng" | "lat";

/** 各轴的半球字母：先正后负。 */
const HEMISPHERES: Record<Axis, [string, string]> = { lat: ["N", "S"], lng: ["E", "W"] };

/**
 * 把一栏里填的坐标读成十进制度。
 *
 * 工程资料上给的是度分秒（`N41°6'55.2"`），而地图和数据库要的是十进制度，
 * 所以录入这一层负责换算，库里只存一种写法。
 *
 * 两种都收：度分秒和十进制度。整串「N41°6'55.2",E121°11'46.7"」粘进任意一栏也认得，
 * 按字母取出属于这一栏的那一半。填错栏（把 N 开头的填进经度）返回空，让上层报错。
 */
export function parseCoordinate(text: string, axis: Axis): number | null {
  const cleaned = text.trim().toUpperCase().replace(/，/g, ",");
  if (cleaned === "") return null;

  const [positive, negative] = HEMISPHERES[axis];
  const [otherPositive, otherNegative] = HEMISPHERES[axis === "lat" ? "lng" : "lat"];
  const parts = cleaned.split(",").map((part) => part.trim()).filter(Boolean);
  const mine = parts.length > 1
    ? parts.find((part) => part.includes(positive) || part.includes(negative))
    : parts[0];
  if (mine === undefined) return null;
  if (mine.includes(otherPositive) || mine.includes(otherNegative)) return null;

  const sign = mine.includes(negative) || mine.startsWith("-") ? -1 : 1;
  const body = mine.replace(/[NSEW+\-\s]/g, "");
  if (/^\d+(\.\d+)?$/.test(body)) return sign * Number(body);

  const dms = /^(\d+(?:\.\d+)?)[°º](?:(\d+(?:\.\d+)?)['′’](?:(\d+(?:\.\d+)?)["″”]?)?)?$/.exec(body);
  if (dms === null) return null;
  const degrees = Number(dms[1]);
  const minutes = Number(dms[2] ?? 0);
  const seconds = Number(dms[3] ?? 0);
  if (minutes >= 60 || seconds >= 60) return null;
  return sign * (degrees + minutes / 60 + seconds / 3600);
}

/** 十进制度写成度分秒，秒保留到两位并去掉无意义的尾零。 */
export function formatDms(value: number, axis: Axis): string {
  const [positive, negative] = HEMISPHERES[axis];
  const hemisphere = value < 0 ? negative : positive;
  const total = Math.round(Math.abs(value) * 360000) / 100;
  let degrees = Math.floor(total / 3600);
  let minutes = Math.floor((total - degrees * 3600) / 60);
  let seconds = Math.round((total - degrees * 3600 - minutes * 60) * 100) / 100;
  // 进位：59.999" 四舍五入成 60" 时要往上抬一位，不能印出 60 秒。
  if (seconds >= 60) {
    seconds -= 60;
    minutes += 1;
  }
  if (minutes >= 60) {
    minutes -= 60;
    degrees += 1;
  }
  return `${hemisphere}${degrees}°${minutes}'${Number(seconds.toFixed(2))}"`;
}

/** 一行写完的桥位坐标，纬度在前，和工程资料上的写法一致。 */
export function formatCoordinates(lng: number, lat: number): string {
  return `${formatDms(lat, "lat")},${formatDms(lng, "lng")}`;
}
