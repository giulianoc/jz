#include "CollectionTools.hpp"
#include "../ToolsManager.hpp"

using namespace std;
using namespace jz;

void CollectionTools::init() {
    ToolsManager &tm = ToolsManager::instance();
    tm.register_tool("length", length);
}

/**
 * Get the length of a string, array or object.
 *
 * @param input The input JSON value (string, array or object).
 * @param options Options dictating default value if input is not string/array:
 *                  default: number (default: none) - the default length value to return if input is not string/array/object.
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The length of the string, array or object, or nullptr/default if input is null or not a string/array/object.
 */
ordered_json CollectionTools::length(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                     json &metadata) {
    if (input.is_string())
        return input.get<string>().size();
    if (input.is_array() || input.is_object())
        return input.size();
    if (options.is_object() && options.contains("default") && options["default"].is_number())
        return options["default"].get<long long>();
    return nullptr;
}
