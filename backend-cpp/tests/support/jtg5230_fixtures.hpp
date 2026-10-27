#pragma once

#include <filesystem>
#include <stdexcept>
#include <utility>

#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace bridge_report::tests::jtg5230 {

inline std::filesystem::path package_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "standards/technical-condition/jtg-t-5230-2026/1.0.0";
}

inline standards::StandardPackage load_package() {
    standards::StandardPackageLoader loader;
    auto loaded = loader.load(package_root());
    if (!loaded.ok()) {
        throw std::runtime_error("unable to load JTG/T 5230 package");
    }
    return std::move(*loaded.package);
}

}  // namespace bridge_report::tests::jtg5230
