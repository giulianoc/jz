// cpp
#include "JZParser.hpp"
#include "ToolManager.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace std;
using ordered_json = nlohmann::ordered_json;

namespace jz {

// ---------- undefined sentinel ----------
static const char UNDEF_KEY[] = "__jz_undefined__";

ordered_json undefined() {
    ordered_json s = ordered_json::object();
    s[UNDEF_KEY] = true;
    return s;
}

static bool is_undefined_sentinel(const ordered_json& j) {
    return j.is_object() && j.size() == 1 && j.contains(UNDEF_KEY) && j.at(UNDEF_KEY).is_boolean() && j.at(UNDEF_KEY).get<bool>() == true;
}

// ---------- Utilities ----------
static inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool Processor::is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}
bool Processor::is_identifier_part(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// ---------- Comments removal ----------
string Processor::remove_comments(const string& s) {
    string out;
    out.reserve(s.size());

    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    char string_delim = 0;
    bool escape = false;

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        char next = (i + 1 < s.size() ? s[i + 1] : '\0');

        if (in_line_comment) {
            if (c == '\n') {
                in_line_comment = false;
                out.push_back(c);
            }
            continue;
        }
        if (in_block_comment) {
            if (c == '*' && next == '/') {
                in_block_comment = false;
                ++i; // skip '/'
            }
            continue;
        }

        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else {
                if (c == '\\') {
                    escape = true;
                } else if (c == string_delim) {
                    in_string = false;
                    string_delim = 0;
                }
            }
            continue;
        }

        // not in comment or string
        if (c == '/' && next == '/') {
            in_line_comment = true;
            ++i; // skip second slash
            continue;
        }
        if (c == '/' && next == '*') {
            in_block_comment = true;
            ++i; // skip '*'
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
        throw JZError("Unterminated block comment");
    }
    return out;
}

// ---------- Single-quoted strings conversion ----------
string Processor::convert_single_quoted_strings(const string& s) {
    string out;
    out.reserve(s.size());

    bool in_string = false;
    char delim = 0;
    bool escape = false;

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];

        if (!in_string) {
            if (c == '"' || c == '\'') {
                in_string = true;
                delim = c;
                if (delim == '\'') {
                    // Start JSON with double-quote instead
                    out.push_back('"');
                } else {
                    out.push_back(c);
                }
                escape = false;
            } else {
                out.push_back(c);
            }
            continue;
        }

        // Inside string
        if (delim == '"') {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
                delim = 0;
            }
            continue;
        }

        // Single quoted input -> convert to double quoted JSON
        if (escape) {
            if (c == '\'') {
                out.push_back('\''); // no leading backslash
            } else {
                out.push_back('\\');
                out.push_back(c);
            }
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '\'') {
            out.push_back('"');
            in_string = false;
            delim = 0;
            continue;
        }

        if (c == '"') {
            out.push_back('\\');
            out.push_back('"');
        } else {
            out.push_back(c);
        }
    }

    if (in_string && delim == '\'') {
        throw JZError("Unterminated single-quoted string");
    }

    return out;
}

// ---------- Quote unquoted keys ----------
string Processor::quote_unquoted_keys(const string& s) {
    string out;
    out.reserve(s.size());

    enum class Ctx { None, InObject, InArray };
    struct Frame { Ctx ctx; bool expecting_key; };
    vector<Frame> stack;

    bool in_string = false;
    char delim = 0;
    bool escape = false;

    auto push_ctx = [&](Ctx c) {
        if (c == Ctx::InObject) {
            stack.push_back({c, true});
        } else {
            stack.push_back({c, false});
        }
    };
    auto pop_ctx = [&]() {
        if (!stack.empty()) stack.pop_back();
    };

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];

        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == delim) {
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
            push_ctx(Ctx::InObject);
            continue;
        }
        if (c == '[') {
            out.push_back(c);
            push_ctx(Ctx::InArray);
            continue;
        }
        if (c == '}') {
            out.push_back(c);
            pop_ctx();
            continue;
        }
        if (c == ']') {
            out.push_back(c);
            pop_ctx();
            continue;
        }

        if (!stack.empty() && stack.back().ctx == Ctx::InObject) {
            if (stack.back().expecting_key) {
                if (is_space(c)) {
                    out.push_back(c);
                    continue;
                }
                if (c == '"') {
                    in_string = true;
                    delim = '"';
                    out.push_back(c);
                    continue;
                }
                if (c == '\'') {
                    in_string = true;
                    delim = '\'';
                    out.push_back(c);
                    continue;
                }
                if (is_identifier_start(c)) {
                    size_t j = i + 1;
                    while (j < s.size() && is_identifier_part(s[j])) ++j;

                    size_t k = j;
                    while (k < s.size() && is_space(s[k])) ++k;

                    if (k < s.size() && s[k] == ':') {
                        out.push_back('"');
                        out.append(s.begin() + static_cast<long>(i), s.begin() + static_cast<long>(j));
                        out.push_back('"');
                        out.append(s.begin() + static_cast<long>(j), s.begin() + static_cast<long>(k));
                        out.push_back(':');
                        i = k;
                        stack.back().expecting_key = false;
                        continue;
                    } else {
                        out.push_back(c);
                        continue;
                    }
                }
                out.push_back(c);
                continue;
            } else {
                out.push_back(c);
                if (c == ',') {
                    stack.back().expecting_key = true;
                }
                continue;
            }
        }

        out.push_back(c);
    }

    return out;
}

