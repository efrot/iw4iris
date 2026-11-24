// IW4Iris — minimal self-contained test framework (no dependencies).
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace iris::test
{
    struct Case
    {
        const char* name;
        std::function<void()> body;
    };

    inline std::vector<Case>& Registry()
    {
        static std::vector<Case> cases;
        return cases;
    }

    struct Registrar
    {
        Registrar(const char* name, std::function<void()> body) { Registry().push_back({name, std::move(body)}); }
    };

    struct Failure
    {
        std::string message;
    };

    inline int& Checks()
    {
        static int n = 0;
        return n;
    }

    [[noreturn]] inline void Fail(const char* file, int line, const std::string& what)
    {
        std::ostringstream ss;
        ss << file << ":" << line << ": " << what;
        throw Failure{ss.str()};
    }
}  // namespace iris::test

#define IRIS_CAT2(a, b) a##b
#define IRIS_CAT(a, b) IRIS_CAT2(a, b)
#define TEST_CASE(name)                                                                       \
    static void IRIS_CAT(iris_test_, __LINE__)();                                             \
    static ::iris::test::Registrar IRIS_CAT(iris_reg_, __LINE__)(name, IRIS_CAT(iris_test_, __LINE__)); \
    static void IRIS_CAT(iris_test_, __LINE__)()

#define CHECK(...)                                                               \
    do                                                                           \
    {                                                                            \
        ++::iris::test::Checks();                                                \
        if (!(__VA_ARGS__))                                                      \
            ::iris::test::Fail(__FILE__, __LINE__, std::string("CHECK(") + #__VA_ARGS__ + ") failed"); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                    \
    do                                                                                           \
    {                                                                                            \
        ++::iris::test::Checks();                                                                \
        const double iris_a_ = static_cast<double>(a), iris_b_ = static_cast<double>(b);         \
        if (!(std::abs(iris_a_ - iris_b_) <= (eps)))                                             \
        {                                                                                        \
            std::ostringstream iris_ss_;                                                         \
            iris_ss_.precision(12);                                                              \
            iris_ss_ << "CHECK_NEAR(" #a ", " #b ") failed: " << iris_a_ << " vs " << iris_b_     \
                     << " (eps " << (eps) << ")";                                                \
            ::iris::test::Fail(__FILE__, __LINE__, iris_ss_.str());                              \
        }                                                                                        \
    } while (0)

#define CHECK_VEC_NEAR(a, b, eps)    \
    do                               \
    {                                \
        CHECK_NEAR((a).x, (b).x, eps); \
        CHECK_NEAR((a).y, (b).y, eps); \
        CHECK_NEAR((a).z, (b).z, eps); \
    } while (0)
