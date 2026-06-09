#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

static_assert(
    std::endian::native == std::endian::little ||
    std::endian::native == std::endian::big,
    "serialize.h requires host to be purely little or big endian"
);

namespace serialize {

template <typename T>
static void wire_swap(T& value) {
    if constexpr (sizeof(T) > 1) {
        if constexpr (std::endian::native == std::endian::big) {
            auto* bytes = reinterpret_cast<uint8_t*>(&value);
            std::reverse(bytes, bytes + sizeof(T));
        }
    }
}

struct Writer {
    std::vector<uint8_t> data;

    Writer() = default;
    explicit Writer(size_t reserve) {
        data.reserve(reserve);
    }

    [[nodiscard]] auto size() const -> size_t {
        return data.size();
    }
    void clear() {
        data.clear();
    }

    template <typename T>
    void put(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        wire_swap(value);
        size_t at = data.size();
        data.resize(at + sizeof(T));
        std::memcpy(data.data() + at, &value, sizeof(T));
    }

    void bytes(const void* source, size_t length) {
        size_t at = data.size();
        data.resize(at + length);
        std::memcpy(data.data() + at, source, length);
    }

    void text(const std::string& s) {
        auto n = static_cast<uint16_t>(std::min<size_t>(s.size(), std::numeric_limits<uint16_t>::max()));
        put<uint16_t>(n);
        bytes(s.data(), n);
    }
};

struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t at = 0;
    bool failed = false;

    Reader() = default;
    Reader(const uint8_t* d, size_t n) : data(d), size(n) {}
    explicit Reader(std::span<const uint8_t> bytes) : data(bytes.data()), size(bytes.size()) {}

    [[nodiscard]] auto remaining() const -> size_t {
        if (at > size) {
            return 0;
        }
        return size - at;
    }
    [[nodiscard]] auto valid() const -> bool {
        return !failed;
    }
    void fail() {
        failed = true;
    }
    auto ok(size_t need) -> bool {
        if (failed || at + need > size) {
            failed = true;
            return false;
        }
        return true;
    }

    template <typename T>
    auto get() -> T {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (ok(sizeof(T))) {
            std::memcpy(&value, data + at, sizeof(T));
            at += sizeof(T);
            wire_swap(value);
        }
        return value;
    }

    void bytes(void* destination, size_t length) {
        if (ok(length)) {
            std::memcpy(destination, data + at, length);
            at += length;
        }
    }

    auto text() -> std::string {
        auto n = get<uint16_t>();
        std::string s;
        if (ok(n)) {
            s.assign(reinterpret_cast<const char*>(data + at), n);
            at += n;
        }
        return s;
    }
};

struct WriteArchive {
    Writer& writer;
    template <typename T>
    void value(T& v) {
        if constexpr (std::is_enum_v<T>) {
            writer.put(static_cast<std::underlying_type_t<T>>(v));
        } else if constexpr (std::is_arithmetic_v<T>) {
            writer.put(v);
        } else {
            v.serialize(*this);
        }
    }
    template <typename T>
    auto operator&(T& v) -> WriteArchive& {
        value(v);
        return *this;
    }
    void text(std::string& s) {
        writer.text(s);
    }
    template <typename Count, typename T>
    void vector(std::vector<T>& v) {
        size_t n = std::min<size_t>(v.size(), std::numeric_limits<Count>::max());
        writer.put(static_cast<Count>(n));
        for (size_t i = 0; i < n; ++i) {
            value(v[i]);
        }
    }
    template <typename Length>
    void blob(std::vector<uint8_t>& b) {
        size_t n = std::min<size_t>(b.size(), std::numeric_limits<Length>::max());
        writer.put(static_cast<Length>(n));
        if (n > 0) {
            writer.bytes(b.data(), n);
        }
    }
    template <typename Count>
    void strings(std::vector<std::string>& v) {
        size_t n = std::min<size_t>(v.size(), std::numeric_limits<Count>::max());
        writer.put(static_cast<Count>(n));
        for (size_t i = 0; i < n; ++i) {
            writer.text(v[i]);
        }
    }
};

struct ReadArchive {
    Reader& reader;
    template <typename T>
    void value(T& v) {
        if constexpr (std::is_enum_v<T>) {
            v = static_cast<T>(reader.get<std::underlying_type_t<T>>());
        } else if constexpr (std::is_arithmetic_v<T>) {
            v = reader.get<T>();
        } else {
            v.serialize(*this);
        }
    }
    template <typename T>
    auto operator&(T& v) -> ReadArchive& {
        value(v);
        return *this;
    }
    void text(std::string& s) {
        s = reader.text();
    }
    template <typename Count, typename T>
    void vector(std::vector<T>& v) {
        auto n = reader.get<Count>();
        v.clear();
        if (n > reader.remaining()) {
            reader.fail();
            return;
        }
        v.resize(n);
        for (auto& element : v) {
            value(element);
        }
    }
    template <typename Length>
    void blob(std::vector<uint8_t>& b) {
        auto n = reader.get<Length>();
        b.clear();
        if (n > reader.remaining()) {
            reader.fail();
            return;
        }
        b.resize(n);
        if (n != 0) {
            reader.bytes(b.data(), n);
        }
    }
    template <typename Count>
    void strings(std::vector<std::string>& v) {
        auto n = reader.get<Count>();
        v.clear();
        if (n > reader.remaining()) {
            reader.fail();
            return;
        }
        v.resize(n);
        for (auto& s : v) {
            s = reader.text();
        }
    }
};

template <typename T>
void encode(Writer& writer, T& message) {
    WriteArchive a{writer};
    message.serialize(a);
}
template <typename T>
auto decode(Reader& reader) -> T {
    T m{};
    ReadArchive a{reader};
    m.serialize(a);
    return m;
}

}
