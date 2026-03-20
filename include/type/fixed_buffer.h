#pragma once

template<typename T, int N>
struct FixedBuffer {
    T data[N];
    int count = 0;

    void push(const T& v)  { if (count < N) data[count++] = v; }
    void clear()           { count = 0; }

    bool full() const      { return count >= N; }
    int size() const       { return count; }

    const T* begin() const { return data; }
    const T* end()   const { return data + count; }
    T* begin()             { return data; }
    T* end()               { return data + count; }
};
