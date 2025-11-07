#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>

using namespace std;

using ordered_json = nlohmann::ordered_json;

namespace jz {
    class IncludeTools {
    public:
        IncludeTools() = default;

        virtual ~IncludeTools() = default;

        ordered_json operator()(const ordered_json &input, const ordered_json &options, const ordered_json &ctx) const;

    protected:
        virtual std::string getInclude(const ordered_json &options, const ordered_json &data = nullptr) const = 0;
    };
}