// ---------- Remove trailing commas ----------
string Processor::remove_trailing_commas(const string& s) {
    string out;
    out.reserve(s.size());

    bool in_string = false;
    char delim = 0;
    bool escape = false;

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];

        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == delim) {
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
            size_t trim = out.size();
            while (trim > 0 && is_space(out[trim - 1])) --trim;

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

// ---------- JSON5 -> JSON pipeline ----------
string Processor::normalize_json5_to_json(const string& s) {
    auto s1 = convert_single_quoted_strings(s);
    auto s2 = quote_unquoted_keys(s1);
    auto s3 = remove_trailing_commas(s2);
    return s3;
}

// ---------- Placeholder expression evaluation + tools pipeline ----------

namespace eval {

struct Value {
    bool missing = false; // path not found
    ordered_json j;

    static Value from_json(ordered_json v) { return Value{false, std::move(v)}; }
    static Value missing_value() { return Value{true, ordered_json(nullptr)}; }
};

static bool is_undefined(const Value& v) {
    return v.missing || is_undefined_sentinel(v.j);
}

// nullish for ?? means only missing OR undefined
static bool is_nullish(const Value& v) {
    return v.missing || is_undefined(v);
}

static bool is_truthy(const Value& v) {
    if (v.missing) return false;
    if (is_undefined(v)) return false;
    if (v.j.is_boolean()) return v.j.get<bool>();
    if (v.j.is_null()) return false;
    if (v.j.is_number_integer() || v.j.is_number_unsigned() || v.j.is_number_float()) {
        try {
            double d = v.j.get<double>();
            return d != 0.0;
        } catch (...) {
            return true;
        }
    }
    if (v.j.is_string()) return !v.j.get_ref<const string&>().empty();
    if (v.j.is_array() || v.j.is_object()) return true;
    return true;
}

// Try to coerce a Value to a double when sensible.
// Returns optional<double> with value if conversion possible, otherwise nullopt.
static optional<double> to_number_opt(const Value& v) {
    if (v.missing) return nullopt;
    if (is_undefined(v)) return nullopt;
    if (v.j.is_number()) {
        try {
            return v.j.get<double>();
        } catch (...) {
            return nullopt;
        }
    }
    if (v.j.is_boolean()) {
        return v.j.get<bool>() ? 1.0 : 0.0;
    }
    if (v.j.is_string()) {
        const string& s = v.j.get_ref<const string&>();
        if (s.empty()) return 0.0; // JS: Number("") -> 0
        try {
            size_t idx = 0;
            double d = stod(s, &idx);
            if (idx == s.size()) return d;
            return nullopt;
        } catch (...) {
            return nullopt;
        }
    }
    return nullopt;
}

// Compare equality in a JS-lite sensible way for our common cases.
// Returns true if equal, false otherwise.
static bool eq_values(const Value& a, const Value& b) {
    // missing/undefined handling:
    if (a.missing && b.missing) return true;
    if ((a.missing && is_undefined(b)) || (b.missing && is_undefined(a))) return true;
    if (is_undefined(a) && is_undefined(b)) return true;

    // If both primitives and same type -> use ordered_json equality
    if (!a.missing && !is_undefined(a) && !b.missing && !is_undefined(b)) {
        if (a.j.type() == b.j.type()) {
            return a.j == b.j;
        }
    }

    // Try numeric comparison when one is number or numeric string or boolean:
    auto an = to_number_opt(a);
    auto bn = to_number_opt(b);
    if (an && bn) {
        return *an == *bn;
    }

    // Fallback: compare stringified forms
    string sa = a.missing ? string("missing") : (is_undefined(a) ? string("undefined") : a.j.dump());
    string sb = b.missing ? string("missing") : (is_undefined(b) ? string("undefined") : b.j.dump());
    return sa == sb;
}

// Relational comparison (<, >, <=, >=) — returns optional<bool> (nullopt if not comparable)
static optional<bool> relational_compare(const Value& a, const Value& b, char op) {
    // If both numbers (or convertible), compare numerically
    auto an = to_number_opt(a);
    auto bn = to_number_opt(b);
    if (an && bn) {
        double A = *an;
        double B = *bn;
        switch (op) {
            case '<': return A < B;
            case '>': return A > B;
            case 'l': return A <= B; // <=
            case 'g': return A >= B; // >=
            default: return nullopt;
        }
    }

    // If both strings, compare lexicographically
    if (!a.missing && !is_undefined(a) && a.j.is_string() && !b.missing && !is_undefined(b) && b.j.is_string()) {
        const string& A = a.j.get_ref<const string&>();
        const string& B = b.j.get_ref<const string&>();
        switch (op) {
            case '<': return A < B;
            case '>': return A > B;
            case 'l': return A <= B;
            case 'g': return A >= B;
            default: return nullopt;
        }
    }

    // Otherwise not comparable in our simplified semantics
    return nullopt;
}

struct Token {
    enum Type {
        T_EOF,
        T_IDENTIFIER,
        T_NUMBER,
        T_STRING,
        T_TRUE,
        T_FALSE,
        T_NULL,
        T_UNDEFINED,
        T_QMARK,   // ?
        T_COLON,   // :
        T_DOT,     // .
        T_LPAREN,  // (
        T_RPAREN,  // )
        T_LBRACKET, // [
        T_RBRACKET, // ]
        T_LBRACE,  // {
        T_RBRACE,  // }
        T_COMMA,   // ,
        T_PIPE,    // |
        T_HASH,    // #
        T_OR,      // ||
        T_AND,     // &&
        T_NOT,     // !
        T_NULLISH, // ??
        T_EQ,      // ==
        T_NE,      // !=
        T_GT,      // >
        T_LT,      // <
        T_GTE,     // >=
        T_LTE,     // <=
        T_ASSIGN   // =     (single equals for tool options)
    } type;
    string text;
};

struct Lexer {
    const string& s;
    size_t i = 0;

    explicit Lexer(const string& src) : s(src) {}

    static bool is_id_start(char c) {
        return isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    }
    static bool is_id_part(char c) {
        return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    }

    void skip_ws() {
        while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
    }

    Token next() {
        skip_ws();
        if (i >= s.size()) return {Token::T_EOF, ""};
        char c = s[i];

        // Two-char operators first
        if (c == '?' && i + 1 < s.size() && s[i + 1] == '?') { i += 2; return {Token::T_NULLISH, "??"}; }
        if (c == '|' && i + 1 < s.size() && s[i + 1] == '|') { i += 2; return {Token::T_OR, "||"}; }
        if (c == '&' && i + 1 < s.size() && s[i + 1] == '&') { i += 2; return {Token::T_AND, "&&"}; }
        if (c == '=' && i + 1 < s.size() && s[i + 1] == '=') { i += 2; return {Token::T_EQ, "=="}; }
        if (c == '!' && i + 1 < s.size() && s[i + 1] == '=') { i += 2; return {Token::T_NE, "!="}; }
        if (c == '>' && i + 1 < s.size() && s[i + 1] == '=') { i += 2; return {Token::T_GTE, ">="}; }
        if (c == '<' && i + 1 < s.size() && s[i + 1] == '=') { i += 2; return {Token::T_LTE, "<="}; }

        // Single-char tokens
        if (c == '?') { ++i; return {Token::T_QMARK, "?"}; }
        if (c == ':') { ++i; return {Token::T_COLON, ":"}; }
        if (c == '.') { ++i; return {Token::T_DOT, "."}; }
        if (c == '(') { ++i; return {Token::T_LPAREN, "("}; }
        if (c == ')') { ++i; return {Token::T_RPAREN, ")"}; }
        if (c == '[') { ++i; return {Token::T_LBRACKET, "["}; }
        if (c == ']') { ++i; return {Token::T_RBRACKET, "]"}; }
        if (c == '{') { ++i; return {Token::T_LBRACE, "{"}; }
        if (c == '}') { ++i; return {Token::T_RBRACE, "}"}; }
        if (c == ',') { ++i; return {Token::T_COMMA, ","}; }
        if (c == '|') { ++i; return {Token::T_PIPE, "|"}; } // single pipe for tool pipeline
        if (c == '#') { ++i; return {Token::T_HASH, "#"}; } // tool marker
        if (c == '!') { ++i; return {Token::T_NOT, "!"}; }
        if (c == '>') { ++i; return {Token::T_GT, ">"}; }
        if (c == '<') { ++i; return {Token::T_LT, "<"}; }

        // single '=' (assignment / option separator)
        if (c == '=') { ++i; return {Token::T_ASSIGN, "="}; }

        if (c == '"' || c == '\'') {
            char delim = c;
            ++i;
            string acc;
            bool esc = false;
            while (i < s.size()) {
                char ch = s[i++];
                if (esc) {
                    switch (ch) {
                        case '"': acc.push_back('"'); break;
                        case '\\': acc.push_back('\\'); break;
                        case '/': acc.push_back('/'); break;
                        case 'b': acc.push_back('\b'); break;
                        case 'f': acc.push_back('\f'); break;
                        case 'n': acc.push_back('\n'); break;
                        case 'r': acc.push_back('\r'); break;
                        case 't': acc.push_back('\t'); break;
                        case 'u': {
                            if (i + 4 <= s.size()) {
                                acc.push_back('\\');
                                acc.push_back('u');
                                acc.append(s, i, 4);
                                i += 4;
                            } else {
                                acc.push_back('\\');
                                acc.push_back('u');
                            }
                            break;
                        }
                        case '\'': acc.push_back('\''); break;
                        default: acc.push_back(ch); break;
                    }
                    esc = false;
                } else if (ch == '\\') {
                    esc = true;
                } else if (ch == delim) {
                    break;
                } else {
                    acc.push_back(ch);
                }
            }
            return {Token::T_STRING, std::move(acc)};
        }

        if (isdigit(static_cast<unsigned char>(c)) || (c == '-' && i + 1 < s.size() && isdigit(static_cast<unsigned char>(s[i + 1])))) {
            size_t start = i;
            ++i;
            while (i < s.size() && (isdigit(static_cast<unsigned char>(s[i])))) ++i;
            if (i < s.size() && s[i] == '.') {
                ++i;
                while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) ++i;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                ++i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) ++i;
            }
            return {Token::T_NUMBER, s.substr(start, i - start)};
        }

        if (is_id_start(c)) {
            size_t start = i++;
            while (i < s.size() && is_id_part(s[i])) ++i;
            string id = s.substr(start, i - start);
            if (id == "true")  return {Token::T_TRUE, id};
            if (id == "false") return {Token::T_FALSE, id};
            if (id == "null")  return {Token::T_NULL, id};
            if (id == "undefined") return {Token::T_UNDEFINED, id};
            return {Token::T_IDENTIFIER, std::move(id)};
        }

        string msg = "Unexpected character in expression: '";
        msg.push_back(c);
        msg.push_back('\'');
        throw JZError(msg);
    }
};

