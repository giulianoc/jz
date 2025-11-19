#include "JZParser.hpp"
#include "ToolsManager.hpp" // assumed available in your project

#include <algorithm>
#include <charconv>
#include <format>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace std;
using ordered_json = nlohmann::ordered_json;

namespace jz {
    /* -------------------------
       JZError implementation
       ------------------------- */
    JZError::JZError(const string_view msg, const size_t line_, const size_t col_)
        : runtime_error(std::format("{} (line {}, column {})", std::string(msg), line_, col_)),
          _line(line_ > 0 ? line_ : 0), _column(col_ > 0 ? col_ : 0) {
        // full_msg = std::format("{} (line {}, column {})\n{}", std::string(msg), line, column);
    }

    JZError::JZError(const string &msg, const string &json)
        : runtime_error(msg), _line(0), _column(0), _json(json) {
    }

    // JZError::JZError(const std::string &msg, size_t line_, size_t col_)
    //     : JZError(string_view(msg), line_, col_) {
    // }

    // const char *JZError::what() const noexcept {
    //     return full_msg.c_str();
    // }
    //
    /* -------------------------
       Internal helpers
       ------------------------- */

    // sentinel key used to represent `undefined` inside templates
    static constexpr string_view UNDEF_KEY = "__jz_undefined__";

    // Create an ordered_json sentinel for undefined
    static ordered_json undefined_sentinel() {
        ordered_json o = ordered_json::object();
        o[string(UNDEF_KEY)] = true;
        return o;
    }

    static bool is_undefined_sentinel(const ordered_json &j) noexcept {
        return j.is_object() && j.size() == 1 && j.contains(string(UNDEF_KEY)) &&
               j.at(string(UNDEF_KEY)).is_boolean() && j.at(string(UNDEF_KEY)).get<bool>() == true;
    }

