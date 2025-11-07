#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

using ordered_json = nlohmann::ordered_json;

namespace jz {
    // Tool function signature:
    // - input: the value coming in the pipe
    // - options: key->value options parsed from parentheses (values are evaluated expressions -> ordered_json)
    // - ctx: context object parsed from { ... } block (if provided) or empty object
    using ToolFunction = std::function<ordered_json(const ordered_json &input, const ordered_json &options,
                                                    const ordered_json &ctx)>;

    class ToolsManager {
    public:
        static ToolsManager &instance();


        // register a tool by name (lowercase)
        void register_tool(const std::string &name, ToolFunction fn);

        template<typename ToolObject>
        void register_tool(const std::string &name, ToolObject tool) {
            _registry[name] = [tool = std::move(tool)](const ordered_json &input,
                                                       const ordered_json &options,
                                                       const ordered_json &ctx) mutable {
                return tool(input, options, ctx);
            };
        }

        // run a registered tool; throws if not found
        ordered_json run_tool(const std::string &name, const ordered_json &input, const ordered_json &options,
                              const ordered_json &ctx);

        // check exists
        bool has_tool(const std::string &name);

    private:
        ToolsManager() = default;

        ~ToolsManager() = default;

        static std::string normalize_tool_name(const std::string &s);

        std::unordered_map<std::string, ToolFunction> _registry;
        std::shared_mutex _registryMutex;
    };
} // namespace jz
