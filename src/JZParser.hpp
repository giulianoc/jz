#pragma once

#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>

using std::string;
using std::runtime_error;
using ordered_json = nlohmann::ordered_json;

namespace jz {

struct JZError : public runtime_error {
	explicit JZError(const string& msg, const size_t line_ = 0, const size_t col_ = 0)
		: runtime_error(msg), line(line_), col(col_) {
		//TODO: full_msg = std::format("{} at line {} col {}", msg, line, col);
		full_msg = msg;
	}

	[[nodiscard]] size_t line_no() const noexcept { return line; }
	[[nodiscard]] size_t col_no()  const noexcept { return col; }

	[[nodiscard]] const char* what() const noexcept override {
		return full_msg.c_str();
	}

private:
	size_t line = 0;
	size_t col  = 0;
	std::string full_msg;
};

// Restituisce un valore JSON sentinel che indica "undefined" per JZ.
// Esempio:
//   ordered_json data;
//   data["user"]["middle"] = jz::undefined();
ordered_json undefined();

class Processor {
public:
	// Public API: prende l'input JZ (testo) e la mappa di dati (ordered_json),
	// restituisce l'ordered_json risultante.
	// Lancia jz::JZError in caso di errore.
	static ordered_json to_json(const string& jz_input, const ordered_json& data);

	// Utility pubbliche
	static bool is_identifier_start(char c);
	static bool is_identifier_part(char c);
	static string normalize_json5_to_json(const string& s);

private:
	// Pipeline helpers: trasformazioni testuali (JSON5 -> JSON) e placeholder/template
	static string remove_comments(const string& s);
	static string convert_single_quoted_strings(const string& s);
	static string quote_unquoted_keys(const string& s);
	static string remove_trailing_commas(const string& s);

	// Placeholder/template replacement:
	// - replace_placeholders_impl: implementazione che produce la stringa JSONish con
	//   valori inseriti (uso interno).
	static string replace_placeholders_impl(const string& s, const ordered_json& data);
	static string replace_placeholders(const string& s, const ordered_json& data);

	// Rimozione dei sentinel 'undefined' e pulizia ricorsiva:
	// - rimuove proprietà aventi il sentinel e filtra gli elementi undefined nelle array.
	static void remove_undefined_sentinels(ordered_json& j);
};

} // namespace jz