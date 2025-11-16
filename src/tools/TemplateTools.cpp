#include "../JZParser.hpp"
#include "../ToolsManager.hpp"
#include "TemplateTools.hpp"

using namespace jz;

void TemplateTools::init() {
    ToolsManager &tm = ToolsManager::instance();
    tm.register_tool("merge", merge);
    tm.register_tool("vars", vars);
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
ordered_json TemplateTools::merge(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
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

/**
 * Create a new context by adding the input as a variable or merged in the context.
 *
 * @param input The input JSON object.
 * @param options Options containing the "key" to use for the variable.
 * @param ctx The existing context object.
 * @param metadata Metadata (not used in this function).
 * @return The new context object with the input added as a variable (or merged, if options "key" is not present).
 */
ordered_json TemplateTools::vars(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                 json &metadata) {
    if (!input.is_null()) {
        if (options.contains("key")) {
            ordered_json _ctx;
            _ctx[options["key"].get<string>()] = input;
            _ctx.merge_patch(ctx);
            return _ctx;
        }
        if (!input.empty()) {
            ordered_json _ctx(input);
            _ctx.merge_patch(ctx);
            return _ctx;
        }
    }
    return ctx;
}

/**
 * Process the input JSON object using the include tool.
 *
 * @param input The input JSON object.
 * @param options Options for the include tool.
 * @param ctx Context object to merge into each item.
 * @param metadata Metadata for processing.
 * @return The processed JSON object.
 */
ordered_json TemplateTools::operator()(const ordered_json &input, const ordered_json &options,
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
