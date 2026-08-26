#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/http/DefectThreadRoutes.hpp"

using bridge_report::http::BindObservationRequest;
using bridge_report::http::CreateThreadRequest;
using bridge_report::http::parse_bind_observation_request;
using bridge_report::http::parse_create_thread_request;

namespace {

constexpr const char* kUuidA = "11111111-1111-1111-1111-111111111111";
constexpr const char* kUuidB = "22222222-2222-2222-2222-222222222222";

Json::Value valid_create_body() {
    Json::Value body;
    body["bridge_component_id"] = kUuidA;
    body["defect_type"] = "蜂窝、麻面";
    body["defect_location"] = "左侧端部";
    body["first_observation_id"] = kUuidB;
    body["expected_observation_updated_at"] = "2026-07-14 10:00:00+08";
    return body;
}

}  // namespace

TEST(ParseCreateThreadRequestTest, AcceptsValidBodyAndOptionalThreadName) {
    CreateThreadRequest parsed;
    EXPECT_FALSE(parse_create_thread_request(valid_create_body(), parsed).has_value());
    EXPECT_EQ(parsed.defect_type, "蜂窝、麻面");
    EXPECT_EQ(parsed.defect_location, "左侧端部");
    EXPECT_FALSE(parsed.thread_name.has_value());

    auto body = valid_create_body();
    body["thread_name"] = "自定义线索名";
    EXPECT_FALSE(parse_create_thread_request(body, parsed).has_value());
    EXPECT_EQ(parsed.thread_name.value(), "自定义线索名");
}

TEST(ParseCreateThreadRequestTest, RejectsMissingTypeLocationOrToken) {
    for (const char* field : {"defect_type", "defect_location", "expected_observation_updated_at",
                              "bridge_component_id", "first_observation_id"}) {
        SCOPED_TRACE(field);
        auto body = valid_create_body();
        body.removeMember(field);
        CreateThreadRequest parsed;
        const auto error = parse_create_thread_request(body, parsed);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(*error, "thread_required_field_missing");
    }
}

TEST(ParseCreateThreadRequestTest, RejectsNonUuidIdentifiers) {
    auto body = valid_create_body();
    body["first_observation_id"] = "not-a-uuid";
    CreateThreadRequest parsed;
    EXPECT_EQ(parse_create_thread_request(body, parsed).value(), "thread_required_field_missing");
}

TEST(ParseBindObservationRequestTest, AcceptsBindUnbindAndConfirmFlag) {
    Json::Value body;
    body["defect_thread_id"] = kUuidA;
    body["expected_observation_updated_at"] = "2026-07-14 10:00:00+08";
    BindObservationRequest parsed;
    EXPECT_FALSE(parse_bind_observation_request(body, parsed).has_value());
    EXPECT_EQ(parsed.defect_thread_id.value(), kUuidA);
    EXPECT_FALSE(parsed.confirm_rebind);

    body["defect_thread_id"] = Json::Value(Json::nullValue);
    body["confirm_rebind"] = true;
    EXPECT_FALSE(parse_bind_observation_request(body, parsed).has_value());
    EXPECT_FALSE(parsed.defect_thread_id.has_value());
    EXPECT_TRUE(parsed.confirm_rebind);
}

TEST(ParseBindObservationRequestTest, RejectsMissingTokenOrInvalidThreadId) {
    Json::Value body;
    body["defect_thread_id"] = kUuidA;
    BindObservationRequest parsed;
    EXPECT_EQ(parse_bind_observation_request(body, parsed).value(), "thread_required_field_missing");

    body["expected_observation_updated_at"] = "2026-07-14 10:00:00+08";
    body["defect_thread_id"] = "not-a-uuid";
    EXPECT_EQ(parse_bind_observation_request(body, parsed).value(), "thread_required_field_missing");
}

// ── 批量应用请求解析 ──────────────────────────────────────────────────
// 请求本身不合法的几种，不必进事务就能判掉——省一次数据库往返，也让错误码更准确。

