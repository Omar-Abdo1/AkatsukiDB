//
// Created by omarabdo on 6/9/26.
//

#include "../../include/AkatsukiDB/Table/RowSerializer.hpp"
#include <cstring>

std::vector<uint8_t> RowSerializer::Serialize(const DbRow& row, const std::vector<ColumnDefinition>& columns, int rowSizeBytes) {


    std::vector<uint8_t> buffer(rowSizeBytes, 0);

    for (const auto& col : columns) {

        std::span<uint8_t> slice = std::span<uint8_t>(buffer).subspan(col.Offset, col.Size);

        auto it = row.find(col.Name);

        if (it == row.end())
            continue;

        const DbObject& value = it->second;

        if (col.Type == "int") {
            if (std::holds_alternative<int>(value)) {
                WriteInt(slice, std::get<int>(value));
            }
        }
        else if (col.Type == "float") {
            if (std::holds_alternative<double>(value)) {
                WriteDouble(slice, std::get<double>(value));
            }
        }
        else if (col.Type == "bool") {
            if (std::holds_alternative<bool>(value)) {
                slice[0] = std::get<bool>(value) ? 1 : 0;
            }
        }
        else if (col.Type == "str") {
            if (std::holds_alternative<std::string>(value)) {
                WriteString(slice, std::get<std::string>(value));
            }
        }
    }
    return buffer;
}

DbRow RowSerializer::Deserialize(std::span<const uint8_t> data, const std::vector<ColumnDefinition>& columns) {
    DbRow row;

    for (const auto& col : columns) {
        std::span<const uint8_t> slice = data.subspan(col.Offset, col.Size);

        if (col.Type == "int") {
            row[col.Name] = ReadInt(slice);
        }
        else if (col.Type == "float") {
            row[col.Name] = ReadDouble(slice);
        }
        else if (col.Type == "bool") {
            row[col.Name] = static_cast<bool>(slice[0] == 1);
        }
        else if (col.Type == "str") {
            row[col.Name] = ReadString(slice);
        }
    }
    return row;
}

void RowSerializer::WriteDouble(std::span<uint8_t> s, double v) {
    std::memcpy(s.data(), &v, sizeof(double));
}

double RowSerializer::ReadDouble(std::span<const uint8_t> s) {
    double v;
    std::memcpy(&v, s.data(), sizeof(double));
    return v;
}

void RowSerializer::WriteInt(std::span<uint8_t> s, int v) {
    std::memcpy(s.data(), &v, sizeof(int));
}

int RowSerializer::ReadInt(std::span<const uint8_t> s) {
    int v;
    std::memcpy(&v, s.data(), sizeof(int));
    return v;
}

void RowSerializer::WriteString(std::span<uint8_t> s, const std::string& v) {
    int len = static_cast<int>(v.length()); // Write the 4-byte length prefix
    WriteInt(s.subspan(0, 4), len);
    std::memcpy(s.data() + 4, v.data(), len);
    // to it now (len)(string)  (4 o m a r)
}

std::string RowSerializer::ReadString(std::span<const uint8_t> s) {
    int len = ReadInt(s.subspan(0, 4));

    auto textStart = s.begin() + 4;
    auto textEnd = textStart + len;

    return std::string(textStart, textEnd);
}

bool RowSerializer::IsDeleted(std::span<const uint8_t> data, int rowSizeBytes) {
    return data[rowSizeBytes - 10] == 1;
}

void RowSerializer::MarkDeleted(std::span<uint8_t> data, int rowSizeBytes) {
    data[rowSizeBytes - 10] = 1;
}