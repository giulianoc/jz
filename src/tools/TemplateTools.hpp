#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>
#include "../ToolsManager.hpp"

using namespace std;

using ordered_json = nlohmann::ordered_json;

namespace jz {
    class TemplateTools : public ToolsManager::ToolObject {
    public:
        TemplateTools() = default;

        static void init();

        // include tool
        ordered_json operator()(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                json &metadata) const override;

    protected:
        [[nodiscard]] virtual std::string getInclude(const ordered_json &options, const ordered_json &data,
                                                     json &metadata) const = 0;

    private:
        static ordered_json merge(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                  json &metadata);
    };
}
