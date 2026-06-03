#pragma once

template <typename T, int N>
struct FixedBuffer {
    T data[N];
    int count = 0;

    [[nodiscard]] auto full() const -> bool {
        return count >= N;
    }
    [[nodiscard]] auto size() const -> int {
        return count;
    }

    void push(const T& v) {
        if (count < N) {
            data[count++] = v;
        }
    }
    void clear() {
        count = 0;
    }

    [[nodiscard]] auto begin() const -> const T* {
        return data;
    }
    [[nodiscard]] auto end() const -> const T* {
        return data + count;
    }
    auto begin() -> T* {
        return data;
    }
    auto end() -> T* {
        return data + count;
    }
};
