#pragma once
#include <nlohmann/json_fwd.hpp>

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

namespace jz {
    class ArrayTools {
    public:
        static void init();

    private:
        static ordered_json length(const ordered_json &input, const ordered_json &options, const ordered_json &ctx, json &metadata);
    };
}