struct Parser {
    Lexer lex;
    const ordered_json& data;
    Token cur;

    explicit Parser(const string& expr, const ordered_json& d) : lex(expr), data(d) {
        cur = lex.next();
    }

    void consume(Token::Type t, const char* what) {
        if (cur.type != t) {
            ostringstream oss;
            oss << "Expected " << what << " in expression";
            throw JZError(oss.str());
        }
        cur = lex.next();
    }

    bool match(Token::Type t) {
        if (cur.type == t) {
            cur = lex.next();
            return true;
        }
        return false;
    }

    // utility: find matching brace position in lex.s starting at current lex.i (which is after the '{')
    size_t find_matching_brace_pos() {
        const string& s = lex.s;
        size_t pos = lex.i; // points after '{'
        int depth = 1;
        bool in_str = false;
        char delim = 0;
        bool esc = false;
        while (pos < s.size()) {
            char c = s[pos++];
            if (in_str) {
                if (esc) { esc = false; continue; }
                if (c == '\\') { esc = true; continue; }
                if (c == delim) { in_str = false; delim = 0; continue; }
            } else {
                if (c == '"' || c == '\'') { in_str = true; delim = c; continue; }
                if (c == '{') { depth++; continue; }
                if (c == '}') { depth--; if (depth == 0) return pos - 1; }
            }
        }
        throw JZError("Unterminated '{...}' block in tool context");
    }

