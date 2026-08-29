#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/resolution/ResolutionModels.hpp"

namespace bridge_report::db {

/**
 * @brief 构件解析与评分树解析关系表的纯读写层。
 *
 * 它只负责数据库读写，不自行决定业务结果（设计 §12）：状态该不该变、目标合不合法、
 * 哈希是否失效，全部由上层的解析服务判定。仓储在这里只保证一件事——写进去的行与
 * 027 迁移的约束一致，读出来的行忠实还原。
 *
 * 传入 `drogon::orm::Transaction` 即可让全部调用参与调用方的事务；导入初始化正是
 * 这么用的，它必须与 `parsed_result_json` 的写入同生共死。
 */
class ImportResolutionRepository {
public:
    explicit ImportResolutionRepository(drogon::orm::DbClientPtr db_client);

    // --- 来源构件组 -------------------------------------------------------
    /// 插入一个组并回填生成的 id 与 version。
    resolution::ComponentResolutionGroup insert_group(
        const resolution::ComponentResolutionGroup& group) const;

    std::vector<resolution::ComponentResolutionGroup> list_groups(
        const std::string& import_record_id) const;

    std::optional<resolution::ComponentResolutionGroup> find_group(
        const std::string& group_id) const;

    /**
     * @brief 按 expected_version 条件更新组状态，成功时 version 递增。
     *
     * 返回递增后的版本；条件不成立（版本已过期或组不存在）时返回 nullopt，
     * 由调用方翻译成 resolution_version_conflict。
     */
    std::optional<int> update_group_resolution(
        const std::string& group_id,
        int expected_version,
        const std::string& status,
        const std::optional<std::string>& match_method,
        const std::optional<std::string>& inventory_revision_id,
        const std::string& resolution_mode,
        const std::optional<std::string>& resolved_by_user_id) const;

    // --- 组成员 -----------------------------------------------------------
    resolution::ComponentGroupMember insert_member(
        const resolution::ComponentGroupMember& member) const;

    std::vector<resolution::ComponentGroupMember> list_members(
        const std::string& import_record_id) const;

    void delete_member(const std::string& member_id) const;

    /// 删除一个导入下没有任何成员的空组；返回删掉的行数。
    int delete_empty_groups(const std::string& import_record_id) const;

    // --- 解析目标 ---------------------------------------------------------
    resolution::ComponentResolutionTarget insert_target(
        const resolution::ComponentResolutionTarget& target) const;

    std::vector<resolution::ComponentResolutionTarget> list_targets(
        const std::string& group_id) const;

    /// 一次取回整份导入的目标。工作区读模型必须走这条：按组逐次查是 N+1，
    /// 一次导入几百个组时它就是首屏卡顿的全部来源。
    std::vector<resolution::ComponentResolutionTarget> list_targets_by_import(
        const std::string& import_record_id) const;

    void delete_targets(const std::string& group_id) const;

    // --- 病害解析实例 -----------------------------------------------------
    resolution::ResolvedDefectInstance insert_instance(
        const resolution::ResolvedDefectInstance& instance) const;

    std::vector<resolution::ResolvedDefectInstance> list_instances_by_import(
        const std::string& import_record_id) const;

    std::vector<resolution::ResolvedDefectInstance> list_instances_by_member(
        const std::string& group_member_id) const;

    void delete_instance(const std::string& instance_id) const;

    /**
     * @brief 差量对齐时重刷序号与构件解析世代；不动覆盖、状态和实例自身版本（§9.1）。
     *
     * **当前没有调用方**，这是刻意留着的：重绑定目前仍是"删掉全部目标、按旧内容重建"，
     * 于是没变化的实例也会换掉 resolved_defect_instance_id。要改成真正的增量差分
     * （未变化的目标复用 target 与 instance 行，只重刷这两个字段），需要的就是它。
     * 那一轮做之前不要删，也不要以为它是忘了接线。
     */
    void restamp_instance(
        const std::string& instance_id,
        int instance_order,
        int component_resolution_version) const;

    /**
     * @brief 切换实例状态并按 expected_version 条件写入，成功时 version 递增。
     *
     * 状态与照片归属必须一起改：`is_photo_owner` 上有"被忽略的实例不能持有照片"的
     * CHECK，先改状态再重算会在中间那一刻直接被数据库挡下来。把两步收进一个操作，
     * 调用方就没有写出中间态的机会。
     *
     * 返回递增后的版本；版本过期或实例不存在时返回 nullopt。
     */
    std::optional<int> set_instance_status(
        const std::string& instance_id,
        int expected_version,
        const std::string& instance_status) const;

    /**
     * @brief 重算一条来源病害的照片归属。
     *
     * 归属者是**活动实例中 instance_order 最小的那个**，不是"instance_order = 1 的
     * 活动实例"——实例 1 被忽略时后者会让整组照片没有归属者，正式入库时凭空消失。
     * 没有活动实例时全部清零。
     */
    void recompute_photo_owner(const std::string& group_member_id) const;

    // --- 评分树解析 -------------------------------------------------------
    void upsert_rating_resolution(const resolution::RatingResolution& resolution) const;

    std::vector<resolution::RatingResolution> list_rating_resolutions_by_import(
        const std::string& import_record_id) const;

    std::optional<resolution::RatingResolution> find_rating_resolution(
        const std::string& instance_id) const;

    void delete_rating_resolution(const std::string& instance_id) const;

    // --- 审计 -------------------------------------------------------------
    void append_event(const resolution::ResolutionEvent& event) const;

    // --- 来源草稿并发版本（§8.0）------------------------------------------
    /// 读取当前 draft_version；导入记录不存在时返回 nullopt。
    std::optional<int> read_draft_version(const std::string& import_record_id) const;

    /// 按 expected 条件递增 draft_version，返回新版本；条件不成立时 nullopt。
    std::optional<int> bump_draft_version(
        const std::string& import_record_id, int expected_version) const;

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
