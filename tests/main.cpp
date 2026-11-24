// IW4Iris — test runner. Usage: iw4iris_tests [substring-filter]
#include <cstring>
#include <exception>

#include "test.hpp"

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    for (const auto& c : iris::test::Registry())
    {
        if (filter && !std::strstr(c.name, filter))
            continue;
        try
        {
            c.body();
            ++passed;
        }
        catch (const iris::test::Failure& f)
        {
            ++failed;
            std::printf("FAIL  %s\n      %s\n", c.name, f.message.c_str());
        }
        catch (const std::exception& e)
        {
            ++failed;
            std::printf("FAIL  %s\n      unexpected exception: %s\n", c.name, e.what());
        }
    }
    std::printf("%d test cases passed, %d failed, %d checks\n", passed, failed, iris::test::Checks());
    return failed == 0 ? 0 : 1;
}