    // Top-level
    Value parse_expr() {
        return parse_ternary();
    }

    // ternary: cond ? then : else
    Value parse_ternary() {
        Value cond = parse_or();
        if (match(Token::T_QMARK)) {
            Value thenv = parse_expr();
            consume(Token::T_COLON, "':'");
            Value elsev = parse_expr();
            return is_truthy(cond) ? thenv : elsev;
        }
        return cond;
    }

    // OR: left || right  (short-circuit, returns operand)
    Value parse_or() {
        Value left = parse_and();
        while (cur.type == Token::T_OR) {
            match(Token::T_OR);
            Value right = parse_and();
            left = is_truthy(left) ? left : right;
        }
        return left;
    }

    // AND: left && right (short-circuit)
    Value parse_and() {
        Value left = parse_nullish();
        while (cur.type == Token::T_AND) {
            match(Token::T_AND);
            Value right = parse_nullish();
            left = is_truthy(left) ? right : left;
        }
        return left;
    }

    // Nullish coalesce: left ?? right (operates only for missing/undefined)
    Value parse_nullish() {
        Value left = parse_equality();
        if (cur.type == Token::T_NULLISH) {
            match(Token::T_NULLISH);
            Value right = parse_equality();
            left = is_nullish(left) ? right : left;
        }
        return left;
    }

