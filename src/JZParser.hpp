#pragma once

#include <string>
#include <string_view>
#include <stdexcept>
#include <nlohmann/json.hpp>

using std::string;
using std::string_view;
using std::runtime_error;
using ordered_json = nlohmann::ordered_json;

namespace jz {
    /*
     JZError
     - extends std::runtime_error for compatibility with std::exception catchers
     - stores line and column (1-based) and provides formatted what()
    */
    struct JZError final : public runtime_error {
    public:
        explicit JZError(string_view msg, size_t line_ = 1, size_t col_ = 1);

        explicit JZError(const string &msg, const string &json);

        // JZError(const std::string &msg, size_t line_ = 1, size_t col_ = 1);

        // override what() to include location; noexcept per std::exception
        // [[nodiscard]] const char *what() const noexcept override;

        [[nodiscard]] size_t line() const noexcept { return _line; }
        [[nodiscard]] size_t column() const noexcept { return _column; }
        [[nodiscard]] string_view json() const noexcept { return _json; }

    private:
        size_t _line;
        size_t _column;
        string _json;
        // std::string full_msg; // cached message returned by what()
    };

    /*
     Processor
     - static utility class that processes a JZ template (string) producing JSON output.
     - public API: to_json which accepts input as std::string_view and the input data as ordered_json.
     - many helper methods are static and internal-use; kept public static to match previous usage style.
    */
    struct Processor {
        // Public API: convert a jz template (jz_input) into string, using `data` as the input context.
        // Throws JZError on parse/eval/formatting errors.
        static string to_string(string_view jz_input, const ordered_json &data);

        // Public API: convert a jz template (jz_input) into JSON, using `data` as the input context.
        // Throws JZError on parse/eval/formatting errors.
        static ordered_json to_json(std::string_view jz_input, const ordered_json &data);

        // --- Utilities used internally (but kept public static for testability) ---

        // Comment removal (handles // and /* */ and respects strings)
        static std::string remove_comments(std::string_view s);

        // Return whether a char is allowed as identifier start/part
        static bool is_identifier_start(char c) noexcept;

        static bool is_identifier_part(char c) noexcept;

        // Convert single-quoted strings to double-quoted JSON strings
        static std::string convert_single_quoted_strings(std::string_view s);

        // Quote unquoted object keys in object contexts ("foo: 1" -> "\"foo\": 1")
        static std::string quote_unquoted_keys(std::string_view s);

        // Remove trailing commas (before ']' or '}')
        static std::string remove_trailing_commas(std::string_view s);

        // Convenience: normalize JSON5-like input to strict JSON
        static std::string normalize_json5_to_json(std::string_view s);

        // Placeholder/template replacement:
        static std::string replace_placeholders(std::string_view s, const ordered_json &data);

        // Recursively remove 'undefined' sentinel nodes from JSON (both objects and arrays)
        static void remove_undefined_sentinels(ordered_json &j);
    };
} // namespace jz
