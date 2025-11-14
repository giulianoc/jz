#include "../JZParser.hpp"
#include "../ToolsManager.hpp"
#include "IncludeTools.hpp"

using namespace jz;

void IncludeTools::init() {
    ToolsManager &tm = ToolsManager::instance();
    tm.register_tool("merge", merge);
}

/**
 * Merge the context object into the input object using JSON Merge Patch.
 *
 * @param input The input JSON object.
 * @param options Options (not used in this function).
 * @param ctx Context object to merge into the input.
 * @param metadata Metadata (not used in this function).
 * @return The merged JSON object.
 */
ordered_json IncludeTools::merge(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                 json &metadata) {
    if (ctx.empty()) return input;
    if (input.is_object()) {
        ordered_json result(input);
        result.merge_patch(ctx);
        return result;
    }
    return input;
    /* TODO
    if (input.is_array()) {
        ordered_json result(ordered_json::array());
        for (auto &item: input) {
            ordered_json _item(item);
            _item.merge_patch(ctx);
            result.push_back(_item);
        }
        return result;
    }*/
}

ordered_json IncludeTools::operator()(const ordered_json &input, const ordered_json &options,
                                      const ordered_json &ctx, json &metadata) const {
    if (input.is_null()) return nullptr;
    string jz_input = getInclude(options, nullptr, metadata);
    const bool contextualInclude = jz_input.empty();
    if (input.is_array()) {
        ordered_json result(ordered_json::array());
        for (auto &item: input) {
            if (!ctx.empty()) {
                ordered_json _item(item);
                _item.merge_patch(ctx);
                if (contextualInclude)
                    jz_input = getInclude(options, _item, metadata);
                result.push_back(Processor::to_json(jz_input, _item, metadata));
            } else {
                if (contextualInclude)
                    jz_input = getInclude(options, item, metadata);
                result.push_back(Processor::to_json(jz_input, item, metadata));
            }
        }
        return result;
    }
    if (!ctx.empty()) {
        ordered_json _input(input);
        _input.merge_patch(ctx);
        if (contextualInclude)
            jz_input = getInclude(options, _input, metadata);
        return Processor::to_json(jz_input, _input, metadata);
    }
    if (contextualInclude)
        jz_input = getInclude(options, input, metadata);
    return Processor::to_json(jz_input, input, metadata);
}
