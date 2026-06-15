//
// Created by omarabdo on 6/9/26.
//

#include "../../include/AkatsukiDB/Table/RowSerializer.hpp"
#include <cstring>
#include <stdexcept>


// convert the row into bytes
std::vector<uint8_t> RowSerializer::Serialize(const DbRow& row, const std::vector<ColumnDefinition>& columns, int rowSizeBytes) {


    std::vector<uint8_t> buffer(rowSizeBytes, 0);

    // row -> [row bytes][10 byte systems]
    // 10 byte ->  [9 bytes for NULL][delete flag]

    int footerStart = rowSizeBytes - 10;
    int nullBitmapStart = footerStart;        // 9 bytes from footerStart to footerStart+8 , last byte for delete

    // 9 bytes then 72 bit
    uint8_t nullBitmap[9] = {0};

    for (int colIdx=0;colIdx<columns.size();colIdx++) {
        auto &col = columns[colIdx];
        std::span<uint8_t> slice = std::span<uint8_t>(buffer).subspan(col.Offset, col.Size);

        auto it = row.find(col.Name);
        bool isNull = (it == row.end() || std::holds_alternative<std::monostate>(it->second));

        if (isNull) {
            int bytePos = colIdx/8;
            int bitPos = colIdx%8;
            nullBitmap[bytePos]|=(1LL<<bitPos);
            continue;
        }

        const DbObject& value = it->second;

        if (col.Type == "int") {
            int intVal = 0;
            if (std::holds_alternative<int>(value)) {
                intVal = std::get<int>(value);
            } else if (std::holds_alternative<double>(value)) {
                intVal = static_cast<int>(std::get<double>(value));   // truncates (5.52 → 5)
            } else if (std::holds_alternative<bool>(value)) {
                intVal = std::get<bool>(value) ? 1 : 0;
            } else if (std::holds_alternative<std::string>(value)) {
                try { intVal = std::stoi(std::get<std::string>(value)); }
                catch (...) { intVal = 0; }
            }
            WriteInt(slice, intVal);
        }
        else if (col.Type == "float") {
            double doubleVal = 0.0;
            if (std::holds_alternative<double>(value)) {
                doubleVal = std::get<double>(value);
            } else if (std::holds_alternative<int>(value)) {
                doubleVal = static_cast<double>(std::get<int>(value)); // 9000 → 9000.0
            } else if (std::holds_alternative<bool>(value)) {
                doubleVal = std::get<bool>(value) ? 1.0 : 0.0;
            } else if (std::holds_alternative<std::string>(value)) {
                try { doubleVal = std::stod(std::get<std::string>(value)); }
                catch (...) { doubleVal = 0.0; }
            }
            WriteDouble(slice, doubleVal);
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

    std::memcpy(buffer.data() + nullBitmapStart, nullBitmap, 9);

    return buffer;
}


// convert bytes into DBRow
DbRow RowSerializer::Deserialize(std::span<const uint8_t> data, const std::vector<ColumnDefinition>& columns) {
    DbRow row;

    int rowSize = static_cast<int>(data.size());
    int footerStart = rowSize - 10;
    int nullBitmapStart = footerStart;               // 9 bytes

    // Read the 9-byte null bitmap
    const uint8_t* nullBitmap = data.data() + nullBitmapStart;

    for (size_t colIdx = 0; colIdx < columns.size(); ++colIdx) {
        const auto& col = columns[colIdx];
        int bytePos = colIdx / 8;
        int bitPos = colIdx % 8;
        bool isNull = (nullBitmap[bytePos] & (1LL << bitPos)) != 0;

        if (isNull) {
            row[col.Name] = std::monostate{};
            continue;
        }

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
    return data[rowSizeBytes - 1] == 1;
}

void RowSerializer::MarkDeleted(std::span<uint8_t> data, int rowSizeBytes) {
    data[rowSizeBytes - 1] = 1;
}