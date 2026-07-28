#include "bridge_report/rating_tree/RatingTreePackageLoader.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <unordered_map>
#include <utility>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::rating_tree {

namespace {

RatingTreeIssue issue(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

bool read_json(
    const std::filesystem::path& path,
    Json::Value& document,
    RatingTreeIssue& read_issue) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        read_issue = issue("rating_tree_file_missing", "评定树包缺少声明的 JSON 文件。");
        return false;
    }
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &document, &errors) ||
        !document.isObject()) {
        read_issue = issue("rating_tree_json_invalid", "评定树包包含无效 JSON 对象。");
        return false;
    }
    return true;
}

bool safe_json_path(const std::string& raw_path) {
    const std::filesystem::path path(raw_path);
    if (raw_path.empty() || path.is_absolute() || path.has_root_name() ||
        path.extension() != ".json") {
        return false;
    }
    for (const auto& part : path.lexically_normal()) {
        if (part == "..") return false;
    }
    return true;
}

std::optional<std::vector<std::string>> entry_files(
    const Json::Value& manifest,
    RatingTreeIssue& parse_issue) {
    if (!manifest["entry_files"].isArray() || manifest["entry_files"].empty()) {
        parse_issue = issue(
            "rating_tree_entry_files_invalid",
            "评定树清单必须声明非空 JSON 入口文件列表。");
        return std::nullopt;
    }
    std::set<std::string> unique;
    std::vector<std::string> result;
    for (const auto& value : manifest["entry_files"]) {
        if (!value.isString() || !safe_json_path(value.asString())) {
            parse_issue = issue(
                "rating_tree_entry_file_unsafe",
                "评定树入口必须是包内安全的相对 JSON 路径。");
            return std::nullopt;
        }
        const auto normalized =
            std::filesystem::path(value.asString()).generic_string();
        if (!unique.insert(normalized).second) {
            parse_issue = issue(
                "rating_tree_entry_file_duplicate", "评定树入口文件不能重复。");
            return std::nullopt;
        }
        result.push_back(normalized);
    }
    return result;
}

std::string canonical_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    return Json::writeString(builder, value);
}

void append_digest(
    std::string& input,
    const std::string& name,
    const std::string& content) {
    input += std::to_string(name.size()) + ":" + name;
    input += std::to_string(content.size()) + ":" + content;
}

bool non_empty_string(
    const Json::Value& value,
    const char* key,
    std::string& target) {
    if (!value[key].isString() || value[key].asString().empty()) return false;
    target = value[key].asString();
    return true;
}

std::optional<std::vector<std::string>> string_array(const Json::Value& value) {
    if (!value.isArray()) return std::nullopt;
    std::vector<std::string> result;
    for (const auto& item : value) {
        if (!item.isString() || item.asString().empty()) return std::nullopt;
        result.push_back(item.asString());
    }
    return result;
}

bool parse_node(
    const Json::Value& value,
    RatingTreeExtensionNode& node,
    RatingTreeIssue& parse_issue) {
    std::string node_type;
    std::string scoring_mode;
    if (!value.isObject() || !non_empty_string(value, "id", node.id) ||
        !non_empty_string(value, "display_name", node.display_name) ||
        !non_empty_string(value, "node_type", node_type) ||
        !non_empty_string(value, "scoring_mode", scoring_mode) ||
        !value["sort_order"].isInt() || !value["is_selectable"].isBool()) {
        parse_issue = issue(
            "rating_tree_node_invalid", "评定树节点缺少必填字段或字段类型错误。");
        return false;
    }
    const auto parsed_node_type = parse_rating_tree_node_type(node_type);
    const auto parsed_scoring_mode = parse_rating_tree_scoring_mode(scoring_mode);
    const auto bridges = string_array(value["bridge_type_ids"]);
    const auto components = string_array(value["component_category_ids"]);
    if (!parsed_node_type.has_value() || !parsed_scoring_mode.has_value() ||
        !bridges.has_value() || !components.has_value()) {
        parse_issue = issue(
            "rating_tree_node_invalid", "评定树节点包含不支持的类型、模式或适用范围。");
        return false;
    }
    node.node_type = *parsed_node_type;
    node.scoring_mode = *parsed_scoring_mode;
    node.sort_order = value["sort_order"].asInt();
    node.is_selectable = value["is_selectable"].asBool();
    node.bridge_type_ids = *bridges;
    node.component_category_ids = *components;
    if (value.isMember("parent_id") && !value["parent_id"].isNull()) {
        if (!value["parent_id"].isString() || value["parent_id"].asString().empty()) {
            parse_issue = issue("rating_tree_node_invalid", "父节点 ID 必须为非空字符串。");
            return false;
        }
        node.parent_id = value["parent_id"].asString();
    }
    if (value.isMember("h21_indicator_id") && !value["h21_indicator_id"].isNull()) {
        if (!value["h21_indicator_id"].isString() ||
            value["h21_indicator_id"].asString().empty()) {
            parse_issue = issue(
                "rating_tree_node_invalid", "H21 指标引用必须为非空字符串。");
            return false;
        }
        node.h21_indicator_id = value["h21_indicator_id"].asString();
    }
    if (value["organization_note"].isString()) {
        node.organization_note = value["organization_note"].asString();
    }
    if (value.isMember("source_ids")) {
        const auto sources = string_array(value["source_ids"]);
        if (!sources.has_value()) {
            parse_issue = issue("rating_tree_node_invalid", "节点来源必须为字符串数组。");
            return false;
        }
        node.source_ids = *sources;
    }
    if (node.node_type == RatingTreeNodeType::placeholder && node.is_selectable) {
        parse_issue = issue(
            "rating_tree_placeholder_selectable", "占位节点不能用于病害选择。");
        return false;
    }
    return true;
}

