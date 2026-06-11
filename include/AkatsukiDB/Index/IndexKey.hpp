//
// Created by omarabdo on 6/10/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_INDEXKEY_HPP
#define AKATSUKIDB_CPP_INDEXKEY_HPP
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>


#ifdef _MSC_VER
    #include <stdlib.h>
    #define BSWAP_32 _byteswap_ulong
    #define BSWAP_64 _byteswap_uint64
#else
    #define BSWAP_32 __builtin_bswap32
    #define BSWAP_64 __builtin_bswap64
#endif


// ----------------------------------------------------------------------------
// IndexKey – fixed 128‑byte key, comparable, stores int/double/string in big‑endian
// ----------------------------------------------------------------------------

class IndexKey {
public:
    static constexpr size_t Size = 128;

    explicit IndexKey(const std::array<uint8_t, Size>& data);

    explicit IndexKey(std::span<const uint8_t> span);

    template<typename... Args> // like params in c#
    explicit IndexKey(Args... args) : _data{} {
        size_t offset = 0;
        (WriteValue(args, offset), ...); // it just fold expression make in compile time
        if (offset > Size) throw std::runtime_error("IndexKey OVERFLOW");
    }

    IndexKey();

    const std::array<uint8_t, Size>& Data() const { return _data; }

    int CompareTo(const IndexKey& other) const;

    bool operator<(const IndexKey& other) const ;
    bool operator<=(const IndexKey& other) const ;
    bool operator>(const IndexKey& other) const ;
    bool operator>=(const IndexKey& other) const ;
    bool operator==(const IndexKey& other) const ;

    static IndexKey Min();
    static IndexKey Max();
    static IndexKey Max(const IndexKey& a, const IndexKey& b);
    static IndexKey Min(const IndexKey& a, const IndexKey& b);

private:
    std::array<uint8_t, Size> _data;

    void WriteValue(int value, size_t& offset);
    void WriteValue(double value, size_t& offset);
    void WriteValue(const std::string& value, size_t& offset);
};


#endif //AKATSUKIDB_CPP_INDEXKEY_HPP
