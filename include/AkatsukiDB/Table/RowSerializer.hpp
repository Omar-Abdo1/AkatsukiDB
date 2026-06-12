//
// Created by omarabdo on 6/9/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_ROWSERIALIZER_HPP
#define AKATSUKIDB_CPP_ROWSERIALIZER_HPP
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "ColumnDefinition.hpp"

class RowSerializer {

public:
    static std::vector<uint8_t> Serialize(const DbRow& row, const std::vector<ColumnDefinition>& columns, int rowSizeBytes);

    static DbRow Deserialize(std::span<const uint8_t> data, const std::vector<ColumnDefinition>& columns);

    static bool IsDeleted(std::span<const uint8_t> data, int rowSizeBytes);
    static void MarkDeleted(std::span<uint8_t> data, int rowSizeBytes);

private:
    static void WriteInt(std::span<uint8_t> s, int v);
    static int ReadInt(std::span<const uint8_t> s);

    static void WriteDouble(std::span<uint8_t> s, double v);
    static double ReadDouble(std::span<const uint8_t> s);

    static void WriteString(std::span<uint8_t> s, const std::string& v);
    static std::string ReadString(std::span<const uint8_t> s);

};


#endif //AKATSUKIDB_CPP_ROWSERIALIZER_HPP
