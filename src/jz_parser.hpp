#pragma once

#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>

using std::string;
using std::runtime_error;
using nlohmann::json;

namespace jz {

struct JZError : public runtime_error {
    using runtime_error::runtime_error;
};

// Restituisce un valore JSON sentinel che indica "undefined" per JZ.
// Devi usare questa funzione quando costruisci la mappa `data` per
// indicare che il valore associato a una chiave è `undefined`.
// Esempio:
//   json data;
//   data["user"]["middle"] = jz::undefined();
json undefined();

class Processor {
public:
    // Converte una stringa JZ in una stringa JSON valida.
    // data: mappa di dati (json) da cui risolvere i path nelle espressioni $(...)
    // throw JZError in caso di errore di parsing/valutazione.
    //
    // Regola di interpolazione stringhe:
    // - Interpolazione attiva SOLO per stringhe delimitate da backtick: `Ciao $(user.name)!`
    // - Stringhe "..." e '...' NON interpolano: "Ciao $(user.name)!" resta letterale.
    static string to_json(const string& jz_input, const json& data);

private:
    // Pipeline
    static string remove_comments(const string& s);
    static string replace_placeholders(const string& s, const json& data);
    static string normalize_json5_to_json(const string& s);

    // Helpers JSON5 -> JSON
    static string convert_single_quoted_strings(const string& s);
    static string quote_unquoted_keys(const string& s);
    static string remove_trailing_commas(const string& s);

    // Placeholder evaluation
    static string replace_placeholders_impl(const string& s, const json& data);

    // After parsing to json, remove object properties that have the undefined sentinel,
    // and FILTER OUT undefined sentinel inside arrays (remove elements) — recursive.
    static void remove_undefined_sentinels(json& j);

    // Utilities
    static bool is_identifier_start(char c);
    static bool is_identifier_part(char c);
};

} // namespace jz