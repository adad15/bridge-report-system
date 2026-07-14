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