    // Equality: ==, != (produce boolean)
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

    // Relational: <, >, <=, >= (produce boolean)
    Value parse_relational() {
        Value left = parse_unary();
        while (cur.type == Token::T_LT || cur.type == Token::T_GT || cur.type == Token::T_LTE || cur.type == Token::T_GTE) {
            Token::Type op = cur.type;
            cur = lex.next();
            Value right = parse_unary();
            char opcode = 0;
            if (op == Token::T_LT) opcode = '<';
            else if (op == Token::T_GT) opcode = '>';
            else if (op == Token::T_LTE) opcode = 'l'; // <=
            else if (op == Token::T_GTE) opcode = 'g'; // >=

            auto cmp = relational_compare(left, right, opcode);
            bool res = false;
            if (cmp.has_value()) res = cmp.value();
            else res = false; // if not comparable, treat as false
            left = Value::from_json(ordered_json(res));
        }
        return left;
    }

    // Unary: !primary
    Value parse_unary() {
        if (cur.type == Token::T_NOT) {
            match(Token::T_NOT);
            Value v = parse_unary();
            bool r = !is_truthy(v);
            return Value::from_json(ordered_json(r));
        }
        return parse_pipeline();
    }

    // pipeline: primary (| #tool(...){...})*
    // cpp
	// Modified parse_pipeline() inside Parser in `thirdPartyLibraries/jz/src/JZParser.cpp`
	Value parse_pipeline() {
	    Value left = parse_primary();
	    while (cur.type == Token::T_PIPE) {
	        match(Token::T_PIPE);
	        // expect tool invocation starting with '#'
	        if (cur.type != Token::T_HASH) {
	            throw JZError("Expected '#' before tool name in pipeline");
	        }
	        match(Token::T_HASH);
	        if (cur.type != Token::T_IDENTIFIER) {
	            throw JZError("Expected tool identifier after '#'");
	        }
	        string toolname = cur.text;
	        cur = lex.next();

	        // parse options: ( key = expr, ... )
	        ordered_json options = ordered_json::object();
	        if (cur.type == Token::T_LPAREN) {
	            match(Token::T_LPAREN);
	            // allow empty parentheses
	            while (cur.type != Token::T_RPAREN) {
	                if (cur.type != Token::T_IDENTIFIER) {
	                    throw JZError("Expected option name in tool options");
	                }
	                string optname = cur.text;
	                cur = lex.next();
	                consume(Token::T_ASSIGN, "'=' in tool option");
	                // parse expression for the option value
	                Value optval = parse_expr();
	                options[optname] = optval.j;
	                if (cur.type == Token::T_COMMA) {
	                    match(Token::T_COMMA);
	                    continue;
	                } else if (cur.type == Token::T_RPAREN) {
	                    break;
	                } else {
	                    // tolerate missing comma, break to avoid infinite loop
	                    break;
	                }
	            }
	            consume(Token::T_RPAREN, "')' after tool options");
	        }

	        // parse optional context block { ... } raw (balanced) and attempt to parse to JSON5 -> ordered_json
	        ordered_json ctx = ordered_json::object();
	        if (cur.type == Token::T_LBRACE) {
	            // lexer current position is after '{' (lex.i). find matching '}' pos.
	            size_t block_start = lex.i; // position after '{'
	            size_t block_end = find_matching_brace_pos(); // position of matching '}' char
	            string raw_block = lex.s.substr(block_start, block_end - block_start);
	            // advance lexer index to position after '}'
	            lex.i = block_end + 1;
	            cur = lex.next();

	            // try to normalize and parse raw_block as jz fragment (JSON5 -> ordered_json)
	            try {
	                string normalized = Processor::normalize_json5_to_json(raw_block);
	                ordered_json parsed_block = ordered_json::parse(normalized);
	                ctx = parsed_block;
	            } catch (...) {
	                // parsing failed: provide raw string in ctx
	                ctx = ordered_json::object();
	                ctx["__raw_block__"] = raw_block;
	            }
	        }

	        // run tool only if input is not the undefined sentinel
	    	if (toolname == "upper") cerr << "missing: " << left.missing << " - j:" << left.j.dump() << endl;
	        if (is_undefined(left)) {
	            // preserve undefined sentinel — do not call the tool(s)
	            // left remains unchanged (still undefined)
	        } else {
	            ordered_json in_val = left.j;
	            ordered_json out_val;
	            try {
	                out_val = ToolManager::instance().run_tool(toolname, in_val, options, ctx);
	            } catch (const std::exception& e) {
	                throw JZError(string("Tool '") + toolname + "' failed: " + e.what());
	            }
	            left = Value::from_json(std::move(out_val));
	        }
	    }
	    return left;
	}

