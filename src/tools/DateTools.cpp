#include <format>
#include <chrono>
#include "../ToolsManager.hpp"
#include "DateTools.hpp"

#include <iostream>

using namespace std;
using namespace std::chrono;
using namespace jz;

void DateTools::init() {
    ToolsManager &tm = ToolsManager::instance();
    tm.register_tool("dateFormat", dateFormat);
    tm.register_tool("millis", millis);
}

/**
 * Format a date given in milliseconds since epoch.
 *
 * @param input The input JSON value representing milliseconds since epoch (number or string).
 * @param options Options dictating the format string:
 *                  format: string (default: "%Y-%m-%d %H:%M:%S") - the date format string.
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The formatted date string.
 */
ordered_json DateTools::dateFormat(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                   json &metadata) {
    // input
    long long millis_since_epoch = 0;
    if (input.is_number_integer() || input.is_number_unsigned()) {
        millis_since_epoch = input.get<long long>();
    } else if (input.is_number_float()) {
        millis_since_epoch = static_cast<long long>(input.get<double>());
    } else if (input.is_string()) {
        try { millis_since_epoch = stoll(input.get<string>()); } catch (...) { return nullptr; }
    } else {
        return nullptr;
    }

    // options
    string fmt = "%Y-%m-%dT%H:%M:%SZ"; // default format
    string precision = "seconds";
    if (options.is_object()) {
        if (options.contains("format") && options["format"].is_string())
            fmt = options["format"].get<string>();
        if (options.contains("precision") && options["precision"].is_string())
            precision = options["precision"].get<string>();
    }
    const string &f = std::format("{{:{}}}", fmt);

    // Build time_point from milliseconds
    const milliseconds ms{millis_since_epoch};
    system_clock::time_point tp{ms};

    if (precision == "millis" || precision == "milliseconds") {
        return std::vformat(f, std::make_format_args(tp));
    }
    if (precision == "seconds") {
        const auto _tp = floor<seconds>(tp);
        return std::vformat(f, std::make_format_args(_tp));
    }
    if (precision == "minutes") {
        const auto _tp = floor<minutes>(tp);
        return std::vformat(f, std::make_format_args(_tp));
    }
    if (precision == "hours") {
        const auto _tp = floor<hours>(tp);
        return std::vformat(f, std::make_format_args(_tp));
    }
    if (precision == "days") {
        const auto _tp = floor<days>(tp);
        return std::vformat(f, std::make_format_args(_tp));
    }

    throw std::runtime_error(std::format("precision '{}' is not supported", precision));
}

/**
 * Convert input to milliseconds since epoch.
 *
 * @param input The input JSON value (number or string).
 * @param options Options dictating default value if conversion fails:
 *                  default: number (default: none) - the default milliseconds value to return if conversion fails.
 * @param ctx Context (not used in this function).
 * @param metadata Metadata (not used in this function).
 * @return The milliseconds since epoch as a long long integer.
 */
ordered_json DateTools::millis(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                               json &metadata) {
    if (input.is_number()) return input.get<long long>();
    try {
        return stoll(input.dump());
    } catch (...) {
        if (options.is_object()) {
            if (options.contains("default") && options["default"].is_number())
                return options["default"].get<long long>();
        }
        throw;
    }
}
