#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

using ordered_json = nlohmann::ordered_json;

namespace jz {

// Tool function signature:
// - input: the value coming in the pipe
// - options: key->value options parsed from parentheses (values are evaluated expressions -> ordered_json)
// - ctx: context object parsed from { ... } block (if provided) or empty object
using ToolFunction = std::function<ordered_json(const ordered_json& input, const ordered_json& options, const ordered_json& ctx)>;

class ToolManager {
public:
	static ToolManager& instance();



	// register a tool by name (lowercase)
	void register_tool(const std::string& name, ToolFunction fn);

	// run a registered tool; throws if not found
	ordered_json run_tool(const std::string& name, const ordered_json& input, const ordered_json& options, const ordered_json& ctx) const;

	// check exists
	bool has_tool(const std::string& name) const;

private:
	ToolManager();
	std::unordered_map<std::string, ToolFunction> registry_;
};

} // namespace jz