    // Primary: literals, identifiers/paths, parenthesis
    Value parse_primary() {
        switch (cur.type) {
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
                    } else {
                        long long v = stoll(n);
                        return Value::from_json(ordered_json(v));
                    }
                } catch (...) {
                    return Value::from_json(ordered_json(n));
                }
            }
            case Token::T_TRUE:  cur = lex.next(); return Value::from_json(ordered_json(true));
            case Token::T_FALSE: cur = lex.next(); return Value::from_json(ordered_json(false));
            case Token::T_NULL:  cur = lex.next(); return Value::from_json(ordered_json(nullptr));
            case Token::T_UNDEFINED: {
                cur = lex.next();
                return Value::from_json(::jz::undefined());
            }
            case Token::T_IDENTIFIER: {
                // path: id(.id | [index|string])*
                vector<string> parts;
                parts.push_back(cur.text);
                cur = lex.next();
                while (cur.type == Token::T_DOT || cur.type == Token::T_LBRACKET) {
                    if (cur.type == Token::T_DOT) {
                        match(Token::T_DOT);
                        if (cur.type != Token::T_IDENTIFIER) {
                            throw JZError("Expected identifier after '.' in path");
                        }
                        parts.push_back(cur.text);
                        cur = lex.next();
                    } else {
                        // [ number ] or [ "string" ] or [ 'string' ]
                        match(Token::T_LBRACKET);
                        if (cur.type == Token::T_NUMBER) {
                            // push the numeric index as string (resolve_path will detect numeric)
                            parts.push_back(cur.text);
                            cur = lex.next();
                        } else if (cur.type == Token::T_STRING) {
                            // string key
                            parts.push_back(cur.text);
                            cur = lex.next();
                        } else {
                            throw JZError("Expected number or string inside [...] in path");
                        }
                        consume(Token::T_RBRACKET, "']'");
                    }
                }
                return resolve_path(parts);
            }
            default: {
                throw JZError("Unexpected token in expression");
            }
        }
    }

    // Resolve path into Value (handles arrays indices)
    Value resolve_path(const vector<string>& parts) {
        const ordered_json* curj = &data;
        for (const auto& p : parts) {
            bool is_index = !p.empty() && std::all_of(p.begin(), p.end(), ::isdigit);
            if (curj->is_array() && is_index) {
                size_t idx = static_cast<size_t>(std::stoull(p));
                if (idx >= curj->size()) {
                    return Value::missing_value();
                }
                curj = &((*curj)[idx]);
            } else if (curj->is_object()) {
                auto it = curj->find(p);
                if (it == curj->end()) {
                    return Value::missing_value();
                }
                curj = &(*it);
            } else {
                return Value::missing_value();
            }
        }
        // If the value found in the provided data is the undefined sentinel, return it (not `missing`).
        if (is_undefined_sentinel(*curj)) {
            return Value::from_json(*curj);
        }
        return Value::from_json(*curj);
    }
};

