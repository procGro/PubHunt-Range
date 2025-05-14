#pragma once
#include <string>
#include <vector>
#include <stdint.h> // For uint64_t, uint8_t

std::vector<uint8_t> hex2bytes(const std::string& s);

int getInt(std::string name, const char* v);

void getInts(std::string name, std::vector<int>& tokens, const std::string& text, char sep);

void parseFile(std::string fileName, std::vector<std::vector<uint8_t>>& inputHashes);

// Helper function declarations for range parsing (as anticipated by Main.cpp)
void parse_range_string(const std::string& range_str, std::string& start_hex, std::string& end_hex);
void N_to_256bit_range(int n, std::string& start_hex, std::string& end_hex);

// Utility for converting a 256-bit number (represented as uint64_t[4]) to a hex string
// arr[0] is LSW, arr[3] is MSW. Output is 64 chars, MSB first.
std::string u64_array_to_hex_string(const uint64_t arr[4]);

// Utility to set a 256-bit number (uint64_t[4]) to the value 2^n
// n can be 0 to 255. arr[0] will be LSW, arr[3] MSW.
void set_u64_array_to_power_of_2(int n, uint64_t arr[4]);

// Utility to set a 256-bit number (uint64_t[4]) to (2^num_bits) - 1
// num_bits can be 1 to 256. arr[0] will be LSW, arr[3] MSW.
void set_u64_array_to_all_ones(int num_bits, uint64_t arr[4]);

