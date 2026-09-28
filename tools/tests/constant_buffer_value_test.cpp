#include "../../src/Layers/xrRenderDX10/ConstantBufferValue.h"
#include <cassert>
#include <cstdint>

int main()
{
    // Updating a packed float2 must preserve neighbouring scalar constants.
    std::uint32_t buffer[] = {11, 0, 0, 22};
    const std::uint32_t vector[] = {0x3f800000, 0x40000000};
    assert(StoreConstantBufferValue(buffer + 1, vector, sizeof(vector)));
    assert(!StoreConstantBufferValue(buffer + 1, vector, sizeof(vector)));
    assert(buffer[0] == 11 && buffer[3] == 22);
    const std::uint32_t negativeZero = 0x80000000, positiveZero = 0;
    assert(StoreConstantBufferValue(buffer + 1, &negativeZero, 4));
    assert(StoreConstantBufferValue(buffer + 1, &positiveZero, 4));
    const std::uint32_t nan = 0x7fc00001;
    assert(StoreConstantBufferValue(buffer + 1, &nan, 4));
    assert(!StoreConstantBufferValue(buffer + 1, &nan, 4));
    assert(buffer[2] == vector[1] && buffer[3] == 22);
    return 0;
}
