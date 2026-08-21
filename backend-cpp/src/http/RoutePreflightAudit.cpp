#include "bridge_report/http/RoutePreflightAudit.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

namespace bridge_report::http {
namespace {

bool is_mutating(drogon::HttpMethod method) {
    return method == drogon::Post || method == drogon::Put
        || method == drogon::Patch || method == drogon::Delete;
}

// drogon 内部对注册路径做过归一化（大小写等），两边取自同一张表，这里只把
// 首尾空白和末尾斜杠抹平，避免 "/a" 与 "/a/" 被当成两条路径。
std::string canonical(std::string path) {
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) {
        path.pop_back();
    }
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

}  // namespace

std::vector<std::string> paths_missing_preflight(
    const std::vector<drogon::HttpHandlerInfo>& handlers
) {
    std::set<std::string> preflighted;
    std::set<std::string> mutating;
    for (const auto& handler : handlers) {
        const auto path = canonical(std::get<0>(handler));
        const auto method = std::get<1>(handler);
        if (method == drogon::Options) {
            preflighted.insert(path);
        } else if (is_mutating(method)) {
            mutating.insert(path);
        }
    }

    std::vector<std::string> missing;
    std::set_difference(mutating.begin(), mutating.end(),
                        preflighted.begin(), preflighted.end(),
                        std::back_inserter(missing));
    return missing;
}

}  // namespace bridge_report::http