    // safe whitespace test
    static bool is_space_char(const char c) noexcept {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    /* -------------------------
       Scanner with position tracking (1-based)
       ------------------------- */
    struct Scanner {
        std::string_view s; // view into source
        size_t i = 0; // current index (0-based)
        size_t line = 1; // current line (1-based)
        size_t col = 1; // current column (1-based)

        explicit Scanner(const std::string_view src) : s(src) {
        }

        bool eof() const noexcept { return i >= s.size(); }

        char peek(const size_t lookahead = 0) const noexcept {
            const size_t pos = i + lookahead;
            return (pos < s.size()) ? s[pos] : '\0';
        }

        // advance by one character and return it; update line/column
        char next() {
            if (eof()) return '\0';
            const char c = s[i++];
            if (c == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
            return c;
        }

        // advance n characters (used carefully)
        void advance(size_t n = 1) {
            while (n-- && !eof()) next();
        }

        size_t pos() const noexcept { return i; }

        // current line/column describing the character at position i (1-based)
        pair<size_t, size_t> position_before() const noexcept {
            return {line, col};
        }

        // position for previous char (useful when error happens after next())
        pair<size_t, size_t> position_prev() const noexcept {
            if (col > 1) return {line, col - 1};
            if (line == 1) return {1, 1};
            // if we haven't tracked per-line lengths, approximate: column 1 on previous line
            return {line - 1, 1};
        }
    };

    /* Helper to parse quoted string content given we are positioned after the opening delimiter.
       Returns extracted content (without surrounding quotes). Advances scanner to the char after closing delimiter.
       Throws JZError with position if unterminated.
    */
    /*
    static string scan_quoted_string(Scanner &sc, char delim) {
        string acc;
        bool esc = false;
        auto start_pos = sc.position_before();
        while (!sc.eof()) {
            char ch = sc.next();
            if (esc) {
                switch (ch) {
                    case 'b': acc.push_back('\b');
                        break;
                    case 'f': acc.push_back('\f');
                        break;
                    case 'n': acc.push_back('\n');
                        break;
                    case 'r': acc.push_back('\r');
                        break;
                    case 't': acc.push_back('\t');
                        break;
                    case 'u':
                        // preserve \uXXXX as literal sequence; copy backslash-u and the next 4 chars if available
                        acc.push_back('\\');
                        acc.push_back('u');
                        for (int k = 0; k < 4 && !sc.eof(); ++k) {
                            acc.push_back(sc.next());
                        }
                        break;
                    default:
                        acc.push_back(ch);
                        break;
                }
                esc = false;
                continue;
            }
            if (ch == '\\') {
                esc = true;
                continue;
            }
            if (ch == delim) {
                return acc;
            }
            acc.push_back(ch);
        }
        auto [ln, col] = start_pos;
        throw JZError("Unterminated quoted string", ln, col);
    }

    */
    /* -------------------------
       Processor static methods
       ------------------------- */

    bool Processor::is_identifier_start(char c) noexcept {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    }

    bool Processor::is_identifier_part(char c) noexcept {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    }

    /* remove_comments:
       - removes C-like line comments and block comments
       - respects string literals (single, double, backtick)
       - throws JZError if block comment is unterminated
    */
    string Processor::remove_comments(string_view s) {
        Scanner sc{s};
        string out;
        out.reserve(s.size());

        bool in_line_comment = false;
        bool in_block_comment = false;
        bool in_string = false;
        char string_delim = 0;
        bool escape = false;

        while (!sc.eof()) {
            const char c = sc.next();
            const char nextc = sc.peek();

            if (in_line_comment) {
                if (c == '\n') {
                    in_line_comment = false;
                    out.push_back(c); // preserve newline
                } else {
                    // skip character inside line comment
                }
                continue;
            }

            if (in_block_comment) {
                // preserve newlines inside block comments so line numbers remain aligned
                if (c == '\n') {
                    out.push_back('\n');
                    continue;
                }
                // handle CRLF: if we see '\r' and next is '\n', treat as newline
                if (c == '\r') {
                    if (nextc == '\n') {
                        // consume the '\n' in the next loop iteration (sc.next() will do it),
                        // but append a newline now so we preserve line count.
                        out.push_back('\n');
                    } else {
                        out.push_back('\n');
                    }
                    continue;
                }
                if (c == '*' && nextc == '/') {
                    sc.advance(); // skip '/'
                    in_block_comment = false;
                }
                // otherwise skip other chars inside comment (do not append)
                continue;
            }

            if (in_string) {
                out.push_back(c);
                if (escape) {
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == string_delim) {
                    in_string = false;
                    string_delim = 0;
                }
                continue;
            }

            // not in comment or string
            if (c == '/' && nextc == '/') {
                in_line_comment = true;
                sc.advance(); // consume second '/'
                continue;
            }
            if (c == '/' && nextc == '*') {
                in_block_comment = true;
                sc.advance(); // consume '*'
                continue;
            }
            if (c == '"' || c == '\'' || c == '`') {
                in_string = true;
                string_delim = c;
                out.push_back(c);
                continue;
            }

            out.push_back(c);
        }

        if (in_block_comment) {
            // unterminated block comment: report position at previous character
            auto [ln, col] = sc.position_prev();
            throw JZError("Unterminated block comment", ln, col);
        }

        return out;
    }

    /* convert_single_quoted_strings:
       - convert single-quoted strings to double-quoted JSON-compatible strings
       - preserves escape sequences
    */
    string Processor::convert_single_quoted_strings(string_view s) {
        Scanner sc{s};
        string out;
        out.reserve(s.size());

        bool in_string = false;
        char delim = 0;
        bool escape = false;

        while (!sc.eof()) {
            char c = sc.next();
            if (!in_string) {
                if (c == '"' || c == '\'') {
                    in_string = true;
                    delim = c;
                    // if single-quote, emit double-quote instead to make it valid JSON
                    out.push_back((delim == '\'') ? '"' : '"');
                    escape = false;
                    continue;
                }
                out.push_back(c);
                continue;
            }

            // inside string
            if (delim == '"') {
                out.push_back(c);
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == '"') {
                    in_string = false;
                    delim = 0;
                }
                continue;
            }

            // delim == '\'' (single quoted input) -> convert to double quoted
            if (escape) {
                // keep common escapes but ensure JSON-compatibility
                switch (c) {
                    case '\'': out.push_back('\'');
                        break;
                    case '"': out.append("\\\"");
                        break;
                    case '\\': out.append("\\\\");
                        break;
                    case 'n': out.append("\\n");
                        break;
                    case 'r': out.append("\\r");
                        break;
                    case 't': out.append("\\t");
                        break;
                    default:
                        out.push_back('\\');
                        out.push_back(c);
                        break;
                }
                escape = false;
                continue;
            }

            if (c == '\\') {
                escape = true;
                continue;
            }

            if (c == '\'') {
                // close single-quoted -> emit closing double-quote
                out.push_back('"');
                in_string = false;
                delim = 0;
                continue;
            }

            if (c == '"') {
                // escape double-quote inside single-quoted string
                out.append("\\\"");
            } else {
                out.push_back(c);
            }
        }

        if (in_string && delim == '\'') {
            auto [ln, col] = sc.position_prev();
            throw JZError("Unterminated single-quoted string", ln, col);
        }

        return out;
    }

    /* quote_unquoted_keys:
       - walk the content and, when inside an object expecting a key, quote identifier-like keys
         before a ':' character. Keeps whitespace intact.
    */
    string Processor::quote_unquoted_keys(string_view s) {
        string out;
        out.reserve(s.size());

        enum class Ctx { None, InObject, InArray };
        struct Frame {
            Ctx ctx;
            bool expecting_key;
        };
        vector<Frame> stack;

        bool in_string = false;
        char delim = 0;
        bool escape = false;

        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (in_string) {
                out.push_back(c);
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == delim) {
                    in_string = false;
                    delim = 0;
                }
                continue;
            }

            if (c == '"' || c == '\'') {
                in_string = true;
                delim = c;
                out.push_back(c);
                continue;
            }

            if (c == '{') {
                out.push_back(c);
                stack.push_back({Ctx::InObject, true});
                continue;
            }
            if (c == '[') {
                out.push_back(c);
                stack.push_back({Ctx::InArray, false});
                continue;
            }
            if (c == '}' || c == ']') {
                out.push_back(c);
                if (!stack.empty()) stack.pop_back();
                continue;
            }

            if (!stack.empty() && stack.back().ctx == Ctx::InObject) {
                if (stack.back().expecting_key) {
                    if (is_space_char(c)) {
                        out.push_back(c);
                        continue;
                    }
                    if (c == '"' || c == '\'') {
                        in_string = true;
                        delim = c;
                        out.push_back(c);
                        continue;
                    }
                    if (Processor::is_identifier_start(c)) {
                        size_t j = i + 1;
                        while (j < s.size() && Processor::is_identifier_part(s[j])) ++j;
                        size_t k = j;
                        while (k < s.size() && is_space_char(s[k])) ++k;
                        if (k < s.size() && s[k] == ':') {
                            out.push_back('"');
                            out.append(s.data() + i, j - i);
                            out.push_back('"');
                            // append whitespace between identifier end and ':'
                            if (k > j) out.append(s.data() + j, k - j);
                            out.push_back(':');
                            i = k; // main loop will increment i
                            stack.back().expecting_key = false;
                            continue;
                        }
                    }
                    out.push_back(c);
                    continue;
                } else {
                    out.push_back(c);
                    if (c == ',') stack.back().expecting_key = true;
                    continue;
                }
            }

            out.push_back(c);
        }

