#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "bridge_report/archive/WordInputArchive.hpp"

namespace bridge_report::archive {

/// 接口同步导入的来源引用：指向本机离线库，而不是它的副本。
struct SourceDbReference {
    std::string source_db_path;  //: 离线库的绝对路径（UTF-8）
    std::string task_id;         //: 该次检测在离线库里的任务 id
};

enum class SourceDbValidationError {
    None,
    PathMissing,      //: 路径为空或不是一个可读的普通文件
    NotSqlite,        //: 文件头不是 SQLite 数据库
    TaskIdMissing,
};

struct SourceDbValidationResult {
    SourceDbValidationError error{SourceDbValidationError::None};
    std::string original_file_name;  //: 离线库的文件名，用作导入名称

    bool ok() const noexcept { return error == SourceDbValidationError::None; }
};

/// 来源软件把离线库固定放在 %APPDATA% 下的这个位置。用户不会知道这串路径，
/// 也没理由要求他们知道——界面默认就用它，读不到再让人自己指。
/// 取不到 %APPDATA% 时返回空路径。
std::filesystem::path default_source_db_path();

/// Windows 上 std::filesystem::path(std::string) 走当前代码页；离线库路径里
/// 常有中文，必须按 UTF-8 解释，否则打不开甚至抛"无法映射字符"。
std::filesystem::path path_from_utf8(std::string_view value);

/// 只读校验：文件存在、可读、且确实是 SQLite 库。不打开数据库，也不写任何东西。
SourceDbValidationResult validate_source_db(const SourceDbReference& reference);

/// 引用文件的内容（JSON）。放进临时目录后，其生命周期与 Word 源文件完全一致。
std::string encode_source_db_reference(const SourceDbReference& reference);

/// 解析引用文件内容；格式不对时抛 std::invalid_argument。
SourceDbReference decode_source_db_reference(std::string_view content);

//: 引用文件走的是与 Word 源文件同一套归档、过期与清理链路，所以复用同一份元数据结构。
constexpr std::string_view kSourceDbReferenceExtension = ".srcref";

/// 用引用文件的内容算出归档元数据。original_file_name 取离线库的文件名，
/// 好让导入列表里一眼能认出这条记录来自哪份库。
WordInputMetadata describe_source_db_reference(
    std::string_view content, const std::string& original_file_name);

}  // namespace bridge_report::archive
