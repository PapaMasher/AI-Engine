#pragma once

#include "Slate/Core/Base.h"

#include <random>
#include <format>

namespace Slate {

// Random 64-bit identifier used for entities and assets. Zero is reserved as
// the invalid UUID.
class UUID
{
public:
    UUID() = default;
    explicit UUID(UUIDValue value) : m_Value(value) {}

    static UUID Generate()
    {
        static std::mt19937_64 engine(std::random_device{}());
        static std::uniform_int_distribution<UUIDValue> distribution(1, UINT64_MAX);
        return UUID(distribution(engine));
    }

    UUIDValue GetValue() const { return m_Value; }
    explicit operator bool() const { return m_Value != 0; }

    bool operator==(const UUID& other) const { return m_Value == other.m_Value; }
    bool operator!=(const UUID& other) const { return m_Value != other.m_Value; }

    std::string ToString() const { return std::format("{:016x}", m_Value); }

private:
    UUIDValue m_Value = 0;
};

} // namespace Slate
