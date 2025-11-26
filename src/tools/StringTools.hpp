#pragma once
#include <nlohmann/json_fwd.hpp>
#include <functional>

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

namespace jz {
    class StringTools {
    public:
        static void init();

    private:
        static ordered_json upper(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                  json &metadata);

        static ordered_json lower(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                  json &metadata);

        static ordered_json capitalize(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                       json &metadata);

        static ordered_json trim(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                 json &metadata);

        static ordered_json dirname(const ordered_json &input, const ordered_json &options, const ordered_json &ctx,
                                    json &metadata);

        static ordered_json _traverse(const std::function<std::string(std::string s)> &operation,
                                      const ordered_json &input,
                                      const ordered_json &options, const ordered_json &ctx);
    };
}
