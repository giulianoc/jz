#include "StringTools.hpp"
#include "../ToolsManager.hpp"

#include <iostream>
#include <sstream>

using namespace std;
using namespace jz;

void StringTools::init() {
    ToolsManager &tm = ToolsManager::instance();
    // register default tools: upper, lower, capitalize
    tm.register_tool("upper", upper);
    tm.register_tool("lower", lower);
    tm.register_tool("capitalize", capitalize);
}

/**
 * Convert strings to uppercase.
 *
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation (see traverse).
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The transformed JSON structure with strings converted to uppercase.
 */
ordered_json StringTools::upper(const ordered_json &input, const ordered_json &options, const ordered_json &ctx, json &metadata) {
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
ordered_json StringTools::lower(const ordered_json &input, const ordered_json &options, const ordered_json &ctx, json &metadata) {
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
ordered_json StringTools::capitalize(const ordered_json &input, const ordered_json &options, const ordered_json &ctx, json &metadata) {
    // cerr << "capitalize:" << input.dump() << endl;
    bool firstOnly = false, forceLower = true;
    if (options.is_object()) {
        if (options.contains("firstOnly") && options["firstOnly"].is_boolean())
            firstOnly = options["firstOnly"].get<bool>();
        if (options.contains("forceLower") && options["forceLower"].is_boolean())
            forceLower = options["forceLower"].get<bool>();
    }
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
 * Traverse the input JSON structure and apply the operation to strings according to options.
 *
 * @param operation The string operation to apply.
 * @param input The input JSON structure.
 * @param options Options dictating how to traverse and apply the operation:
 *                  traverse: "dump" (default, just dump the input to string), "array", "object", "both"
 *                  kv: "both" (default), "key", "value" (only for object traversal)
 *                  convert: bool (default: false) - whether to convert non-string values to string before applying operation
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

    // Extract traverse mode (default: "dump")
    string traverse_mode = "dump";
    if (options.is_object() && options.contains("traverse") && options["traverse"].is_string())
        traverse_mode = options["traverse"].get<string>();

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
        string kv_mode = "both";
        if (options.is_object() && options.contains("kv") && options["kv"].is_string())
            kv_mode = options["kv"].get<string>();

        ordered_json object = ordered_json::object();
        for (const auto &[key, value]: input.items()) {
            string new_key = key;
            if (kv_mode == "key" || kv_mode == "both") {
                new_key = operation(new_key);
            }

            if (kv_mode == "value" || kv_mode == "both") {
                object[new_key] = _traverse(operation, value, options, ctx);
            } else {
                object[new_key] = value;
            }
        }
        return object;
    }

    bool convert = false;
    if (options.is_object() && options.contains("convert"))
        convert = options.at("convert").get<bool>();

    if (convert) {
        // dump to string and transform it
        return operation(input.dump());
    }

    return input;
}
