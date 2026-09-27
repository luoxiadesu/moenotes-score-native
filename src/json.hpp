#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
namespace mns {
struct JsonError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};
class Json {
  public:
    using array_t = std::vector<Json>;
    using object_t = std::map<std::string, Json, std::less<>>;
    using storage = std::variant<std::nullptr_t, bool, int64_t, double, std::string, array_t, object_t>;
    storage data = nullptr;
    Json() = default;
    Json(std::nullptr_t) : data(nullptr) {}
    Json(bool v) : data(v) {}
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    Json(T v) : data(int64_t(v)) {}
    Json(double v) : data(v) {}
    Json(float v) : data(double(v)) {}
    Json(const char *v) : data(std::string(v)) {}
    Json(std::string v) : data(std::move(v)) {}
    Json(std::string_view v) : data(std::string(v)) {}
    Json(array_t v) : data(std::move(v)) {}
    Json(object_t v) : data(std::move(v)) {}
    Json(std::initializer_list<std::pair<const std::string, Json>> fields)
        : data(object_t(fields.begin(), fields.end())) {}
    static Json array() {
        return array_t{};
    }
    static Json array(std::initializer_list<Json> v) {
        return array_t(v);
    }
    static Json object() {
        return object_t{};
    }
    static Json parse(std::string_view text);
    std::string dump(int indent = -1) const;
    bool is_null() const {
        return std::holds_alternative<std::nullptr_t>(data);
    }
    bool is_boolean() const {
        return std::holds_alternative<bool>(data);
    }
    bool is_number_integer() const {
        return std::holds_alternative<int64_t>(data);
    }
    bool is_number() const {
        return is_number_integer() || std::holds_alternative<double>(data);
    }
    bool is_string() const {
        return std::holds_alternative<std::string>(data);
    }
    bool is_array() const {
        return std::holds_alternative<array_t>(data);
    }
    bool is_object() const {
        return std::holds_alternative<object_t>(data);
    }
    bool contains(std::string_view key) const {
        return is_object() && std::get<object_t>(data).contains(key);
    }
    const Json &at(std::string_view key) const {
        if (!is_object())
            throw JsonError("expected object reading " + std::string(key));
        auto it = items().find(key);
        if (it == items().end())
            throw JsonError("missing field: " + std::string(key));
        return it->second;
    }
    Json &at(std::string_view key) {
        return const_cast<Json &>(static_cast<const Json &>(*this).at(key));
    }
    const Json &operator[](std::string_view key) const {
        return at(key);
    }
    Json &operator[](std::string_view key) {
        if (is_null())
            data = object_t{};
        return std::get<object_t>(data)[std::string(key)];
    }
    const Json &at(size_t i) const {
        return std::get<array_t>(data).at(i);
    }
    Json &at(size_t i) {
        return std::get<array_t>(data).at(i);
    }
    const Json &operator[](size_t i) const {
        return at(i);
    }
    Json &operator[](size_t i) {
        return at(i);
    }
    template <class T> T get() const {
        if constexpr (std::is_same_v<T, std::string>)
            return std::get<std::string>(data);
        else if constexpr (std::is_same_v<T, bool>)
            return std::get<bool>(data);
        else if constexpr (std::is_integral_v<T>) {
            const auto value = std::get<int64_t>(data);
            if (!std::in_range<T>(value))
                throw JsonError("JSON integer narrowing out of range");
            return static_cast<T>(value);
        } else if constexpr (std::is_floating_point_v<T>)
            return is_number_integer() ? static_cast<T>(std::get<int64_t>(data))
                                       : static_cast<T>(std::get<double>(data));
        else
            return std::get<T>(data);
    }
    template <class T> T value(std::string_view key, T fallback) const {
        return contains(key) ? at(key).get<T>() : fallback;
    }
    std::string value(std::string_view key, const char *fallback) const {
        return contains(key) ? at(key).get<std::string>() : std::string(fallback);
    }
    const array_t &as_array() const {
        return std::get<array_t>(data);
    }
    array_t &as_array() {
        return std::get<array_t>(data);
    }
    const object_t &items() const {
        return std::get<object_t>(data);
    }
    object_t &items() {
        return std::get<object_t>(data);
    }
    auto begin() {
        return as_array().begin();
    }
    auto end() {
        return as_array().end();
    }
    auto begin() const {
        return as_array().begin();
    }
    auto end() const {
        return as_array().end();
    }
    void push_back(Json v) {
        if (is_null())
            data = array_t{};
        as_array().push_back(std::move(v));
    }
    size_t size() const {
        return is_array()    ? as_array().size()
               : is_object() ? items().size()
               : is_string() ? get<std::string>().size()
                             : 0;
    }
    bool empty() const {
        return size() == 0;
    }
    bool operator==(const Json &) const = default;
};
std::string read_file(const std::string &path);
} // namespace mns
