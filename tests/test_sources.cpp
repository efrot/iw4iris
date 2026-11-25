// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Source hygiene checks that the compilers do not all enforce.
#include <filesystem>
#include <fstream>
#include <string>

#include "test.hpp"

namespace fs = std::filesystem;

TEST_CASE("sources: code outside comments is ASCII (MSVC reads sources in the system code page)")
{
    const fs::path root(IW4IRIS_SOURCE_DIR);
    int files = 0;
    for (const char* dir : {"core/src", "core/include", "engine/iw4x/src", "launcher/src"})
        for (const auto& e : fs::recursive_directory_iterator(root / dir))
        {
            const auto ext = e.path().extension();
            if (ext != ".cpp" && ext != ".hpp" && ext != ".h")
                continue;
            ++files;
            std::ifstream in(e.path(), std::ios::binary);
            bool block = false;
            int lineNo = 0;
            for (std::string line; std::getline(in, line);)
            {
                ++lineNo;
                std::string code;
                for (std::size_t i = 0; i < line.size(); ++i)
                {
                    if (block)
                    {
                        if (line.compare(i, 2, "*/") == 0)
                        {
                            block = false;
                            ++i;
                        }
                        continue;
                    }
                    if (line.compare(i, 2, "//") == 0)
                        break;
                    if (line.compare(i, 2, "/*") == 0)
                    {
                        block = true;
                        ++i;
                        continue;
                    }
                    code += line[i];
                }
                bool ascii = true;
                for (const char c : code)
                    ascii = ascii && static_cast<unsigned char>(c) < 0x80;
                CHECK_MSG(ascii, e.path().string() + ":" + std::to_string(lineNo));
            }
        }
    CHECK(files > 40);
}