        return out;
    }

    /* remove_trailing_commas:
       - naive approach: when encountering ']' or '}', walk back and remove a comma immediately before closing
         (skipping whitespace).
    */
    string Processor::remove_trailing_commas(string_view s) {
        string out;
        out.reserve(s.size());

        bool in_string = false;
        char delim = 0;
        bool escape = false;

        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (in_string) {
                out.push_back(c);
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == delim) {
                    in_string = false;
                    delim = 0;
                }
                continue;
            }

            if (c == '"' || c == '\'') {
                in_string = true;
                delim = c;
                out.push_back(c);
                continue;
            }

            if (c == ']' || c == '}') {
                // remove a trailing comma before this bracket, skipping spaces
                size_t trim = out.size();
                while (trim > 0 && is_space_char(out[trim - 1])) --trim;
                if (trim > 0 && out[trim - 1] == ',') {
                    out.erase(out.begin() + static_cast<long>(trim - 1), out.end());
                }
                out.push_back(c);
                continue;
            }

            out.push_back(c);
        }

        return out;
    }

    string Processor::normalize_json5_to_json(const string_view s) {
        return remove_trailing_commas(quote_unquoted_keys(convert_single_quoted_strings(s)));
    }

    /* -------------------------
       Evaluation subsystem (expressions, lexer & parser)
       The expression system supports:
        - identifiers and path access (a.b[0].c)
        - literals (string, number, true/false/null/undefined)
        - boolean operators: ||, &&, !
        - equality/relational operators: ==, !=, <, >, <=, >=
        - nullish coalescing: ??
        - ternary: ?:
        - pipeline: |#toolName(opt=val){...}
       This is a compact, self-contained expression evaluator adapted from the original.
       ------------------------- */

    namespace eval {
        struct Value {
            bool missing = false;
            ordered_json j;
            static Value from_json(ordered_json v) { return Value{false, std::move(v)}; }
            static Value missing_value() { return Value{true, ordered_json(nullptr)}; }
        };

        static bool is_undefined(const Value &v) { return v.missing || is_undefined_sentinel(v.j); }
        static bool is_nullish(const Value &v) { return v.missing || is_undefined(v); }

        static bool is_truthy(const Value &v) {
            if (v.missing) return false;
            if (is_undefined(v)) return false;
            if (v.j.is_boolean()) return v.j.get<bool>();
            if (v.j.is_null()) return false;
            if (v.j.is_number()) {
                try { return v.j.get<double>() != 0.0; } catch (...) { return true; }
            }
            if (v.j.is_string()) return !v.j.get_ref<const string &>().empty();
            if (v.j.is_array() || v.j.is_object()) return true;
            return true;
        }

        static optional<double> to_number_opt(const Value &v) {
            if (v.missing) return nullopt;
            if (is_undefined(v)) return nullopt;
            if (v.j.is_number()) {
                try { return v.j.get<double>(); } catch (...) { return nullopt; }
            }
            if (v.j.is_boolean()) return v.j.get<bool>() ? 1.0 : 0.0;
            if (v.j.is_string()) {
                const string &s = v.j.get_ref<const string &>();
                if (s.empty()) return 0.0;
                try {
                    size_t idx = 0;
                    double d = stod(s, &idx);
                    if (idx == s.size()) return d;
                    return nullopt;
                } catch (...) { return nullopt; }
            }
            return nullopt;
        }

        static bool eq_values(const Value &a, const Value &b) {
            if (a.missing && b.missing) return true;
            if ((a.missing && is_undefined(b)) || (b.missing && is_undefined(a))) return true;
            if (is_undefined(a) && is_undefined(b)) return true;

            if (!a.missing && !is_undefined(a) && !b.missing && !is_undefined(b)) {
                if (a.j.type() == b.j.type()) return a.j == b.j;
            }

            auto an = to_number_opt(a);
            auto bn = to_number_opt(b);
            if (an && bn) return *an == *bn;

            string sa = a.missing ? string("missing") : (is_undefined(a) ? string("undefined") : a.j.dump());
            string sb = b.missing ? string("missing") : (is_undefined(b) ? string("undefined") : b.j.dump());
            return sa == sb;
        }

        static optional<bool> relational_compare(const Value &a, const Value &b, char op) {
            auto an = to_number_opt(a);
            auto bn = to_number_opt(b);
            if (an && bn) {
                double A = *an, B = *bn;
                switch (op) {
                    case '<': return A < B;
                    case '>': return A > B;
                    case 'l': return A <= B;
                    case 'g': return A >= B;
                    default: return nullopt;
                }
            }
            if (!a.missing && !is_undefined(a) && a.j.is_string() && !b.missing && !is_undefined(b) && b.j.
                is_string()) {
                const string &A = a.j.get_ref<const string &>();
                const string &B = b.j.get_ref<const string &>();
                switch (op) {
                    case '<': return A < B;
                    case '>': return A > B;
                    case 'l': return A <= B;
                    case 'g': return A >= B;
                    default: return nullopt;
                }
            }
            return nullopt;
        }

        /* Token, Lexer, Parser:
           compact implementation focusing on clarity and position tracking where errors are thrown.
        */
        struct Token {
            enum Type {
                T_EOF, T_IDENTIFIER, T_NUMBER, T_STRING, T_TRUE, T_FALSE, T_NULL, T_UNDEFINED,
                T_QMARK, T_COLON, T_DOT, T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_LBRACE, T_RBRACE,
                T_COMMA, T_PIPE, T_HASH, T_OR, T_AND, T_NOT, T_NULLISH, T_EQ, T_NE, T_GT, T_LT, T_GTE, T_LTE, T_ASSIGN
            };

            Type type;
            string text;
            size_t line = 1;
            size_t col = 1;
        };

        struct Lexer {
            string_view s;
            size_t i = 0;
            size_t line = 1;
            size_t col = 1;

            explicit Lexer(string_view src) : s(src) {
            }

            static bool is_id_start(char c) { return isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
            static bool is_id_part(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

            void skip_ws() {
                while (i < s.size()) {
                    char c = s[i];
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
                        ++i;
                        ++col;
                        continue;
                    }
                    if (c == '\n') {
                        ++i;
                        ++line;
                        col = 1;
                        continue;
                    }
                    break;
                }
            }

            Token make_token(const Token::Type t, string text = {}) const {
                Token tok;
                tok.type = t;
                tok.text = std::move(text);
                tok.line = line;
                tok.col = col;
                return tok;
            }

            Token next() {
                skip_ws();
                if (i >= s.size()) return make_token(Token::T_EOF);

                // helper to peek ahead
                auto peek = [&](size_t k = 0) -> char { return (i + k < s.size()) ? s[i + k] : '\0'; };

                char c = s[i];

                // two-char tokens
                if (c == '?' && peek(1) == '?') {
                    // nullish
                    size_t l = line, co = col;
                    i += 2;
                    col += 2;
                    return Token{Token::T_NULLISH, "??", l, co};
                }
                if (c == '|' && peek(1) == '|') {
                    size_t l = line, co = col;
                    i += 2;
                    col += 2;
                    return Token{Token::T_OR, "||", l, co};
                }
                if (c == '&' && peek(1) == '&') {
                    size_t l = line, co = col;
                    i += 2;
                    col += 2;
                    return Token{Token::T_AND, "&&", l, co};
                }
                if (c == '=' && peek(1) == '=') {
                    size_t l = line, co = col;
                    i += 2;
                    col += 2;
                    return Token{Token::T_EQ, "==", l, co};
                }
                if (c == '!' && peek(1) == '=') {
                    size_t l = line, co = col;
                    i += 2;
                    col += 2;
                    return Token{Token::T_NE, "!=", l, co};
                }
                if (c == '>' && peek(1) == '=') {
                    size_t l = line, co = col;
                    i += 2;
                    col += 2;
                    return Token{Token::T_GTE, ">=", l, co};
                }
                if (c == '<' && peek(1) == '=') {
                    size_t l = line, co = col;
                    i += 2;
                    col += 2;
                    return Token{Token::T_LTE, "<=", l, co};
                }

                // single char tokens
                auto consume_single = [&](Token::Type t) -> Token {
                    Token tok;
                    tok.type = t;
                    tok.line = line;
                    tok.col = col;
                    ++i;
                    ++col;
                    return tok;
                };

                switch (c) {
                    case '?': return consume_single(Token::T_QMARK);
                    case ':': return consume_single(Token::T_COLON);
                    case '.': return consume_single(Token::T_DOT);
                    case '(': return consume_single(Token::T_LPAREN);
                    case ')': return consume_single(Token::T_RPAREN);
                    case '[': return consume_single(Token::T_LBRACKET);
                    case ']': return consume_single(Token::T_RBRACKET);
                    case '{': return consume_single(Token::T_LBRACE);
                    case '}': return consume_single(Token::T_RBRACE);
                    case ',': return consume_single(Token::T_COMMA);
                    case '|': return consume_single(Token::T_PIPE);
                    case '#': return consume_single(Token::T_HASH);
                    case '!': return consume_single(Token::T_NOT);
                    case '>': return consume_single(Token::T_GT);
                    case '<': return consume_single(Token::T_LT);
                    case '=': return consume_single(Token::T_ASSIGN);
                }

                // strings
                if (c == '"' || c == '\'') {
                    size_t start_line = line, start_col = col;
                    char delim = c;
                    ++i;
                    ++col; // skip delim
                    string acc;
                    bool esc = false;
                    while (i < s.size()) {
                        char ch = s[i++];
                        if (ch == '\n') {
                            ++line;
                            col = 1;
                        } else ++col;
                        if (esc) {
                            switch (ch) {
                                case '"': acc.push_back('"');
                                    break;
                                case '\\': acc.push_back('\\');
                                    break;
                                case '/': acc.push_back('/');
                                    break;
                                case 'b': acc.push_back('\b');
                                    break;
                                case 'f': acc.push_back('\f');
                                    break;
                                case 'n': acc.push_back('\n');
                                    break;
                                case 'r': acc.push_back('\r');
                                    break;
                                case 't': acc.push_back('\t');
                                    break;
                                case 'u':
                                    // copy \uXXXX as-is
                                    acc.push_back('\\');
                                    acc.push_back('u');
                                    for (int k = 0; k < 4 && i < s.size(); ++k) {
                                        acc.push_back(s[i++]);
                                        ++col;
                                    }
                                    break;
                                default: acc.push_back(ch);
                                    break;
                            }
                            esc = false;
                            continue;
                        }
                        if (ch == '\\') {
                            esc = true;
                            continue;
                        }
                        if (ch == delim) {
                            return Token{Token::T_STRING, std::move(acc), start_line, start_col};
                        }
                        acc.push_back(ch);
                    }
                    throw JZError("Unterminated string literal", start_line, start_col);
                }

                // numbers (simple recognition)
                if (isdigit(static_cast<unsigned char>(c)) || (
                        c == '-' && isdigit(static_cast<unsigned char>(peek(1))))) {
                    size_t start = i;
                    size_t start_line = line, start_col = col;
                    ++i;
                    ++col;
                    while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) {
                        ++i;
                        ++col;
                    }
                    if (i < s.size() && s[i] == '.') {
                        ++i;
                        ++col;
                        while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) {
                            ++i;
                            ++col;
                        }
                    }
                    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                        ++i;
                        ++col;
                        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                            ++i;
                            ++col;
                        }
                        while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) {
                            ++i;
                            ++col;
                        }
                    }
                    return Token{Token::T_NUMBER, string(s.substr(start, i - start)), start_line, start_col};
                }

                // identifier or keywords
                if (is_id_start(c)) {
                    size_t start = i;
                    size_t start_line = line, start_col = col;
                    ++i;
                    ++col;
                    while (i < s.size() && is_id_part(s[i])) {
                        ++i;
                        ++col;
                    }
                    string id = string(s.substr(start, i - start));
                    if (id == "true") return Token{Token::T_TRUE, id, start_line, start_col};
                    if (id == "false") return Token{Token::T_FALSE, id, start_line, start_col};
                    if (id == "null") return Token{Token::T_NULL, id, start_line, start_col};
                    if (id == "undefined") return Token{Token::T_UNDEFINED, id, start_line, start_col};
                    return Token{Token::T_IDENTIFIER, std::move(id), start_line, start_col};
                }

                // unexpected character
                throw JZError(std::format("Unexpected character in expression: '{}'", c), line, col);
            }
        };

        /* Parser: recursive descent parser for expressions */
        struct Parser {
            Lexer lex;
            Token cur;
            json &metadata;
            const ordered_json &data;

            // NEW: gate to enable/disable tool execution during parsing
            bool enable_tools = true;

            // RAII scope to toggle tool execution
            struct ToolExecScope {
                Parser *self;
                bool prev;

                explicit ToolExecScope(Parser *s, bool enabled)
                    : self(s), prev(s->enable_tools) { self->enable_tools = enabled; }

                ~ToolExecScope() { self->enable_tools = prev; }
            };

            explicit Parser(const string_view expr, const ordered_json &d, json &m) : lex(expr), metadata(m), data(d) {
                cur = lex.next();
            }

            void consume(const Token::Type t, const char *what) {
                if (cur.type != t) {
                    throw JZError(std::format("Expected {} in expression", what), cur.line, cur.col);
                }
                cur = lex.next();
            }

            bool match(const Token::Type t) {
                if (cur.type == t) {
                    cur = lex.next();
                    return true;
                }
                return false;
            }

            // find matching brace position in raw lexer input (used to extract {...} tool context)
            [[nodiscard]] size_t find_matching_brace_pos_in_source() const {
                // operate on lex.s directly; start at lex.i (current position points after '{')
                const string_view &s = lex.s;
                size_t pos = lex.i;
                int depth = 1;
                bool in_str = false;
                char delim = 0;
                bool esc = false;
                while (pos < s.size()) {
                    char c = s[pos++];
                    if (in_str) {
                        if (esc) {
                            esc = false;
                            continue;
                        }
                        if (c == '\\') {
                            esc = true;
                            continue;
                        }
                        if (c == delim) {
                            in_str = false;
                            delim = 0;
                            continue;
                        }
                    } else {
                        if (c == '"' || c == '\'') {
                            in_str = true;
                            delim = c;
                            continue;
                        }
                        if (c == '{') {
                            ++depth;
                            continue;
                        }
                        if (c == '}') { if (--depth == 0) return pos - 1; }
                    }
                }
                throw JZError("Unterminated '{...}' block in tool context", cur.line, cur.col);
            }

            Value parse_expr() { return parse_ternary(); }

            // Short-circuit ternary: only evaluate the selected branch
            Value parse_ternary() {
                Value cond = parse_or();
                if (cur.type == Token::T_QMARK) {
                    match(Token::T_QMARK);
                    if (is_truthy(cond)) {
                        Value thenv = parse_expr();
                        consume(Token::T_COLON, ":");
                        // Parse and discard else branch with tools disabled
                        {
                            ToolExecScope _(this, false);
                            (void) parse_expr();
                        }
                        return thenv;
                    } else {
                        // Parse and discard then branch with tools disabled
                        {
                            ToolExecScope _(this, false);
                            (void) parse_expr();
                        }
                        consume(Token::T_COLON, ":");
                        Value elsev = parse_expr();
                        return elsev;
                    }
                }
                return cond;
            }

            // Short-circuit OR: skip right when left is truthy
            Value parse_or() {
                Value left = parse_and();
                while (cur.type == Token::T_OR) {
                    match(Token::T_OR);
                    if (is_truthy(left)) {
                        ToolExecScope _(this, false);
                        (void) parse_and(); // consume tokens without running tools
                    } else {
                        left = parse_and(); // evaluate right
                    }
                }
                return left;
            }

            // Short-circuit AND: skip right when left is falsy
            Value parse_and() {
                Value left = parse_nullish();
                while (cur.type == Token::T_AND) {
                    match(Token::T_AND);
                    if (is_truthy(left)) {
                        left = parse_nullish(); // evaluate right
                    } else {
                        ToolExecScope _(this, false);
                        (void) parse_nullish(); // consume tokens without running tools
                    }
                }
                return left;
            }

            // Short-circuit nullish coalescing: skip right when left is not nullish
            Value parse_nullish() {
                Value left = parse_equality();
                if (cur.type == Token::T_NULLISH) {
                    match(Token::T_NULLISH);
                    if (!is_nullish(left)) {
                        ToolExecScope _(this, false);
                        (void) parse_equality(); // consume tokens without running tools
                        return left;
                    } else {
                        return parse_equality(); // evaluate right
                    }
                }
                return left;
            }

            Value parse_equality() {
                Value left = parse_relational();
                while (cur.type == Token::T_EQ || cur.type == Token::T_NE) {
                    Token::Type op = cur.type;
                    cur = lex.next();
                    Value right = parse_relational();
                    bool res = (op == Token::T_EQ) ? eq_values(left, right) : !eq_values(left, right);
                    left = Value::from_json(ordered_json(res));
                }
                return left;
            }

            Value parse_relational() {
                Value left = parse_unary();
                while (cur.type == Token::T_LT || cur.type == Token::T_GT || cur.type == Token::T_LTE || cur.type ==
                       Token::T_GTE) {
                    Token::Type op = cur.type;
                    cur = lex.next();
                    Value right = parse_unary();
                    char opcode = (op == Token::T_LT)
                                      ? '<'
                                      : (op == Token::T_GT)
                                            ? '>'
                                            : (op == Token::T_LTE)
                                                  ? 'l'
                                                  : 'g';
                    auto cmp = relational_compare(left, right, opcode);
                    bool res = cmp.has_value() ? cmp.value() : false;
                    left = Value::from_json(ordered_json(res));
                }
                return left;
            }

            Value parse_unary() {
                if (cur.type == Token::T_NOT) {
                    match(Token::T_NOT);
                    const Value v = parse_unary();
                    bool r = !is_truthy(v);
                    return Value::from_json(ordered_json(r));
                }
                return parse_pipeline();
            }

            // pipeline: primary (| #tool(...){...})*
            // Pipeline: run tool only if enabled and input not undefined
            Value parse_pipeline() {
                Value left = parse_primary();
                while (cur.type == Token::T_PIPE) {
                    match(Token::T_PIPE);
                    if (cur.type != Token::T_HASH)
                        throw JZError("Expected '#' before tool name in pipeline", cur.line, cur.col);
                    match(Token::T_HASH);

                    // Accept identifier or '{' (empty tool name with immediate context)
                    std::string toolname;
                    if (cur.type == Token::T_IDENTIFIER) {
                        toolname = cur.text;
                        cur = lex.next();
                    } else if (cur.type == Token::T_LBRACE || cur.type == Token::T_LPAREN) {
                        toolname = std::string(); // empty tool name
                        // fall-through to context parsing below
                    } else {
                        throw JZError("Expected tool identifier or '{' after '#'", cur.line, cur.col);
                    }

                    // Parse options
                    ordered_json options = ordered_json::object();
                    if (cur.type == Token::T_LPAREN) {
                        match(Token::T_LPAREN);
                        while (cur.type != Token::T_RPAREN) {
                            if (cur.type != Token::T_IDENTIFIER)
                                throw JZError("Expected option name in tool options", cur.line, cur.col);
                            string optname = cur.text;
                            cur = lex.next();
                            if (cur.type != Token::T_ASSIGN)
                                throw JZError("Expected '=' in tool option", cur.line, cur.col);
                            cur = lex.next();
                            Value optval = parse_expr(); // respects enable_tools
                            options[optname] = optval.j;
                            if (cur.type == Token::T_COMMA) {
                                match(Token::T_COMMA);
                                continue;
                            } else if (cur.type == Token::T_RPAREN) break;
                        }
                        consume(Token::T_RPAREN, ")");
                    }

                    ordered_json ctx = ordered_json::object();
                    string raw_block;
                    if (cur.type == Token::T_LBRACE) {
                        try {
                            size_t block_start = lex.i;
                            size_t block_end = find_matching_brace_pos_in_source();
                            raw_block = string(lex.s.substr(block_start, block_end - block_start));
                            lex.i = block_end + 1;
                            for (char ch: raw_block) {
                                if (ch == '\n') {
                                    ++lex.line;
                                    lex.col = 1;
                                } else { ++lex.col; }
                            }
                            if (block_end < lex.s.size() && lex.s[block_end] == '}') { ++lex.col; }
                            cur = lex.next();
                            // ltrim and check for enclosing braces
                            const auto it = ranges::find_if(raw_block, [](char c) {
                                return !isspace<char>(c, locale::classic());
                            });
                            raw_block.erase(raw_block.begin(), it);
                            if (raw_block.empty() || (raw_block[0] != '[' && raw_block[0] != '{')) {
                                raw_block.insert(raw_block.begin(), '{');
                                raw_block.push_back('}');
                            }

                            // parse context JSON from raw_block
                            if (!toolname.empty() && toolname[0] == '$') {
                                // modifier '$' tools: merge input data into context
                                if (!left.j.is_null()) {
                                    // input data can be merged into a specific context key or at top level
                                    if (options.contains("$key")) {
                                        ordered_json _data(data);
                                        _data[options["$key"].get<string>()] = left.j;
                                        // parse merged context JSON from raw_block
                                        ctx = Processor::to_json(raw_block, _data, metadata);
                                    } else if (!left.j.is_array()) {
                                        // if input data are not an array and not empty, merge at top level
                                        // (array can be merged only into a specific key)
                                        if (!left.j.empty()) {
                                            ordered_json _data(data);
                                            _data.merge_patch(left.j);
                                            // parse merged context JSON from raw_block
                                            ctx = Processor::to_json(raw_block, _data, metadata);
                                        } else {
                                            // parse context JSON from raw_block
                                            ctx = Processor::to_json(raw_block, data, metadata);
                                        }
                                    }
                                }
                            } else if (!toolname.empty()) {
                                // parse merged context JSON from raw_block only if the tool is not anonymous
                                ctx = Processor::to_json(raw_block, data, metadata);
                            }
                        } catch (exception &e) {
                            throw JZError(std::format("Tool '{}' error parsing context: [{}]", toolname, e.what()),
                                          cur.line, cur.col);
                        }
                    }

                    if (is_undefined(left)) {
                        // keep undefined sentinel; skip calling the tool
                    } else if (enable_tools) {
                        ordered_json in_val = left.j;
                        ordered_json out_val;
                        try {
                            if (toolname == "$") {
                                // anonymous tool: context was not processed beforehand; use context instruction to process input
                                // the '$' modifier indicates that global context is available
                                // '$loop' option tells to the anonymous tool if array input must be processed as items (default) or as a whole
                                const auto _loop = options.contains("$loop")
                                                       ? options["$loop"].get<bool>()
                                                       : true;
                                if (_loop && left.j.is_array()) {
                                    // put each array item into '$key' if given
                                    const auto _key = options.contains("$key")
                                                          ? options["$key"].get<string>()
                                                          : string();
                                    // put current index into each item if '$index' option is given
                                    const auto _idx = options.contains("$index")
                                                          ? options["$index"].get<string>()
                                                          : string();
                                    // special '$' tool: process each array item separately
                                    for (size_t idx = 0; idx < left.j.size(); ++idx) {
                                        const auto &item = left.j[idx];
                                        ordered_json _item(data);
                                        if (!_idx.empty())
                                            _item[_idx] = idx;
                                        if (!_key.empty())
                                            _item[_key] = item;
                                        else
                                            _item.merge_patch(item);
                                        const auto val = Processor::to_json(raw_block, _item, metadata);
                                        // cout << "====>>> " << val << endl;
                                        out_val.push_back(val);
                                    }
                                    // cout << "++++>>> " << out_val << endl;
                                } else {
                                    out_val = ctx;
                                }
                            } else if (toolname.empty()) {
                                // anonymous tool: context was not processed beforehand; use context instruction to process input
                                // please note that global context is not available
                                // 'loop' option tells to the anonymous tool if array input must be processed as items (default) or as a whole
                                const auto _loop = options.contains("loop")
                                                       ? options["loop"].get<bool>()
                                                       : true;
                                if (_loop && left.j.is_array()) {
                                    // put each array item into '$key' if given
                                    const auto _key = options.contains("key")
                                                          ? options["key"].get<string>()
                                                          : string();
                                    // put current index into each item if 'index' option is given
                                    const auto _idx = options.contains("index")
                                                          ? options["index"].get<string>()
                                                          : string();
                                    // process each array item separately
                                    for (size_t idx = 0; idx < left.j.size(); ++idx) {
                                        const auto &item = left.j[idx];
                                        if (!_idx.empty() || !_key.empty()) {
                                            ordered_json _item;
                                            if (!_idx.empty())
                                                _item[_idx] = idx;
                                            if (!_key.empty())
                                                _item[_key] = item;
                                            else
                                                _item.merge_patch(item);
                                            out_val.push_back(Processor::to_json(raw_block, _item, metadata));
                                        } else
                                            out_val.push_back(Processor::to_json(raw_block, item, metadata));
                                    }
                                } else {
                                    out_val = Processor::to_json(raw_block, left.j, metadata);
                                }
                            } else {
                                out_val = ToolsManager::instance().run_tool(
                                    toolname[0] == '$' ? toolname.substr(1) : toolname, in_val, options, ctx, metadata);
                            }
                        } catch (const std::exception &e) {
                            throw JZError(std::format("Tool '{}' failed: {}", toolname, e.what()), cur.line, cur.col);
                        }
                        left = Value::from_json(std::move(out_val));
                    } else {
                        // tools disabled: parse syntactically, do not execute; leave 'left' unchanged
                    }
                }
                return left;
            }

            Value parse_primary() {
                switch (cur.type) {
                    case Token::T_DOT: {
                        // Allow '.' to represent the entire input object
                        match(Token::T_DOT);
                        return Value::from_json(data);
                    }
                    case Token::T_LPAREN: {
                        match(Token::T_LPAREN);
                        Value v = parse_expr();
                        consume(Token::T_RPAREN, "')'");
                        return v;
                    }
                    case Token::T_STRING: {
                        string s = cur.text;
                        cur = lex.next();
                        return Value::from_json(ordered_json(s));
                    }
                    case Token::T_NUMBER: {
                        string n = cur.text;
                        cur = lex.next();
                        try {
                            if (n.find_first_of(".eE") != string::npos) {
                                double d = stod(n);
                                return Value::from_json(ordered_json(d));
                            }
                            long long v = stoll(n);
                            return Value::from_json(ordered_json(v));
                        } catch (...) {
                            return Value::from_json(ordered_json(n));
                        }
                    }
                    case Token::T_TRUE: cur = lex.next();
                        return Value::from_json(ordered_json(true));
                    case Token::T_FALSE: cur = lex.next();
                        return Value::from_json(ordered_json(false));
                    case Token::T_NULL: cur = lex.next();
                        return Value::from_json(ordered_json(nullptr));
                    case Token::T_UNDEFINED: {
                        cur = lex.next();
                        return Value::from_json(undefined_sentinel());
                    }
                    case Token::T_IDENTIFIER: {
                        vector<string> parts;
                        parts.push_back(cur.text);
                        cur = lex.next();
                        while (cur.type == Token::T_DOT || cur.type == Token::T_LBRACKET) {
                            if (cur.type == Token::T_DOT) {
                                match(Token::T_DOT);
                                if (cur.type != Token::T_IDENTIFIER)
                                    throw JZError("Expected identifier after '.' in path", cur.line, cur.col);
                                parts.push_back(cur.text);
                                cur = lex.next();
                            } else {
                                match(Token::T_LBRACKET);
                                if (cur.type == Token::T_NUMBER) {
                                    parts.push_back(cur.text);
                                    cur = lex.next();
                                } else if (cur.type == Token::T_STRING) {
                                    parts.push_back(cur.text);
                                    cur = lex.next();
                                } else
                                    throw JZError("Expected number or string inside [...] in path", cur.line, cur.col);
                                consume(Token::T_RBRACKET, "']'");
                            }
                        }
                        return resolve_path(parts);
                    }
                    default:
                        throw JZError("Unexpected token in expression", cur.line, cur.col);
                }
            }

            [[nodiscard]] Value resolve_path(const vector<string> &parts) const {
                const ordered_json *curj = &data;
                for (const auto &p: parts) {
                    bool is_index = !p.empty() && ranges::all_of(p, ::isdigit);
                    if (curj->is_array() && is_index) {
                        auto idx = static_cast<size_t>(stoull(p));
                        if (idx >= curj->size()) return Value::missing_value();
                        curj = &((*curj)[idx]);
                    } else if (curj->is_object()) {
                        auto it = curj->find(p);
                        if (it == curj->end()) return Value::missing_value();
                        curj = &(*it);
                    } else return Value::missing_value();
                }
                if (is_undefined_sentinel(*curj)) return Value::from_json(*curj);
                return Value::from_json(*curj);
            }

            // evaluate expression helper
            static ordered_json evaluate_expression(string_view expr, const ordered_json &data, json &metadata,
                                                    bool &was_missing) {
                Parser p(expr, data, metadata);
                Value v = p.parse_expr();
                was_missing = v.missing;
                return v.j;
            }
        }; // end Parser
    } // namespace eval

    /* -------------------------
       Template placeholder replacement
       - supports $(expr) -> JSON insertion
       - backtick strings: `...` where $(...) expressions inside are evaluated and resulting string inserted (then JSON-escaped)
       - throws JZError with positions when unterminated templates/expressions found
       ------------------------- */

    static string to_json_string_literal(const string &text) {
        return ordered_json(text).dump();
    }

    // helper to append value for template (if string => raw text, else JSON dump)
    static void append_value_for_template(string &acc, const ordered_json &val) {
        if (val.is_null()) return;
        if (is_undefined_sentinel(val)) return;
        if (val.is_string()) acc += val.get_ref<const string &>();
        else acc += val.dump();
    }

    string Processor::replace_placeholders(string_view s, const ordered_json &data, json &metadata) {
        // similar behavior as earlier implementation but using string_view and Scanner for pos tracking
        string out;
        out.reserve(s.size());
        Scanner sc{s};

        bool in_string = false;
        char delim = 0;
        bool escape = false;

        while (!sc.eof()) {
            char c = sc.next();

            // handle backtick template: `...`
            if (!in_string && c == '`') {
                string acc;
                bool esc = false;
                auto start_pos = sc.position_prev();
                while (!sc.eof()) {
                    char ch = sc.next();
                    if (esc) {
                        acc.push_back(ch);
                        esc = false;
                        continue;
                    }
                    if (ch == '\\') {
                        esc = true;
                        continue;
                    }
                    if (ch == '`') break;
                    if (ch == '$' && sc.peek() == '(') {
                        sc.advance(); // skip '('
                        // find matching ')' with nested parentheses and string awareness
                        int depth = 1;
                        bool ex_in_str = false;
                        bool ex_esc = false;
                        char ex_delim = 0;
                        size_t expr_start_idx = sc.pos();
                        size_t expr_start_line = sc.line, expr_start_col = sc.col;
                        while (!sc.eof()) {
                            char ec = sc.next();
                            if (ex_in_str) {
                                if (ex_esc) {
                                    ex_esc = false;
                                    continue;
                                }
                                if (ec == '\\') {
                                    ex_esc = true;
                                    continue;
                                }
                                if (ec == ex_delim) {
                                    ex_in_str = false;
                                    ex_delim = 0;
                                    continue;
                                }
                                continue;
                            } else {
                                if (ec == '"' || ec == '\'') {
                                    ex_in_str = true;
                                    ex_delim = ec;
                                    continue;
                                }
                                if (ec == '(') ++depth;
                                else if (ec == ')') { if (--depth == 0) break; }
                            }
                        }
                        if (sc.eof()) {
                            throw JZError("Unterminated $(...) in template string", expr_start_line, expr_start_col);
                        }
                        size_t expr_end_idx = sc.pos() - 1;
                        auto expr = string(sc.s.substr(expr_start_idx, expr_end_idx - expr_start_idx));
                        bool missing = false;
                        ordered_json val = eval::Parser::evaluate_expression(expr, data, metadata, missing);
                        if (!missing && !val.is_null() && !is_undefined_sentinel(val))
                            append_value_for_template(
                                acc, val);
                        continue;
                    }
                    acc.push_back(ch);
                }
                if (sc.eof()) {
                    throw JZError("Unterminated template string (`...`)", start_pos.first, start_pos.second);
                }
                // produce JSON string literal from acc
                out += ordered_json(acc).dump();
                continue;
            }

            if (in_string) {
                out.push_back(c);
                if (escape) escape = false;
                else {
                    if (c == '\\') escape = true;
                    else if (c == delim) {
                        in_string = false;
                        delim = 0;
                    }
                }
                continue;
            }

            if (c == '"' || c == '\'') {
                in_string = true;
                delim = c;
                out.push_back(c);
                continue;
            }

            // if (c == '#' && sc.peek() == '{') {
            //     auto [ln, col] = sc.position_prev();
            //     throw JZError("Encountered '#{...}' directive which is not supported", ln, col);
            // }

            // handle $(expr) replacement
            if (c == '$' && sc.peek() == '(') {
                // remember start position for error reporting
                auto start_pos = sc.position_prev();
                sc.advance(); // skip '('
                int depth = 1;
                bool str_in = false;
                bool str_esc = false;
                char str_delim = 0;
                size_t expr_start_idx = sc.pos();
                size_t expr_start_line = sc.line, expr_start_col = sc.col;
                while (!sc.eof()) {
                    char ch = sc.next();
                    if (str_in) {
                        if (str_esc) {
                            str_esc = false;
                            continue;
                        }
                        if (ch == '\\') {
                            str_esc = true;
                            continue;
                        }
                        if (ch == str_delim) {
                            str_in = false;
                            str_delim = 0;
                            continue;
                        }
                        continue;
                    } else {
                        if (ch == '"' || ch == '\'') {
                            str_in = true;
                            str_delim = ch;
                            continue;
                        }
                        if (ch == '(') ++depth;
                        else if (ch == ')') { if (--depth == 0) break; }
                    }
                }
                if (sc.eof()) throw JZError("Unterminated $(...) placeholder", expr_start_line, expr_start_col);
                size_t expr_end_idx = sc.pos() - 1;
                string expr = string(sc.s.substr(expr_start_idx, expr_end_idx - expr_start_idx));
                bool missing = false;
                ordered_json val = eval::Parser::evaluate_expression(expr, data, metadata, missing);
                if (missing) out += undefined_sentinel().dump();
                else {
                    if (is_undefined_sentinel(val)) out += undefined_sentinel().dump();
                    else out += val.dump();
                }
                continue;
            }

            out.push_back(c);
        }

        return out;
    }

    /* remove_undefined_sentinels:
       - remove object properties with the undefined sentinel
       - filter out undefined sentinel elements from arrays
    */
    void Processor::remove_undefined_sentinels(ordered_json &j) {
        if (j.is_object()) {
            vector<string> to_erase;
            for (auto it = j.begin(); it != j.end(); ++it) {
                const string &key = it.key();
                ordered_json &val = it.value();
                if (is_undefined_sentinel(val)) to_erase.push_back(key);
                else remove_undefined_sentinels(val);
            }
            for (const auto &k: to_erase) j.erase(k);
        } else if (j.is_array()) {
            ordered_json new_arr = ordered_json::array();
            for (auto &el: j) {
                if (is_undefined_sentinel(el)) continue;
                remove_undefined_sentinels(el);
                new_arr.push_back(std::move(el));
            }
            j = std::move(new_arr);
        }
    }

    /* -------------------------
       Public API: to_json
       1) remove comments
       2) replace placeholders / evaluate templates
       ------------------------- */
    string Processor::to_string(const string_view jz_input, const ordered_json &data, json &metadata) {
        // 1) comments
        const auto no_comments = remove_comments(jz_input);

        // 2) placeholders and backtick templates
        return replace_placeholders(no_comments, data, metadata);
    }

    /* -------------------------
       Public API: to_json
       1) to_string
       2) normalize json5-like to JSON
       3) parse into ordered_json
       4) remove undefined sentinels
       ------------------------- */
    ordered_json Processor::to_json(const string_view jz_input, const ordered_json &data, json &metadata) {
        // 1) to_string
        const auto with_values = to_string(jz_input, data, metadata);

        // 2) normalize JSON5-ish constructs
        auto jsonish = normalize_json5_to_json(with_values);
        try {
            // 3) parse -> note: nlohmann::json parse takes std::string
            ordered_json j = ordered_json::parse(jsonish);
            // 4) remove undefined sentinels
            remove_undefined_sentinels(j);
            return j;
        } catch (const std::exception &e) {
            // we want to throw JZError including message and (approx) first line/col of failure
            // find position by naive heuristics: look for first occurrence of problematic substring
            // For simplicity, throw with line 1 col 1 (could be improved by analyzing exception message)
            throw JZError(std::format("Invalid JSON after JZ transform: {}", e.what()), jsonish);
        }
    }
} // namespace jz
