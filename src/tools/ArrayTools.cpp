#include "ArrayTools.hpp"
#include "../ToolsManager.hpp"

#include <sstream>

using namespace std;
using namespace jz;

void ArrayTools::init() {
    ToolsManager &tm = ToolsManager::instance();
    // register default tools: upper, lower, capitalize
    tm.register_tool("length", length);
}

ordered_json ArrayTools::length(const ordered_json &input, const ordered_json &options, const ordered_json &ctx) {
    if (input.is_null()) return 0; // ...oppure nullptr?
    if (input.is_string())
        return input.get<string>().size();
    if (input.is_array())
        return input.size();
    // TODO: dare eccezione perché ci aspettiamo una stringa?
    return nullptr;
}
