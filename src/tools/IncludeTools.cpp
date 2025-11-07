#include "../JZParser.hpp"
#include "IncludeTools.hpp"

using namespace jz;

ordered_json IncludeTools::operator()(const ordered_json &input, const ordered_json &options,
                                      const ordered_json &ctx) const {
    if (input.is_null()) return nullptr;
    string jz_input = getInclude(options);
    const bool contextualInclude = jz_input.empty();
    if (input.is_array()) {
        ordered_json result(ordered_json::array());
        for (auto &item: input) {
            if (!ctx.empty()) {
                ordered_json _item(item);
                _item.merge_patch(ctx);
                if (contextualInclude)
                    jz_input = getInclude(options, _item);
                result.push_back(Processor::to_json(jz_input, _item));
            } else {
                if (contextualInclude)
                    jz_input = getInclude(options, item);
                result.push_back(Processor::to_json(jz_input, item));
            }
        }
        return result;
    }
    if (!ctx.empty()) {
        ordered_json _input(input);
        _input.merge_patch(ctx);
        if (contextualInclude)
            jz_input = getInclude(options, _input);
        return Processor::to_json(jz_input, _input);
    }
    if (contextualInclude)
        jz_input = getInclude(options, input);
    return Processor::to_json(jz_input, input);
}
