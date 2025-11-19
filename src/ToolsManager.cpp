#include "ToolsManager.hpp"
#include "tools/CollectionTools.hpp"
#include "tools/DateTools.hpp"
#include "tools/StringTools.hpp"
#include <algorithm>
#include <mutex>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <iostream>

#include "tools/TemplateTools.hpp"

using namespace std;
using namespace jz;

ToolsManager &ToolsManager::instance() {
    static ToolsManager inst;
    static bool _toolsInitialized = false;
    if (!_toolsInitialized) {
        _toolsInitialized = true;
        CollectionTools::init();
        DateTools::init();
        TemplateTools::init();
        StringTools::init();
    }
    return inst;
}

void ToolsManager::register_tool(const std::string &name, ToolFunction fn) {
    unique_lock locker(_registryMutex);
    _registry[name] = std::move(fn);
}

void ToolsManager::register_tool(const std::string &name, const std::shared_ptr<ToolObject> &tool) {
    std::unique_lock locker(_registryMutex);
    _registry[name] = [tool](const ordered_json &input, const ordered_json &options,
                             const ordered_json &ctx, json &metadata) mutable {
        return (*tool)(input, options, ctx, metadata);
    };
}

ordered_json ToolsManager::run_tool(const std::string &name, const ordered_json &input, const ordered_json &options,
                                    const ordered_json &ctx, json &metadata) {
    shared_lock locker(_registryMutex);
    const auto it = _registry.find(name);
    if (it == _registry.end()) {
        throw std::runtime_error("Unknown tool: " + name);
    }
    return it->second(input, options, ctx, metadata);
}

bool ToolsManager::has_tool(const std::string &name) {
    shared_lock locker(_registryMutex);
    return _registry.contains(name);
}

