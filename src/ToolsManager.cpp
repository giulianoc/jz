#include "ToolsManager.hpp"
#include "tools/ArrayTools.hpp"
#include "tools/DateTools.hpp"
#include "tools/StringTools.hpp"
#include <algorithm>
#include <mutex>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <iostream>

using namespace std;
using namespace jz;

ToolsManager &ToolsManager::instance() {
    static ToolsManager inst;
    static bool _toolsInitialized = false;
    if (!_toolsInitialized) {
        _toolsInitialized = true;
        ArrayTools::init();
        DateTools::init();
        StringTools::init();
    }
    return inst;
}

void ToolsManager::register_tool(const std::string &name, ToolFunction fn) {
    const string key = normalize_tool_name(name);
    unique_lock locker(_registryMutex);
    _registry[key] = std::move(fn);
}

ordered_json ToolsManager::run_tool(const std::string &name, const ordered_json &input, const ordered_json &options,
                                    const ordered_json &ctx) {
    const string key = normalize_tool_name(name);
    shared_lock locker(_registryMutex);
    const auto it = _registry.find(key);
    if (it == _registry.end()) {
        throw std::runtime_error("Unknown tool: " + name);
    }
    return it->second(input, options, ctx);
}

bool ToolsManager::has_tool(const std::string &name) {
    const string key = normalize_tool_name(name);
    shared_lock locker(_registryMutex);
    return _registry.contains(key);
}

string ToolsManager::normalize_tool_name(const string &s) {
    string out = s;
    ranges::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}


