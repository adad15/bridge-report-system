#include <gtest/gtest.h>

#include "bridge_report/http/InspectionRatingTreeRoutes.hpp"
#include "bridge_report/http/ImportResolutionRoutes.hpp"

// 仓储结果 → 错误码 + HTTP 状态的映射。
//
// 这段决策原本埋在路由 lambda 里没人验得了，而它正是最容易出错的地方：Invalid 分支
// 曾经把仓储带回的具体错误码整个丢掉，一律报一个笼统的码，前端于是分不开"参数无效"
// 和"版本已变化"这两种完全不同的处置。
//
// 4.0 绑定链路下线时，覆盖这段的测试文件跟着一起删掉了；这里把它补回来。

namespace {

using bridge_report::db::RatingTreeBindingOutcome;
using bridge_report::db::RatingTreeBindingStatus;
using bridge_report::http::rating_tree_binding_error_response;

RatingTreeBindingOutcome outcome(RatingTreeBindingStatus status,
                                 std::string code = {}, std::string message = {}) {
    RatingTreeBindingOutcome value;
    value.status = status;
    value.error_code = std::move(code);
    value.error_message = std::move(message);
    return value;
}

}  // namespace

// 编辑锁在事务内失效与保存草稿、确认入库共用一个码：三条路径的处置相同（刷新页面
// 重新取锁），前端一处接住即可。
TEST(RatingTreeBindingRoutesTest, EditLockFailureIsAConflictWithTheSharedCode) {
    const auto response =
        rating_tree_binding_error_response(outcome(RatingTreeBindingStatus::EditLockInvalid));
    EXPECT_EQ(response.error_code, "edit_lock_invalid");
    EXPECT_EQ(response.http_status, 409);
}

// 仓储带回具体错误码时必须优先用它。同一个 Conflict 下，"台账版本已变化"要求前端
// 刷新，"年度已有正式评定"则是彻底拒绝，套用默认码会把两者抹平。
TEST(RatingTreeBindingRoutesTest, KeepsTheSpecificCodeTheRepositoryReturned) {
    const auto response = rating_tree_binding_error_response(outcome(
        RatingTreeBindingStatus::Conflict,
        "component_inventory_revision_changed", "构件台账版本已变化，请刷新后重试。"));
    EXPECT_EQ(response.error_code, "component_inventory_revision_changed");
    EXPECT_EQ(response.error_message, "构件台账版本已变化，请刷新后重试。");
    EXPECT_EQ(response.http_status, 409);
}

TEST(RatingTreeBindingRoutesTest, FallsBackToADefaultCodeWhenTheRepositoryGivesNone) {
    const auto response =
        rating_tree_binding_error_response(outcome(RatingTreeBindingStatus::Conflict));
    EXPECT_EQ(response.error_code, "component_binding_conflict");
    EXPECT_FALSE(response.error_message.empty());
    EXPECT_EQ(response.http_status, 409);
}

// 评定树自身的三种拒绝各有各的处置：换一版、等发布、先修台账映射。
TEST(RatingTreeBindingRoutesTest, SeparatesTheThreeRatingTreeRefusals) {
    EXPECT_EQ(rating_tree_binding_error_response(
                  outcome(RatingTreeBindingStatus::TreeNotFound)).http_status, 404);
    EXPECT_EQ(rating_tree_binding_error_response(
                  outcome(RatingTreeBindingStatus::TreeUnavailable)).error_code,
              "rating_tree_unavailable");
    EXPECT_EQ(rating_tree_binding_error_response(
                  outcome(RatingTreeBindingStatus::MappingIncompatible)).error_code,
              "rating_tree_inventory_incompatible");
}

// 数据库异常不能伪装成业务拒绝：503 才会让调用方重试，4xx 会让它以为改请求就能过。
TEST(RatingTreeBindingRoutesTest, DatabaseFailureIsUnavailableNotABusinessRefusal) {
    const auto response =
        rating_tree_binding_error_response(outcome(RatingTreeBindingStatus::Failed));
    EXPECT_EQ(response.error_code, "database_unavailable");
    EXPECT_EQ(response.http_status, 503);
}

// P2-5 回归：解析命令的版本冲突要保留服务层给的具体错误码。
//
// 三种版本冲突的处置完全不同：来源草稿冲突要客户端取回最新草稿再合并；解析对象冲突
// 只需刷新那一个对象；台账版本冲突要重新选构件。硬编码成一个码，前端就只能一律提示
// "请刷新"，而它为草稿冲突准备的差异恢复分支永远走不到。
TEST(ResolutionErrorResponseTest, KeepsTheSpecificVersionConflictCode) {
    bridge_report::resolution::ResolutionOutcome outcome;
    outcome.status = bridge_report::resolution::ResolutionStatus::VersionConflict;
    outcome.error_code = "review_draft_version_conflict";
    outcome.error_message = "草稿已被其他页面保存，请刷新后重试。";

    const auto response = bridge_report::http::resolution_error_response(outcome);
    EXPECT_EQ(response.error_code, "review_draft_version_conflict");
    EXPECT_EQ(response.error_message, "草稿已被其他页面保存，请刷新后重试。");
    EXPECT_EQ(response.http_status, 409);
}

// 服务层没给具体码时才用默认的，行为与此前一致。
TEST(ResolutionErrorResponseTest, FallsBackToTheGenericResolutionConflict) {
    bridge_report::resolution::ResolutionOutcome outcome;
    outcome.status = bridge_report::resolution::ResolutionStatus::VersionConflict;

    const auto response = bridge_report::http::resolution_error_response(outcome);
    EXPECT_EQ(response.error_code, "resolution_version_conflict");
    EXPECT_FALSE(response.error_message.empty());
    EXPECT_EQ(response.http_status, 409);
}