bool has_cycle(const std::map<std::string, RatingTreeExtensionNode>& nodes) {
    enum class State { unvisited, visiting, visited };
    std::unordered_map<std::string, State> states;
    const auto visit = [&](const auto& self, const std::string& id) -> bool {
        const auto state = states[id];
        if (state == State::visiting) return true;
        if (state == State::visited) return false;
        states[id] = State::visiting;
        const auto& node = nodes.at(id);
        if (node.parent_id.has_value() && nodes.contains(*node.parent_id) &&
            self(self, *node.parent_id)) {
            return true;
        }
        states[id] = State::visited;
        return false;
    };
    for (const auto& [id, _] : nodes) {
        if (visit(visit, id)) return true;
    }
    return false;
}

}  // namespace

RatingTreeChecksumResult RatingTreePackageLoader::calculate_checksum(
    const std::filesystem::path& package_root) const {
    Json::Value manifest;
    RatingTreeIssue read_issue;
    if (!read_json(package_root / "manifest.json", manifest, read_issue)) {
        return {std::nullopt, read_issue};
    }
    RatingTreeIssue entry_issue;
    auto files = entry_files(manifest, entry_issue);
    if (!files.has_value()) return {std::nullopt, entry_issue};
    std::sort(files->begin(), files->end());
    auto normalized_manifest = manifest;
    normalized_manifest.removeMember("content_checksum");
    normalized_manifest["entry_files"] = Json::Value(Json::arrayValue);
    for (const auto& file : *files) normalized_manifest["entry_files"].append(file);

    std::string digest_input;
    append_digest(digest_input, "manifest.json", canonical_json(normalized_manifest));
    for (const auto& file : *files) {
        Json::Value document;
        if (!read_json(package_root / file, document, read_issue)) {
            return {std::nullopt, read_issue};
        }
        append_digest(digest_input, file, canonical_json(document));
    }
    return {
        "sha256:" + bridge_report::auth::sha256_hex(digest_input),
        std::nullopt};
}

