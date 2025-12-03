#include "StringTools.hpp"
#include "../ToolsManager.hpp"

#include <iostream>
#include <format>
#include <sstream>

using namespace std;
using namespace jz;

void StringTools::init() {
    ToolsManager &tm = ToolsManager::instance();
    // register default tools: upper, lower, capitalize
    tm.register_tool("upper", upper);
    tm.register_tool("lower", lower);
    tm.register_tool("capitalize", capitalize);
    tm.register_tool("trim", trim);
}

// TODO: toString, split, join, replace, substring, regexReplace, etc.

/**
 * Convert strings to uppercase.
 *
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation (see traverse).
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The transformed JSON structure with strings converted to uppercase.
 */
ordered_json StringTools::upper(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                json &metadata) {
    // cerr << "upper:" << input.dump() << endl;
    auto operation = [](string s) {
        ranges::transform(s, s.begin(), [](const unsigned char c) { return toupper(c); });
        return s;
    };
    return _traverse(operation, input, options, ctx);
}

/**
 * Convert strings to lowercase.
 *
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation (see traverse).
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The transformed JSON structure with strings converted to lowercase.
 */
ordered_json StringTools::lower(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                json &metadata) {
    // cerr << "lower:" << input.dump() << endl;
    auto operation = [](string s) {
        ranges::transform(s, s.begin(), [](const unsigned char c) { return tolower(c); });
        return s;
    };
    return _traverse(operation, input, options, ctx);
}

/**
 * Capitalize the first letter of each word in strings.
 *
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation:
 *                  firstOnly: bool (default: false) - whether to capitalize only the first character of the string
 *                  forceLower: bool (default: true) - whether to lowercase the entire string before capitalizing
 *                  (see traverse for other options)
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The transformed JSON structure with strings capitalized.
 */
ordered_json StringTools::capitalize(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                     json &metadata) {
    // cerr << "capitalize:" << input.dump() << endl;
    const bool firstOnly = ToolsManager::get_option(options, "firstOnly", false);
    const bool forceLower = ToolsManager::get_option(options, "forceLower", true);

    auto operation = [firstOnly, forceLower](string s) {
        if (s.empty()) return s;

        string result(s);

        // Optionally lowercase the entire string first
        if (forceLower) {
            ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        }

        bool newWord = true;
        for (auto &ch: result) {
            if (isalpha(static_cast<unsigned char>(ch))) {
                if (newWord) {
                    ch = static_cast<char>(toupper(ch));
                    if (firstOnly) break; // only the very first character
                    newWord = false;
                }
            } else {
                // any non-letter separates words
                newWord = true;
            }
        }

        return result;
    };
    return _traverse(operation, input, options, ctx);
}

/**
 * Trim whitespace from the beginning and end of strings.
 *
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation:
 *                  side: "both" (default), "left", "right" - which side(s) to trim
 *                  (see traverse for other options)
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The transformed JSON structure with strings trimmed.
 */
ordered_json StringTools::trim(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                               json &metadata) {
    const string side = ToolsManager::get_option(options, "side", string("both"));

    auto operation = [side](string s) {
        if (s.empty()) return s;

        auto is_space = [](unsigned char c) { return std::isspace(c); };
        if (side == "left" || side == "both") {
            s.erase(s.begin(), ranges::find_if_not(s, is_space));
        }

        if (side == "right" || side == "both") {
            s.erase(std::find_if_not(s.rbegin(), s.rend(), is_space).base(), s.end());
        }

        return s;
    };
    return _traverse(operation, input, options, ctx);
}

/**
 * Get the directory name from file paths in strings.
 *
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation:
 *                  separator: char (default: '/') - the path separator
 *                  onlyIfFilenameContains: string (optional) - only apply dirname if the filename contains this substring
 *                  (see traverse for other options)
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The transformed JSON structure with directory names.
 */
ordered_json StringTools::dirname(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                  json &metadata) {
    const char separator = ToolsManager::get_option(options, "separator", '/');
    const optional<string> filenameContains = ToolsManager::get_option<string>(options, "onlyIfFilenameContains");

    auto operation = [separator, filenameContains](const string &path) {
        if (path.empty()) return std::format("{}", separator);
        const auto pos = path.rfind(separator);
        const std::string last_segment =
                (pos == std::string::npos ? path : path.substr(pos + 1));
        if (filenameContains) {
            if (last_segment.find(filenameContains.value()) != std::string::npos) {
                return path.substr(0, pos + 1);
            }
            return path;
        }
        return path.substr(0, pos + 1);
    };
    return _traverse(operation, input, options, ctx);
}


/**
 * Traverse the input JSON structure and apply the operation to strings according to options.
 *
 * @param operation The string operation to apply.
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation:
 *                  traverse: "none" (just dump the input to string), "array", "object", "both" (default: "both")
 *                  kv: "both" (default), "key", "value" (only for object traversal)
 *                  toString: bool (default: false) - whether to convert non-string values to string before applying operation
 * @param ctx Context (not used in this function).
 * @return The transformed JSON structure.
 */
ordered_json StringTools::_traverse(const function<string(string)> &operation,
                                    const ordered_json &input,
                                    const ordered_json &options,
                                    const ordered_json &ctx) {
    // Handle null input
    if (input.is_null())
        return nullptr;

    // Handle string
    if (input.is_string()) {
        return operation(input.get<string>());
    }

    // Extract traverse mode (default: "both")
    const string traverse_mode = ToolsManager::get_option(options, "traverseMode", string("both"));

    // Handle arrays
    if ((traverse_mode == "array" || traverse_mode == "both") && input.is_array()) {
        ordered_json array = ordered_json::array();
        for (const auto &el: input) {
            array.push_back(_traverse(operation, el, options, ctx));
        }
        return array;
    }

    // Handle objects
    if ((traverse_mode == "object" || traverse_mode == "both") && input.is_object()) {
        const bool applyToKeys = ToolsManager::get_option(options, "applyToKeys", false);
        const bool applyToValues = ToolsManager::get_option(options, "applyToValues", true);
        ordered_json object = ordered_json::object();
        for (const auto &[key, value]: input.items()) {
            string new_key = key;
            if (applyToKeys) {
                new_key = operation(new_key);
            }

            if (applyToValues) {
                object[new_key] = _traverse(operation, value, options, ctx);
            } else {
                object[new_key] = value;
            }
        }
        return object;
    }

    if (ToolsManager::get_option(options, "convertAllToString", false)) {
        // dump to string and transform it
        return operation(input.dump());
    }

    return input;
}
