#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <type_traits>

struct Writer {
    std::vector<uint8_t> data;

    size_t size() const { return data.size(); }

    template<typename T>
    void put(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
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
        put<uint16_t>(static_cast<uint16_t>(s.size()));
        bytes(s.data(), s.size());
    }
};

struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t at = 0;

    Reader() = default;
    Reader(const uint8_t* d, size_t n) : data(d), size(n) {}

    bool ok(size_t need) const { return at + need <= size; }

    template<typename T>
    T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (ok(sizeof(T))) {
            std::memcpy(&value, data + at, sizeof(T));
            at += sizeof(T);
        }
        return value;
    }

    void bytes(void* destination, size_t length) {
        if (ok(length)) {
            std::memcpy(destination, data + at, length);
            at += length;
        }
    }

    std::string text() {
        uint16_t n = get<uint16_t>();
        std::string s;
        if (ok(n)) { s.assign(reinterpret_cast<const char*>(data + at), n); at += n; }
        return s;
    }
};

struct WriteArchive {
    Writer& writer;
    template<typename T> void value(T& v) {
        if constexpr (std::is_enum_v<T>)            writer.put(static_cast<std::underlying_type_t<T>>(v));
        else if constexpr (std::is_arithmetic_v<T>) writer.put(v);
        else                                        v.serialize(*this);
    }
    template<typename T> WriteArchive& operator&(T& v) { value(v); return *this; }
    void text(std::string& s) { writer.text(s); }
    template<typename Count, typename T> void vector(std::vector<T>& v) {
        writer.put(static_cast<Count>(v.size()));
        for (auto& element : v) value(element);
    }
    template<typename Length> void blob(std::vector<uint8_t>& b) {
        writer.put(static_cast<Length>(b.size()));
        if (!b.empty()) writer.bytes(b.data(), b.size());
    }
};

struct ReadArchive {
    Reader& reader;
    template<typename T> void value(T& v) {
        if constexpr (std::is_enum_v<T>)            v = static_cast<T>(reader.get<std::underlying_type_t<T>>());
        else if constexpr (std::is_arithmetic_v<T>) v = reader.get<T>();
        else                                        v.serialize(*this);
    }
    template<typename T> ReadArchive& operator&(T& v) { value(v); return *this; }
    void text(std::string& s) { s = reader.text(); }
    template<typename Count, typename T> void vector(std::vector<T>& v) {
        Count n = reader.get<Count>();
        v.clear(); v.resize(n);
        for (auto& element : v) value(element);
    }
    template<typename Length> void blob(std::vector<uint8_t>& b) {
        Length n = reader.get<Length>();
        b.resize(n);
        if (n) reader.bytes(b.data(), n);
    }
};

namespace util {

template<typename T> void encode(Writer& writer, T& message) { WriteArchive a{writer}; message.serialize(a); }
template<typename T> T    decode(Reader& reader)             { T m{}; ReadArchive a{reader}; m.serialize(a); return m; }

}
