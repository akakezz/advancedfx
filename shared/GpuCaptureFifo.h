#pragma once
#include <cassert>
#include <cstdint>

// FIFO indices for bounded GPU resource rings. A producer cannot overwrite a
// submitted slot until the consumer removes it; GPU reuse also requires its
// completion event, checked separately by the graphics backend.
template<unsigned Capacity> class GpuCaptureFifo {
    static_assert(Capacity>0,"GPU ring must have at least one slot");
public:
    bool Empty() const { return m_Read==m_Written; }
    bool Full() const { return m_Written-m_Read==Capacity; }
    unsigned WriteSlot() const { assert(!Full()); return unsigned(m_Written%Capacity); }
    unsigned ReadSlot() const { assert(!Empty()); return unsigned(m_Read%Capacity); }
    void Push() { assert(!Full()); ++m_Written; }
    void Pop() { assert(!Empty()); ++m_Read; }
    unsigned long long Written() const { return m_Written; }
    unsigned long long Read() const { return m_Read; }
private:
    unsigned long long m_Written=0,m_Read=0;
};
