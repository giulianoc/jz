#pragma once
#include <nlohmann/json_fwd.hpp>

using ordered_json = nlohmann::ordered_json;

namespace jz
{
class DateTools
{
public:
	static void init();

private:
	static ordered_json dateFormat(const ordered_json& input,const ordered_json& options,const ordered_json& ctx, json &metadata);

    static ordered_json millis(const ordered_json &input, const ordered_json &options, const ordered_json &ctx, json &metadata);
};
}
