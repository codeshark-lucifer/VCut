#pragma once

#include <optional>
#include <string>

#include "core/Project.hpp"

namespace vcut {

bool saveProject(const Project& project, const std::string& path, std::string* error = nullptr);
std::optional<Project> loadProject(const std::string& path, std::string* error = nullptr);

}  // namespace vcut
