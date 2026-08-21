#include <gtest/gtest.h>

#include "bridge_report/http/RoutePreflightAudit.hpp"

namespace {

using bridge_report::http::paths_missing_preflight;

drogon::HttpHandlerInfo handler(std::string path, drogon::HttpMethod method) {
    return {std::move(path), method, ""};
}

// bind-multi 上线时的真实形态：干活的注册了，接预检的漏了。
TEST(RoutePreflightAuditTest, ReportsAWritePathWithoutItsOptionsHandler) {
    const auto missing = paths_missing_preflight({
        handler("/api/x/bind", drogon::Post),
        handler("/api/x/bind", drogon::Options),
        handler("/api/x/bind-multi", drogon::Post),
    });
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing.front(), "/api/x/bind-multi");
}

TEST(RoutePreflightAuditTest, PassesWhenEveryWritePathHasOne) {
    EXPECT_TRUE(paths_missing_preflight({
        handler("/api/x/bind", drogon::Post),
        handler("/api/x/bind", drogon::Options),
        handler("/api/x/clear", drogon::Delete),
        handler("/api/x/clear", drogon::Options),
    }).empty());
}

// GET 要不要预检取决于调用方带不带自定义头，服务端看不到，不该一刀切成缺陷。
TEST(RoutePreflightAuditTest, DoesNotFlagReadOnlyPaths) {
    EXPECT_TRUE(paths_missing_preflight({
        handler("/health", drogon::Get),
        handler("/api/x/inventory", drogon::Get),
    }).empty());
}

TEST(RoutePreflightAuditTest, CoversEveryMutatingMethod) {
    const auto missing = paths_missing_preflight({
        handler("/api/put", drogon::Put),
        handler("/api/patch", drogon::Patch),
        handler("/api/delete", drogon::Delete),
        handler("/api/post", drogon::Post),
    });
    EXPECT_EQ(missing, (std::vector<std::string>{
        "/api/delete", "/api/patch", "/api/post", "/api/put"}));
}

// 同一路径注册了多个改写方法时只报一次，别让一条路径刷屏。
TEST(RoutePreflightAuditTest, ReportsEachPathOnce) {
    const auto missing = paths_missing_preflight({
        handler("/api/x", drogon::Post),
        handler("/api/x", drogon::Put),
        handler("/api/x", drogon::Delete),
    });
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing.front(), "/api/x");
}

// 末尾斜杠不该让同一条路径被判成两条。
TEST(RoutePreflightAuditTest, TreatsTrailingSlashAsTheSamePath) {
    EXPECT_TRUE(paths_missing_preflight({
        handler("/api/x/", drogon::Post),
        handler("/api/x", drogon::Options),
    }).empty());
}

}  // namespace