RatingTreeLoadResult RatingTreePackageLoader::load(
    const std::filesystem::path& package_root) const {
    RatingTreeLoadResult result;
    Json::Value manifest;
    RatingTreeIssue read_issue;
    if (!read_json(package_root / "manifest.json", manifest, read_issue)) {
        result.issues.push_back(read_issue);
        return result;
    }

    RatingTreeExtensionPackage package;
    const bool manifest_valid =
        manifest["package_type"].isString() &&
        manifest["package_type"].asString() == "rating_tree_extension" &&
        non_empty_string(manifest, "tree_code", package.manifest.tree_code) &&
        non_empty_string(manifest, "tree_name", package.manifest.tree_name) &&
        non_empty_string(manifest, "package_version", package.manifest.package_version) &&
        non_empty_string(manifest, "status", package.manifest.status) &&
        non_empty_string(
            manifest, "content_checksum", package.manifest.content_checksum) &&
        manifest["contract_version"].isInt();
    if (!manifest_valid) {
        result.issues.push_back(issue(
            "rating_tree_manifest_invalid", "评定树清单身份、版本或状态字段无效。"));
        return result;
    }
    package.manifest.contract_version = manifest["contract_version"].asInt();
    if (package.manifest.contract_version != supported_contract_version ||
        package.manifest.status != "active") {
        result.issues.push_back(issue(
            "rating_tree_manifest_unsupported", "评定树包接口版本或状态不受支持。"));
        return result;
    }
    RatingTreeIssue entry_issue;
    const auto files = entry_files(manifest, entry_issue);
    if (!files.has_value()) {
        result.issues.push_back(entry_issue);
        return result;
    }
    package.manifest.entry_files = *files;

    const auto checksum = calculate_checksum(package_root);
    if (!checksum.checksum.has_value() ||
        *checksum.checksum != package.manifest.content_checksum) {
        result.issues.push_back(issue(
            "rating_tree_checksum_mismatch", "评定树包内容摘要与清单不一致。"));
        return result;
    }

    for (const auto& file : *files) {
        Json::Value document;
        if (!read_json(package_root / file, document, read_issue)) {
            result.issues.push_back(read_issue);
            return result;
        }
        package.documents.emplace(file, document);
    }
    const auto tree_it = package.documents.find("tree.json");
    if (tree_it == package.documents.end() || !tree_it->second["nodes"].isArray()) {
        result.issues.push_back(issue(
            "rating_tree_nodes_invalid", "评定树包必须提供 tree.json nodes 数组。"));
        return result;
    }
    for (const auto& value : tree_it->second["nodes"]) {
        RatingTreeExtensionNode node;
        RatingTreeIssue parse_issue;
        if (!parse_node(value, node, parse_issue)) {
            result.issues.push_back(parse_issue);
            return result;
        }
        if (!package.nodes.emplace(node.id, std::move(node)).second) {
            result.issues.push_back(issue(
                "rating_tree_node_id_duplicate", "评定树节点稳定 ID 不能重复。"));
            return result;
        }
    }
    for (const auto& [_, node] : package.nodes) {
        if (node.parent_id.has_value() && !package.nodes.contains(*node.parent_id)) {
            result.issues.push_back(issue(
                "rating_tree_parent_missing", "评定树节点引用了不存在的父节点。"));
            return result;
        }
    }
    if (has_cycle(package.nodes)) {
        result.issues.push_back(issue("rating_tree_cycle", "评定树父子关系存在循环。"));
        return result;
    }

    const auto aliases_it = package.documents.find("aliases.json");
    if (aliases_it != package.documents.end()) {
        if (!aliases_it->second["aliases"].isArray()) {
            result.issues.push_back(issue(
                "rating_tree_aliases_invalid", "aliases.json 必须包含 aliases 数组。"));
            return result;
        }
        std::set<std::string> unique_aliases;
        for (const auto& value : aliases_it->second["aliases"]) {
            RatingTreeAlias alias;
            if (!value.isObject() || !non_empty_string(value, "alias", alias.alias) ||
                !non_empty_string(value, "target_node_id", alias.target_node_id) ||
                !non_empty_string(value, "bridge_type_id", alias.bridge_type_id) ||
                !non_empty_string(
                    value, "component_category_id", alias.component_category_id) ||
                !package.nodes.contains(alias.target_node_id)) {
                result.issues.push_back(issue(
                    "rating_tree_alias_invalid", "评定树别名字段或目标节点无效。"));
                return result;
            }
            const auto key = alias.bridge_type_id + "\n" +
                alias.component_category_id + "\n" + alias.alias;
            if (!unique_aliases.insert(key).second) {
                result.issues.push_back(issue(
                    "rating_tree_alias_conflict", "同一适用范围内的别名必须唯一。"));
                return result;
            }
            package.aliases.push_back(std::move(alias));
        }
    }

    const auto sources_it = package.documents.find("sources.json");
    if (sources_it != package.documents.end()) {
        if (!sources_it->second["sources"].isArray()) {
            result.issues.push_back(issue(
                "rating_tree_sources_invalid", "sources.json 必须包含 sources 数组。"));
            return result;
        }
        for (const auto& value : sources_it->second["sources"]) {
            RatingTreeSource source;
            if (!value.isObject() || !non_empty_string(value, "id", source.id) ||
                !non_empty_string(value, "source_type", source.source_type) ||
                !non_empty_string(value, "title", source.title)) {
                result.issues.push_back(issue(
                    "rating_tree_source_invalid", "评定树来源字段无效。"));
                return result;
            }
            if (value["reference"].isString()) {
                source.reference = value["reference"].asString();
            }
            if (!package.sources.emplace(source.id, std::move(source)).second) {
                result.issues.push_back(issue(
                    "rating_tree_source_duplicate", "评定树来源 ID 不能重复。"));
                return result;
            }
        }
    }
    for (const auto& [_, node] : package.nodes) {
        for (const auto& source_id : node.source_ids) {
            if (!package.sources.contains(source_id)) {
                result.issues.push_back(issue(
                    "rating_tree_source_missing", "评定树节点引用了不存在的来源。"));
                return result;
            }
        }
    }

    result.package = std::move(package);
    return result;
}

std::vector<std::filesystem::path> RatingTreePackageLoader::discover(
    const std::filesystem::path& standards_root) const {
    std::vector<std::filesystem::path> result;
    const auto tree_root = standards_root / "rating-tree";
    std::error_code error;
    if (!std::filesystem::is_directory(tree_root, error) || error) return result;
    for (std::filesystem::recursive_directory_iterator it(
             tree_root,
             std::filesystem::directory_options::skip_permission_denied,
             error), end;
         !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error) && !error &&
            it->path().filename() == "manifest.json") {
            result.push_back(it->path().parent_path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace bridge_report::rating_tree
