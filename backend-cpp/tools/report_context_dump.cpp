/**
 * @file report_context_dump.cpp
 * @brief 把某个年度的 ReportContext 导成 JSON 的诊断工具。
 *
 * 用于拿真实数据看生成效果、以及第 10 步的真实规模验收：报告任务的 HTTP 出口
 * 还没有（设计 §27 第 8 步），但上下文组装本身已经完成，这个小工具让它可以被
 * 单独跑起来。
 *
 * 它走的是生产同一条路径——同一个 ReportContextRepository、同一份规范包注册表，
 * 所以导出来的 JSON 与将来任务里发给 Python 的那份是同一个东西；如果这里改成
 * 另写一份查询，看到的效果就不作数了。
 *
 * 用法（仓库根目录）：
 *     report-context-dump <年度ID> <输出JSON路径> [配置文件]
 */

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReportContextRepository.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace {

std::shared_ptr<bridge_report::standards::StandardRegistry> load_registry(
    const std::filesystem::path& standards_root) {
    auto registry = std::make_shared<bridge_report::standards::StandardRegistry>();
    bridge_report::standards::StandardPackageLoader loader;
    for (const auto& package_root : loader.discover(standards_root)) {
        auto loaded = loader.load(package_root);
        if (loaded.ok()) registry->register_package(std::move(*loaded.package));
    }
    return registry;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "用法: report-context-dump <年度ID> <输出JSON路径> [配置文件]\n";
        return 2;
    }
    const std::string inspection_year_id = argv[1];
    const std::filesystem::path output_path = argv[2];
    const std::string config_path = argc > 3 ? argv[3] : "config/local.json";

    try {
        const auto config = bridge_report::config::load_app_config(config_path);
        const auto db_client = bridge_report::db::create_db_client(config.postgres, 1);
        const auto registry = load_registry(config.standards_root);
        std::cout << "规范包: " << registry->package_count() << " 个\n";

        const auto context =
            bridge_report::db::ReportContextRepository(db_client, registry)
                .build(inspection_year_id);
        if (!context.has_value()) {
            std::cerr << "上下文组装失败：年度不存在，或尚未配置报告模板。\n";
            return 1;
        }

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "  ";
        writer["emitUTF8"] = true;
        std::ofstream stream(output_path, std::ios::binary);
        stream << Json::writeString(writer, context->to_json());

        std::cout << "已写出 " << output_path.string() << "\n"
                  << "  结构部位 " << context->parts.size()
                  << "，人员 " << context->personnel.size()
                  << "，设备 " << context->equipment.size()
                  << "，部件权重行 " << context->assessment.component_weights.size()
                  << "，部件评定 " << context->assessment.categories.size() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "失败: " << error.what() << "\n";
        return 1;
    }
}
