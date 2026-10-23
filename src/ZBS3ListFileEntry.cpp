#include "ZeroBounce/ZBS3ListFileEntry.h"

#include "ZeroBounce/utils.h"

ZBS3ListFileEntry ZBS3ListFileEntry::from_json(const json& j) {
    ZBS3ListFileEntry entry;
    entry.name = getOrDefault<std::string>(j, "name", "");
    entry.size = getOrDefault<int>(j, "size", 0);
    entry.lastModified = getOrDefault<std::string>(j, "last_modified", "");
    return entry;
}

std::vector<ZBS3ListFileEntry> ZBS3ListFileEntry::listFromJson(const json& j) {
    std::vector<ZBS3ListFileEntry> entries;
    if (!j.is_array()) {
        return entries;
    }
    for (const auto& item : j) {
        entries.push_back(from_json(item));
    }
    return entries;
}

bool ZBS3ListFileEntry::operator==(const ZBS3ListFileEntry& other) const {
    return name == other.name && size == other.size && lastModified == other.lastModified;
}
