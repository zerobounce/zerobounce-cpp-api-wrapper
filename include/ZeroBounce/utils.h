#ifndef UTILS_H
#define UTILS_H

#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Thrown when a response field arrives with a JSON type its model does not
 * take, or a field the result needs is missing. Field types are part of the
 * response shape: the request reports it as an API error with the HTTP status.
 */
class ZBJsonFieldTypeError : public std::runtime_error {
public:
    explicit ZBJsonFieldTypeError(const std::string& field)
        : std::runtime_error("ZeroBounce returned the field \"" + field + "\" missing or with the wrong JSON type."),
          field_(field) {}

    const std::string& field() const { return field_; }

private:
    std::string field_;
};

template<typename T>
struct zbIsStringVector : std::false_type {};

template<>
struct zbIsStringVector<std::vector<std::string>> : std::true_type {};

/**
 * Whether value has the JSON type a model field of type T takes: a boolean
 * for bool, an integer for the other integral types, any number for floating
 * point, a string for std::string and the enums (the API sends their names),
 * an array of strings for std::vector<std::string>. Nothing is coerced:
 * nlohmann would read 5.5 as the int 5.
 */
template<typename T>
bool zbJsonHasTypeOf(const json& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value.is_boolean();
    } else if constexpr (std::is_integral_v<T>) {
        return value.is_number_integer();
    } else if constexpr (std::is_floating_point_v<T>) {
        return value.is_number();
    } else if constexpr (std::is_same_v<T, std::string> || std::is_enum_v<T>) {
        return value.is_string();
    } else if constexpr (zbIsStringVector<T>::value) {
        if (!value.is_array()) {
            return false;
        }
        for (const auto& item : value) {
            if (!item.is_string()) {
                return false;
            }
        }
        return true;
    } else {
        return true;
    }
}

/**
 * The string field key of j, which the result cannot do without: present,
 * not null, and a JSON string. Throws ZBJsonFieldTypeError otherwise.
 */
inline std::string zbRequireString(const json& j, const std::string& key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) {
        throw ZBJsonFieldTypeError(key);
    }
    return j[key].get<std::string>();
}

/**
 * Function used to get the value of a given key in a json.
 * If the key doesn't exist or it's null, a default value is returned.
 * A value of another JSON type throws ZBJsonFieldTypeError (see
 * zbJsonHasTypeOf).
 *
 * @tparam T type of the returned value
 * @param j json object
 * @param key name of the field
 * @param defaultValue default value
 * @return T value for the specified field or default value
 */
template<typename T>
T getOrDefault(json j, std::string key, T defaultValue) {
    if (j.count(key) == 0 || j[key].is_null()) {
        return defaultValue;
    }
    if (!zbJsonHasTypeOf<T>(j[key])) {
        throw ZBJsonFieldTypeError(key);
    }
    return j[key].get<T>();
}

/**
 * The message of a file-job answer (send, delete, file status, getfile JSON):
 * a JSON string, or a list of strings joined with "\n". Absent or null is "".
 * Any other JSON type throws ZBJsonFieldTypeError, as every typed field does.
 */
inline std::string zbFileJobMessage(const json& j) {
    if (!j.is_object() || j.count("message") == 0 || j.at("message").is_null()) {
        return "";
    }
    const json& message = j.at("message");
    if (message.is_string()) {
        return message.get<std::string>();
    }
    if (!message.is_array()) {
        throw ZBJsonFieldTypeError("message");
    }
    std::string joined;
    bool first = true;
    for (const auto& item : message) {
        if (!item.is_string()) {
            throw ZBJsonFieldTypeError("message");
        }
        if (!first) {
            joined += "\n";
        }
        joined += item.get<std::string>();
        first = false;
    }
    return joined;
}

#endif