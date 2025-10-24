#include "StringTools.hpp"
#include "../ToolManager.hpp"

#include <iostream>
#include <sstream>

using namespace std;
using namespace jz;

void StringTools::init()
{
	ToolManager& tm = ToolManager::instance();
	// register default tools: upper, lower, capitalize
	tm.register_tool("upper", upper);
	tm.register_tool("lower", lower);
	tm.register_tool("capitalize", capitalize);
}

ordered_json StringTools::upper(const ordered_json& input,const ordered_json& options,const ordered_json& ctx) {
	// cerr << "upper:" << input.dump() << endl;
	if (input.is_null()) return nullptr;
	string s;
	if (input.is_string()) s = input.get<string>();
	else s = input.dump(); // TODO: dare eccezione perché ci aspettiamo una stringa?
	ranges::transform(s, s.begin(), [](const unsigned char c){ return std::toupper(c); });
	return s;
}

ordered_json StringTools::lower(const ordered_json& input,const ordered_json& options,const ordered_json& ctx) {
	if (input.is_null()) return nullptr;
	string s;
	if (input.is_string()) s = input.get<string>();
	else s = input.dump();
	ranges::transform(s, s.begin(), [](const unsigned char c){ return std::tolower(c); });
	return s;
}

ordered_json StringTools::capitalize(const ordered_json& input,const ordered_json& options,const ordered_json& ctx) {
	if (input.is_null()) return nullptr;
	string s;
	if (input.is_string()) s = input.get<string>();
	else s = input.dump();

	bool firstOnly = false;
	if (options.is_object()) {
		if ( options.contains("firstOnly") && options["firstOnly"].is_boolean())
			firstOnly = options["firstOnly"].get<bool>();
	}

	if (firstOnly)
	{
		ranges::transform(s, s.begin(), [](const unsigned char c){ return std::tolower(c); });
		s[0] = std::toupper(static_cast<unsigned char>(s[0]));
		return s;
	} else
	{
		std::istringstream iss(s);
		std::ostringstream oss;
		std::string word;
		bool firstWord = true;
		while (iss >> word) {
			if (!word.empty()) {
				word[0] = std::toupper(static_cast<unsigned char>(word[0]));
				std::transform(word.begin() + 1, word.end(), word.begin() + 1,
							   [](unsigned char c) { return std::tolower(c); });
			}
			if (!firstWord) oss << ' ';
			oss << word;
			firstWord = false;
		}

		return oss.str();
	}
}