static ordered_json evaluate_expression(const string& expr, const ordered_json& data, bool& was_missing) {
    Parser p(expr, data);
    Value v = p.parse_expr();
    was_missing = v.missing;
    return v.j;
}

} // namespace eval

// Helper: escape a text as a JSON string literal using ordered_json
static string to_json_string_literal(const string& text) {
    return ordered_json(text).dump();
}

// Helper: convert JSON value to plain text to be inserted into a template string
static void append_value_for_template(string& acc, const ordered_json& val) {
    if (val.is_null()) return;
    if (is_undefined_sentinel(val)) return; // treat undefined as empty text in templates
    if (val.is_string()) {
        acc += val.get_ref<const string&>();
    } else {
        acc += val.dump();
    }
}

string Processor::replace_placeholders_impl(const string& s, const ordered_json& data) {
    string out;
    out.reserve(s.size());

    bool in_string = false;
    char delim = 0;
    bool escape = false;

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];

        // Handle backtick template strings with interpolation using $(...)
        if (!in_string && c == '`') {
            string acc; // accumulate plain text content
            bool esc = false;
            size_t j = i + 1;

            while (j < s.size()) {
                char ch = s[j++];
                if (esc) {
                    // Keep escaped char literally
                    acc.push_back(ch);
                    esc = false;
                    continue;
                }
                if (ch == '\\') {
                    esc = true;
                    continue;
                }
                if (ch == '`') {
                    // end of template
                    break;
                }
                if (ch == '$' && j < s.size() && s[j] == '(') {
                    // parse $(...)
                    ++j; // skip '('
                    int depth = 1;
                    bool ex_str_in = false;
                    bool ex_str_esc = false;
                    char ex_delim = 0;
                    size_t expr_start = j;

                    while (j < s.size()) {
                        char ec = s[j++];
                        if (ex_str_in) {
                            if (ex_str_esc) {
                                ex_str_esc = false;
                            } else if (ec == '\\') {
                                ex_str_esc = true;
                            } else if (ec == ex_delim) {
                                ex_str_in = false;
                            }
                            continue;
                        }
                        if (ec == '"' || ec == '\'') {
                            ex_str_in = true;
                            ex_delim = ec;
                            continue;
                        }
                        if (ec == '(') depth++;
                        else if (ec == ')') {
                            depth--;
                            if (depth == 0) break;
                        }
                    }
                    if (j >= s.size()) throw JZError("Unterminated $(...) in template string");
                    size_t expr_end = j - 1;
                    string expr = s.substr(expr_start, expr_end - expr_start);

                    bool missing = false;
                    ordered_json val = eval::evaluate_expression(expr, data, missing);
                    if (!missing && !val.is_null() && !is_undefined_sentinel(val)) {
                        append_value_for_template(acc, val);
                    }
                    // if missing or null or undefined -> append nothing (template treats them as empty)
                    continue;
                }

                // normal char
                acc.push_back(ch);
            }

            if (j >= s.size()) {
                throw JZError("Unterminated template string (`...`)");
            }

            // Emit as a proper JSON string
            out += ordered_json(acc).dump();
            i = j - 1; // position on closing backtick
            continue;
        }

        if (in_string) {
            // Inside normal string (" or ') -> NO interpolation, copy literally
            out.push_back(c);
            if (escape) {
                escape = false;
            } else {
                if (c == '\\') {
                    escape = true;
                } else if (c == delim) {
                    in_string = false;
                    delim = 0;
                }
            }
            continue;
        }

        // Start of normal string?
        if (c == '"' || c == '\'') {
            in_string = true;
            delim = c;
            out.push_back(c);
            continue;
        }

        if (c == '#' && i + 1 < s.size() && s[i + 1] == '{') {
            // Reserved for future
            throw JZError("Encountered '#{...}' directive which is not supported yet");
        }

        if (c == '$' && i + 1 < s.size() && s[i + 1] == '(') {
            // Standalone placeholder -> replace with JSON literal
            size_t j = i + 2;
            int depth = 1;
            bool str_esc = false;
            bool str_in = false;
            char str_delim = 0;

            for (; j < s.size(); ++j) {
                char ch = s[j];
                if (str_in) {
                    if (str_esc) {
                        str_esc = false;
                    } else if (ch == '\\') {
                        str_esc = true;
                    } else if (ch == str_delim) {
                        str_in = false;
                    }
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    str_in = true;
                    str_delim = ch;
                    continue;
                }
                if (ch == '(') depth++;
                else if (ch == ')') {
                    depth--;
                    if (depth == 0) break;
                }
            }
            if (j >= s.size()) throw JZError("Unterminated $(...) placeholder");
            string expr = s.substr(i + 2, j - (i + 2));
            bool missing = false;
            ordered_json val = eval::evaluate_expression(expr, data, missing);
            if (missing) {
                // missing now treated as undefined sentinel
                out += undefined().dump();
            } else {
                if (is_undefined_sentinel(val)) {
                    out += undefined().dump();
                } else {
                    out += val.dump();
                }
            }
            i = j; // skip to ')'
            continue;
        }

        out.push_back(c);
    }

    return out;
}

