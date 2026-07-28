#pragma once

#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

int parse_rating_tree_page_limit(const std::string& value);
int parse_rating_tree_page_offset(const std::string& value);
const std::vector<std::string>& rating_tree_route_methods();

void register_rating_tree_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