namespace {

Json::Value apply_body(const std::string& action, const Json::Value& groups) {
    Json::Value body;
    body["batch_id"] = "0123456789abcdef0123456789abcdef";
    body["batch_fingerprint"] = "fingerprint";
    body["action"] = action;
    body["groups"] = groups;
    return body;
}

Json::Value apply_group(const std::string& target_thread_id = "") {
    Json::Value group;
    group["group_id"] = "abcdef0123456789abcdef0123456789";
    if (!target_thread_id.empty()) group["target_thread_id"] = target_thread_id;
    Json::Value observation;
    observation["id"] = "11111111-1111-1111-1111-111111111111";
    observation["updated_at"] = "2026-08-26 10:00:00+08";
    group["observations"] = Json::Value(Json::arrayValue);
    group["observations"].append(observation);
    return group;
}

}  // namespace

TEST(TriageApplyRequestTest, AcceptsACreateBatchWithoutTargets) {
    Json::Value groups(Json::arrayValue);
    groups.append(apply_group());

    bridge_report::http::TriageApplyRequestBody parsed;
    const auto error = bridge_report::http::parse_triage_apply_request(
        apply_body("create", groups), parsed);

    EXPECT_FALSE(error.has_value());
    EXPECT_EQ(parsed.action, "create");
    ASSERT_EQ(parsed.groups.size(), 1u);
    EXPECT_TRUE(parsed.groups[0].target_thread_id.empty());
    ASSERT_EQ(parsed.groups[0].observations.size(), 1u);
    EXPECT_EQ(parsed.groups[0].observations[0].updated_at, "2026-08-26 10:00:00+08")
        << "并发令牌必须原样带进来，落库时要拿它比对";
}

TEST(TriageApplyRequestTest, RejectsACreateGroupThatNamesATarget) {
    Json::Value groups(Json::arrayValue);
    groups.append(apply_group("22222222-2222-2222-2222-222222222222"));

    bridge_report::http::TriageApplyRequestBody parsed;
    const auto error = bridge_report::http::parse_triage_apply_request(
        apply_body("create", groups), parsed);

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(*error, "unexpected_target_thread");
}

TEST(TriageApplyRequestTest, RejectsABindGroupWithoutATarget) {
    Json::Value groups(Json::arrayValue);
    groups.append(apply_group());

    bridge_report::http::TriageApplyRequestBody parsed;
    const auto error = bridge_report::http::parse_triage_apply_request(
        apply_body("bind", groups), parsed);

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(*error, "bind_target_required");
}

TEST(TriageApplyRequestTest, RejectsAnEmptyOrOversizedSelection) {
    bridge_report::http::TriageApplyRequestBody parsed;

    const auto empty = bridge_report::http::parse_triage_apply_request(
        apply_body("create", Json::Value(Json::arrayValue)), parsed);
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(*empty, "empty_group_selection");

    Json::Value many(Json::arrayValue);
    for (int index = 0; index <= bridge_report::http::kTriageApplyMaxGroups; ++index) {
        many.append(apply_group());
    }
    const auto oversized = bridge_report::http::parse_triage_apply_request(
        apply_body("create", many), parsed);
    ASSERT_TRUE(oversized.has_value());
    // 超限直接拒绝，不由前端拆分——拆开就不再是一个事务，"全成或全败"随之作废。
    EXPECT_EQ(*oversized, "batch_too_large");
}

TEST(TriageApplyRequestTest, RejectsAnObservationMissingItsToken) {
    Json::Value group = apply_group();
    group["observations"][0].removeMember("updated_at");
    Json::Value groups(Json::arrayValue);
    groups.append(group);

    bridge_report::http::TriageApplyRequestBody parsed;
    const auto error = bridge_report::http::parse_triage_apply_request(
        apply_body("create", groups), parsed);

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(*error, "triage_required_field_missing");
}

TEST(TriageApplyRequestTest, RejectsAnUnknownAction) {
    Json::Value groups(Json::arrayValue);
    groups.append(apply_group());

    bridge_report::http::TriageApplyRequestBody parsed;
    const auto error = bridge_report::http::parse_triage_apply_request(
        apply_body("merge", groups), parsed);

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(*error, "triage_invalid_action");
}
