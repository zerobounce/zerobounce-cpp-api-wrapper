#ifndef ZBS3LISTFILEENTRY_H
#define ZBS3LISTFILEENTRY_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

class ZBS3ListFileEntry {
    public:
        std::string name;
        int size = 0;
        std::string lastModified;

        static ZBS3ListFileEntry from_json(const json& j);
        static std::vector<ZBS3ListFileEntry> listFromJson(const json& j);

        bool operator==(const ZBS3ListFileEntry& other) const;
};

#endif