string Processor::replace_placeholders(const string& s, const ordered_json& data) {
    return replace_placeholders_impl(s, data);
}

// ---------- Remove undefined sentinels from parsed ordered_json ----------
// UPDATED: Arrays now have undefined elements FILTERED OUT (removed) instead of converted to null.
void Processor::remove_undefined_sentinels(ordered_json& j) {
    if (j.is_object()) {
        // Collect keys to erase to avoid modifying while iterating
        vector<string> to_erase;
        for (auto it = j.begin(); it != j.end(); ++it) {
            const string key = it.key();
            ordered_json& val = it.value();
            if (is_undefined_sentinel(val)) {
                to_erase.push_back(key);
            } else {
                remove_undefined_sentinels(val);
            }
        }
        for (const auto& k : to_erase) {
            j.erase(k);
        }
    } else if (j.is_array()) {
        // New logic: remove elements that are sentinel undefined, recursively clean remaining elements.
        ordered_json new_arr = ordered_json::array();
        // new_arr.reserve(j.size());
        for (auto& el : j) {
            if (is_undefined_sentinel(el)) {
                // skip (remove)
                continue;
            }
            remove_undefined_sentinels(el);
            new_arr.push_back(std::move(el));
        }
        j = std::move(new_arr);
    } else {
        // primitives: nothing to do
    }
}

// ---------- Public API ----------
// Modified: now returns ordered_json (ordered_json alias) instead of serialized string.
// This version integrates the expression evaluator which supports tool pipelines.
ordered_json Processor::to_json(const string& jz_input, const ordered_json& data) {
    // 1) Remove comments (supports backtick strings)
    auto no_comments = remove_comments(jz_input);
    // 2) Evaluate placeholders and template string `...` with $(...)
    auto with_values = replace_placeholders(no_comments, data);
    // 3) Normalize JSON5 -> JSON (single quotes, unquoted keys, trailing commas)
    auto jsonish = normalize_json5_to_json(with_values);

    try {
        // 4) Parse
        auto j = ordered_json::parse(jsonish);
        // 5) Remove undefined sentinels and filter arrays
        remove_undefined_sentinels(j);
        return j;
    } catch (const std::exception& e) {
        ostringstream oss;
        oss << "Invalid JSON after JZ transform: " << e.what();
        throw JZError(oss.str());
    }
}

} // namespace jz
