#include "../../src/Layers/xrRender/PipMotionShaderContract.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>

static unsigned checks = 0;
static void Check(bool value)
{
    ++checks;
    if (!value)
    {
        std::cerr << "Failed shader contract check " << checks << '\n';
        std::exit(1);
    }
}

static std::vector<char> Read(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    Check(input.good());
    return std::vector<char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "Expected authored r3 helper, authored r4 helper, legacy helper\n";
        return 2;
    }
    auto r3 = Read(argv[1]);
    const auto r4 = Read(argv[2]);
    auto legacy = Read(argv[3]);
    Check(r3 == r4);
    Check(IsPipMotionOwnerShaderCompatible(r3.data(), r3.size()));
    Check(IsPipMotionOwnerShaderCompatible(r4.data(), r4.size()));
    std::vector<char> windows;
    for (char c : r3)
    {
        if (c == '\r') continue;
        if (c == '\n') windows.push_back('\r');
        windows.push_back(c);
    }
    Check(IsPipMotionOwnerShaderCompatible(windows.data(), windows.size()));
    windows.push_back('\r');
    Check(!IsPipMotionOwnerShaderCompatible(windows.data(), windows.size()));
    Check(!IsPipMotionOwnerShaderCompatible(nullptr, 0));
    Check(!IsPipMotionOwnerShaderCompatible(nullptr, r3.size()));
    Check(!IsPipMotionOwnerShaderCompatible(r3.data(), std::numeric_limits<std::size_t>::max()));
    Check(!IsPipMotionOwnerShaderCompatible(legacy.data(), legacy.size()));
    // A legacy file of the expected byte length must still fail the content test.
    legacy.resize(r3.size(), ' ');
    Check(!IsPipMotionOwnerShaderCompatible(legacy.data(), legacy.size()));
    for (std::size_t size = 0; size < r3.size(); ++size)
        Check(!IsPipMotionOwnerShaderCompatible(r3.data(), size));
    for (std::size_t index = 0; index < r3.size(); ++index)
    {
        const auto original = static_cast<unsigned char>(r3[index]);
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            r3[index] = static_cast<char>(original ^ (1u << bit));
            Check(!IsPipMotionOwnerShaderCompatible(r3.data(), r3.size()));
        }
        r3[index] = static_cast<char>(original);
    }
    Check(IsPipMotionOwnerShaderCompatible(r3.data(), r3.size()));
    r3.push_back('\n');
    Check(!IsPipMotionOwnerShaderCompatible(r3.data(), r3.size()));
    std::cout << "PiP motion shader contract: " << checks << " checks passed\n";
}
