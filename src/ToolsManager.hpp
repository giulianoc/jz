#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

namespace jz {
    // Tool function signature:
    // - input: the value coming in the pipe
    // - options: key->value options parsed from parentheses (values are evaluated expressions -> ordered_json)
    // - ctx: context object parsed from { ... } block (if provided) or empty object
    using ToolFunction = std::function<ordered_json(const ordered_json &input, const ordered_json &options,
                                                    const ordered_json &ctx, json &metadata)>;

    class ToolsManager {
    public:
        class ToolObject {
        public:
            virtual ~ToolObject() = default;

            virtual ordered_json operator()(const ordered_json &input, const ordered_json &options,
                                            const ordered_json &ctx,
                                            json &metadata) const = 0;
        };

        static ToolsManager &instance();

        void register_tool(const std::string &name, ToolFunction fn);

        void register_tool(const std::string &name, const std::shared_ptr<ToolObject> &tool);

        // run a registered tool; throws if not found
        ordered_json run_tool(const std::string &name, const ordered_json &input, const ordered_json &options,
                              const ordered_json &ctx, json &metadata);

        // check exists
        bool has_tool(const std::string &name);

        template<typename T>
        static T get_option(const ordered_json &options, const std::string &name, const T &defaultValue) {
            if (options.is_object() && options.contains(name))
                return options.at(name).get<T>();
            return defaultValue;
        }
    	template<typename T>
		static optional<T> get_option(const ordered_json &options, const std::string &name) {
        	if (options.is_object() && options.contains(name))
        		return options.at(name).get<T>();
        	return nullopt;
        }

    private:
        ToolsManager() = default;

        ~ToolsManager() = default;

        std::unordered_map<std::string, ToolFunction> _registry;
        std::shared_mutex _registryMutex;
    };
} // namespace jz
