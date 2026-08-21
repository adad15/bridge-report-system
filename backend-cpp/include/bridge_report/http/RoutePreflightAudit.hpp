#pragma once

#include <string>
#include <vector>

#include <drogon/HttpAppFramework.h>

namespace bridge_report::http {

/**
 * @brief 找出缺少 OPTIONS 预检处理器的写接口。
 *
 * 浏览器在发出带自定义头（本项目是 X-Edit-Lock-Token、Authorization）或 JSON 体的
 * 改写请求前，会先发一个 OPTIONS 预检。该路径没有 OPTIONS 处理器时预检得到 404，
 * **真正的请求根本不会发出**——前端只能报一句笼统的网络错误，后端日志里一片空白，
 * 三条线索全是死的。bind-multi 上线时就是这么坏的。
 *
 * 判定只看改写方法（POST/PUT/PATCH/DELETE）。GET 是否需要预检取决于调用方带不带
 * 自定义头，服务端看不到，不在这里一刀切。
 *
 * @param handlers 一般传 drogon::app().getHandlersInfo()。
 * @return 缺预检的路径，去重并排序；全都齐备时为空。
 */
[[nodiscard]] std::vector<std::string> paths_missing_preflight(
    const std::vector<drogon::HttpHandlerInfo>& handlers);

}  // namespace bridge_report::http
