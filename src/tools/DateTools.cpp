#include <format>
#include <chrono>
#include "../ToolManager.hpp"
#include "DateTools.hpp"

using namespace std;
using namespace std::chrono;
using namespace jz;

void DateTools::init()
{
	ToolManager& tm = ToolManager::instance();
	tm.register_tool("dateFormat", dateFormat);
}

ordered_json DateTools::dateFormat(const ordered_json& input,const ordered_json& options,const ordered_json& ctx) {
	// input
	long long millis_since_epoch = 0;
	if (input.is_number_integer() || input.is_number_unsigned()) {
		millis_since_epoch = input.get<long long>();
	} else if (input.is_number_float()) {
		millis_since_epoch = static_cast<long long>(input.get<double>());
	} else if (input.is_string()) {
		try { millis_since_epoch = stoll(input.get<string>()); } catch (...) { return ordered_json(nullptr); }
	} else {
		return ordered_json(nullptr);
	}

	// options
	string fmt = "%Y-%m-%d %H:%M:%S";
	if (options.is_object()) {
		if ( options.contains("format") && options["format"].is_string())
			fmt = options["format"].get<string>();
	}

	// Build time_point from milliseconds
	const milliseconds ms{millis_since_epoch};
	system_clock::time_point tp{ms};

	const string& f = format("{{:{}}}", fmt);
	const string& s = std::vformat(f, std::make_format_args(tp));

	return ordered_json(s);
}
