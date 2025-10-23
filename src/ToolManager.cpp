#include "ToolManager.hpp"
#include "tools/DateTools.hpp"
#include "tools/StringTools.hpp"
#include <algorithm>
#include <locale>
#include <sstream>
#include <stdexcept>

#include <iostream>

using namespace std;
using namespace jz;

static string normalize_tool_name(const string& s) {
    string out = s;
    ranges::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

ToolManager& ToolManager::instance() {
    static ToolManager inst;
	static bool _toolsInitialized = false;
	if (!_toolsInitialized)
	{
		_toolsInitialized = true;
		StringTools::init();
		DateTools::init();
	}
	return inst;
}

ToolManager::ToolManager() = default;

void ToolManager::register_tool(const std::string& name, ToolFunction fn) {
    const string key = normalize_tool_name(name);
    registry_[key] = std::move(fn);
}

ordered_json ToolManager::run_tool(const std::string& name, const ordered_json& input, const ordered_json& options, const ordered_json& ctx) const {
    const string key = normalize_tool_name(name);
    const auto it = registry_.find(key);
    if (it == registry_.end()) {
        throw std::runtime_error("Unknown tool: " + name);
    }
    return it->second(input, options, ctx);
}

bool ToolManager::has_tool(const std::string& name) const {
    const string key = normalize_tool_name(name);
    return registry_.contains(key);
}