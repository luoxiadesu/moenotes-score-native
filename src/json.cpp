#include "json.hpp"
#include "yyjson.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
namespace mns {
static Json from_value(yyjson_val *v, unsigned depth = 0) {
    if (depth > 64)
        throw JsonError("JSON nesting exceeds 64");
    if (yyjson_is_null(v))
        return nullptr;
    if (yyjson_is_bool(v))
        return yyjson_get_bool(v);
    if (yyjson_is_uint(v)) {
        auto n = yyjson_get_uint(v);
        if (n > INT64_MAX)
            throw JsonError("JSON integer exceeds int64");
        return int64_t(n);
    }
    if (yyjson_is_sint(v))
        return yyjson_get_sint(v);
    if (yyjson_is_real(v)) {
        auto n = yyjson_get_real(v);
        if (!std::isfinite(n))
            throw JsonError("nonfinite JSON number");
        return n;
    }
    if (yyjson_is_str(v))
        return std::string(yyjson_get_str(v), yyjson_get_len(v));
    Json out = yyjson_is_arr(v) ? Json::array() : Json::object();
    size_t i, max;
    yyjson_val *key, *val;
    if (yyjson_is_arr(v)) {
        yyjson_arr_foreach(v, i, max, val) out.push_back(from_value(val, depth + 1));
    } else {
        yyjson_obj_foreach(v, i, max, key, val) {
            std::string k(yyjson_get_str(key), yyjson_get_len(key));
            if (out.contains(k))
                throw JsonError("duplicate JSON key: " + k);
            out[k] = from_value(val, depth + 1);
        }
    }
    return out;
}
Json Json::parse(std::string_view text) {
    if (text.size() > 64 * 1024 * 1024)
        throw JsonError("JSON input exceeds 64 MiB");
    yyjson_read_err error{};
    auto *doc = yyjson_read_opts(const_cast<char *>(text.data()), text.size(), 0, nullptr, &error);
    if (!doc) {
        if (error.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION)
            throw std::bad_alloc();
        throw JsonError(std::string("JSON parse: ") + (error.msg ? error.msg : "invalid"));
    }
    try {
        auto result = from_value(yyjson_doc_get_root(doc));
        yyjson_doc_free(doc);
        return result;
    } catch (...) {
        yyjson_doc_free(doc);
        throw;
    }
}
static yyjson_mut_val *to_value(yyjson_mut_doc *d, const Json &j) {
    if (j.is_null())
        return yyjson_mut_null(d);
    if (j.is_boolean())
        return yyjson_mut_bool(d, j.get<bool>());
    if (j.is_number_integer())
        return yyjson_mut_sint(d, j.get<int64_t>());
    if (j.is_number()) {
        auto v = j.get<double>();
        if (!std::isfinite(v))
            throw JsonError("nonfinite result");
        return yyjson_mut_real(d, v);
    }
    if (j.is_string()) {
        auto &s = std::get<std::string>(j.data);
        return yyjson_mut_strncpy(d, s.data(), s.size());
    }
    auto *v = j.is_array() ? yyjson_mut_arr(d) : yyjson_mut_obj(d);
    if (!v)
        throw std::bad_alloc();
    if (j.is_array()) {
        for (auto &x : j)
            if (!yyjson_mut_arr_append(v, to_value(d, x)))
                throw std::bad_alloc();
    } else
        for (auto &[key, value] : j.items())
            if (!yyjson_mut_obj_add(v, yyjson_mut_strncpy(d, key.data(), key.size()), to_value(d, value)))
                throw std::bad_alloc();
    return v;
}
std::string Json::dump(int indent) const {
    auto *d = yyjson_mut_doc_new(nullptr);
    if (!d)
        throw std::bad_alloc();
    try {
        yyjson_mut_doc_set_root(d, to_value(d, *this));
        size_t len = 0;
        auto *p = yyjson_mut_write(d, indent < 0 ? 0 : YYJSON_WRITE_PRETTY, &len);
        if (!p)
            throw std::bad_alloc();
        std::unique_ptr<char, decltype(&std::free)> buffer(p, std::free);
        std::string result(buffer.get(), len);
        yyjson_mut_doc_free(d);
        return result;
    } catch (...) {
        yyjson_mut_doc_free(d);
        throw;
    }
}
std::string read_file(const std::string &path) {
    std::ifstream s(path, std::ios::binary);
    if (!s)
        throw JsonError("cannot read " + path);
    s.seekg(0, std::ios::end);
    auto n = s.tellg();
    if (n < 0 || n > 64 * 1024 * 1024)
        throw JsonError("file exceeds 64 MiB");
    s.seekg(0);
    std::string result(size_t(n), '\0');
    if (!s.read(result.data(), n))
        throw JsonError("cannot read " + path);
    return result;
}
} // namespace mns
