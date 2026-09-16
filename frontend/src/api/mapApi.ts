// 地图服务的前端配置。
//
// key 不写死在前端构建里，由后端从 config/local.json 下发，部署时只改一处。
// 服务端调静态地图接口用的那个 key 留在后端，永远不会出现在这里。

import { request } from "./apiClient";

export interface MapConfig {
  js_key: string;
  security_js_code: string;
  /** 没配 key 就没有地图，界面换成已经存下的地理位置图，而不是摆一个空框。 */
  available: boolean;
}

export function fetchMapConfig(baseUrl: string): Promise<MapConfig> {
  return request(`${baseUrl}/api/config/map`);
}
