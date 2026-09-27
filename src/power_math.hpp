#ifndef MNS_POWER_MATH_HPP
#define MNS_POWER_MATH_HPP

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace mns::power_detail {

// The client retains signed low bits. Unsigned arithmetic avoids C++ signed
// overflow UB; bit_cast specifies the two's-complement interpretation.
inline std::int64_t wrap_add(std::int64_t a, std::int64_t b) {
    return std::bit_cast<std::int64_t>(std::uint64_t(a) + std::uint64_t(b));
}
inline std::int64_t wrap_mul(std::int64_t a, std::int64_t b) {
    return std::bit_cast<std::int64_t>(std::uint64_t(a) * std::uint64_t(b));
}
inline std::int32_t wrap32(std::int64_t n) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n));
}
inline std::int64_t floor32(float value) {
    const double result = std::floor(static_cast<double>(value));
    if (!std::isfinite(result) || result < -2147483648.0 || result > 2147483647.0)
        throw std::invalid_argument("power attribute outside int32 gameplay range");
    return static_cast<std::int64_t>(result);
}
struct Power {
    std::array<std::int64_t, 3> bp{};
    Power() = default;
    explicit Power(std::int64_t n) : bp{n, n, n} {}
    explicit Power(std::array<std::int64_t, 3> n) : bp(n) {}
    static Power points(std::array<std::int64_t, 3> n) {
        for (auto &value : n)
            value = wrap_mul(value, 10000);
        return Power(n);
    }
    static Power points(std::int64_t n) {
        return points({n, n, n});
    }
    Power &operator+=(const Power &other) {
        for (std::size_t i = 0; i < 3; ++i)
            bp[i] = wrap_add(bp[i], other.bp[i]);
        return *this;
    }
    friend Power operator+(Power a, const Power &b) {
        return a += b;
    }
    friend Power operator*(Power a, const Power &b) {
        for (std::size_t i = 0; i < 3; ++i)
            a.bp[i] = wrap_mul(a.bp[i], b.bp[i]) / 10000;
        return a;
    }
    Power to_floor() const {
        std::array<std::int64_t, 3> out{};
        for (std::size_t i = 0; i < 3; ++i)
            out[i] = floor32(static_cast<float>(bp[i]) / 10000.0f);
        return points(out);
    }
    std::array<std::int64_t, 3> integer() const {
        std::array<std::int64_t, 3> out{};
        for (std::size_t i = 0; i < 3; ++i)
            out[i] = static_cast<std::int64_t>(std::floor(static_cast<double>(bp[i]) / 10000.0));
        return out;
    }
    std::int64_t total() const {
        const auto p = integer();
        return p[0] + p[1] + p[2];
    }
};
inline Power growth(std::array<std::int64_t, 3> maxima, std::array<std::int64_t, 3> rates, int method) {
    std::array<std::int64_t, 3> points{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (maxima[i] < INT32_MIN || maxima[i] > INT32_MAX || rates[i] < INT32_MIN || rates[i] > INT32_MAX)
            throw std::invalid_argument("growth master field outside int32");
        float value{};
        if (method == 1) {
            const float rate = static_cast<float>(rates[i]) / 10000.0f;
            value = rate * static_cast<float>(maxima[i]);
        } else {
            const auto product = maxima[i] * rates[i];
            const auto native_product = method == 2 ? std::int64_t(wrap32(product)) : product;
            value = static_cast<float>(native_product) / 10000.0f;
        }
        points[i] = floor32(value);
    }
    return Power::points(points);
}
} // namespace mns::power_detail
#endif
