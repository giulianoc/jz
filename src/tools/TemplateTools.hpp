#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>

using namespace std;

using ordered_json = nlohmann::ordered_json;

namespace jz {
    class TemplateTools {
    public:
        TemplateTools() = default;

        virtual ~TemplateTools() = default;

        static void init();

        // include tool
        ordered_json operator()(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                json &metadata) const;

    protected:
        [[nodiscard]] virtual std::string getInclude(const ordered_json &options, const ordered_json &data,
                                                     json &metadata) const = 0;

    private:
        static ordered_json merge(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                  json &metadata);

        static ordered_json vars(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                 json &metadata);
    };
}
