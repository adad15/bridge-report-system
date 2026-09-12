#pragma once

#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/ReportContextModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::db {

/**
 * @brief 组装一次生成所需的 ReportContext（设计 §9.2、§5.4）。
 *
 * 全部读取在一个 REPEATABLE READ 事务里完成：同一个生成任务不能混入生成过程中
 * 发生的后续修改。构造完就与数据库脱钩，Python 只认这份上下文。
 *
 * 两件事在这里定死，不留给下游：
 *
 *  1. **顺序**（设计 §10.2）：结构部位按上部、下部、桥面系、全桥、其他；
 *     部位内按构件台账的自然编号；同构件内按病害入库顺序。
 *  2. **报告图号**（设计 §7.5、§11.2）：按上面的顺序在每个部位内从 1 重排，
 *     套模板配置的前缀。病害表的「照片编号」列与图题取的是同一批号码——
 *     两处各算各的，读者按表里的号就找不到图。
 */
class ReportContextRepository {
public:
    /// registry 可以为空：不给规范包时，部件权重计算表（表4.1-1）里那些"本桥没有
    /// 的部件"就列不出来——评定结果里只有实际存在的部件。其余内容不受影响。
    explicit ReportContextRepository(
        drogon::orm::DbClientPtr client,
        std::shared_ptr<const standards::StandardRegistry> registry = nullptr);

    /// 年度不存在、或尚未配置模板时返回空。调用方应先跑生成前检查。
    std::optional<report::ReportContext> build(const std::string& inspection_year_id) const;

private:
    /// 评定运行锁定的规范包与桥型。报告读评定当时用的那一版，不按今天的规范重推。
    struct LockedStandard {
        const standards::StandardPackage* package{nullptr};
        std::string bridge_type_id;
    };

    LockedStandard locked_standard(const std::shared_ptr<drogon::orm::Transaction>& tx,
                                   const std::string& run_id) const;

    /// 部件权重计算表（表4.1-1）。要规范包才拼得出"本桥没有的部件"那几行。
    void load_weight_table(const LockedStandard& locked,
                           report::ReportAssessment& assessment) const;

    drogon::orm::DbClientPtr client_;
    std::shared_ptr<const standards::StandardRegistry> registry_;
};

}  // namespace bridge_report::db
