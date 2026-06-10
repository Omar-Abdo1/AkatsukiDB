//
// Created by omarabdo on 6/10/26.
//

#include "../../include/AkatsukiDB/Index/IndexKey.hpp"

IndexKey::IndexKey(const std::array<uint8_t, Size> &data):_data(data) {}

IndexKey::IndexKey(std::span<const uint8_t> span) {
 if (span.size() != Size) throw std::runtime_error("Invalid key size");
 std::copy(span.begin(), span.end(), _data.begin());
}

IndexKey::IndexKey():_data{} {}

int IndexKey::CompareTo(const IndexKey &other) const {
 for (size_t i=0;i<Size;i++) {
  if (_data[i]<other._data[i]) return -1;
  if (_data[i]>other._data[i]) return 1;
 }
 return 0;
}

bool IndexKey::operator<(const IndexKey &other) const {
return CompareTo(other) < 0;
}

bool IndexKey::operator<=(const IndexKey &other) const {
return CompareTo(other) <= 0;
}

bool IndexKey::operator>(const IndexKey &other) const {
return CompareTo(other) > 0;
}

bool IndexKey::operator>=(const IndexKey &other) const {
return CompareTo(other) >= 0;
}

bool IndexKey::operator==(const IndexKey &other) const {
return CompareTo(other) == 0;
}

IndexKey IndexKey::Min() {return IndexKey();}

IndexKey IndexKey::Max() {
IndexKey result;
 result._data.fill(0xFF); // 255
 return result;
}

IndexKey IndexKey::Max(const IndexKey &a, const IndexKey &b) {
 return a>=b ? a : b;
}

IndexKey IndexKey::Min(const IndexKey &a, const IndexKey &b) {
return a<=b ? a : b;
}

void IndexKey::WriteValue(int value, size_t &offset) {
 if (offset + 4 > Size) throw std::runtime_error("IndexKey Overflow");

 //Cast to unsigned to safely manipulate bits
 uint32_t unsigned_val = static_cast<uint32_t>(value);

 //Flip the sign bit so negative numbers sort before positive numbers
 unsigned_val ^= (1U<<(31));

 // Convert to Big-Endian
 unsigned_val = BSWAP_32(unsigned_val);

 std::memcpy(_data.data() + offset, &unsigned_val, 4);
 offset += 4;
}

void IndexKey::WriteValue(double value, size_t &offset) {
 if (offset + 8 > Size) throw std::runtime_error("IndexKey Overflow");

 uint64_t bits;
 std::memcpy(&bits, &value, 8); // Type Pun

 //Apply the IEEE 754
 if (bits&(1ULL<<(63))) { // negative
  //flip ALL bits to reverse the sort order
  bits = ~bits;
 } else {
  // Number is positive , just flip the sign
  bits ^=(1ULL<<(63));
 }

 // Convert to Big-Endian
 bits = BSWAP_64(bits);

 std::memcpy(_data.data() + offset, &bits, 8);
 offset += 8;
}

void IndexKey::WriteValue(const std::string &value, size_t &offset) {
 if (offset + value.size() > Size) throw std::runtime_error("IndexKey Overflow");
 std::memcpy(_data.data() + offset, value.data(), value.size());
 offset += value.size();
}
