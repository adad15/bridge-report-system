#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/ReportDirectoryModels.hpp"

namespace bridge_report::db {

/**
 * @brief 报告人员库与检测设备库的数据访问入口（设计 §15.1、§15.2）。
 *
 * 两张表结构同构、生命周期规则也一样，合成一个仓储：被年度配置引用后不得硬删除，
 * 只能停用。删除接口据此返回 Referenced 而不是把外键异常抛给上层——那是业务结论，
 * 不是数据库故障。
 */
class ReportDirectoryRepository {
public:
    explicit ReportDirectoryRepository(drogon::orm::DbClientPtr client);

    // ---- 人员库 ----------------------------------------------------------

    std::vector<report::ReportPersonnel> list_personnel(bool only_enabled) const;
    std::optional<report::ReportPersonnel> find_personnel(const std::string& id) const;
    report::ReportPersonnel create_personnel(const report::ReportPersonnelInput& input) const;
    std::optional<report::ReportPersonnel> update_personnel(
        const std::string& id, const report::ReportPersonnelInput& input) const;
    std::optional<report::ReportPersonnel> set_personnel_enabled(
        const std::string& id, bool enabled) const;
    report::DirectoryDeleteStatus delete_personnel(const std::string& id) const;

    // ---- 设备库 ----------------------------------------------------------

    std::vector<report::ReportEquipment> list_equipment(bool only_enabled) const;
    std::optional<report::ReportEquipment> find_equipment(const std::string& id) const;
    report::ReportEquipment create_equipment(const report::ReportEquipmentInput& input) const;
    std::optional<report::ReportEquipment> update_equipment(
        const std::string& id, const report::ReportEquipmentInput& input) const;
    std::optional<report::ReportEquipment> set_equipment_enabled(
        const std::string& id, bool enabled) const;
    report::DirectoryDeleteStatus delete_equipment(const std::string& id) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
