#include "Utils.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <string>
#include <string.h>
#include <iomanip>   // For std::setw, std::setfill
#include <stdexcept> // For std::invalid_argument, std::out_of_range

//using namespace std;

std::vector<uint8_t> hex2bytes(const std::string& s)
{
	constexpr size_t width = sizeof(uint8_t) * 2;
	std::vector<uint8_t> v;
	v.reserve((s.size() + width - 1) / width);
	for (auto it = s.crbegin(); it < s.crend(); it += width)
	{
		auto begin = std::min(s.crend(), it + width).base();
		auto end = it.base();
		std::string slice(begin, end);
		uint8_t value = std::stoul(slice, 0, 16);
		v.push_back(value);
	}
	std::reverse(v.begin(), v.end());
	return v;
}

int getInt(std::string name, const char* v) {

	int r;

	try {

		r = std::stoi(std::string(v));

	}
	catch (std::invalid_argument&) {

		printf("Invalid %s argument, number expected\n", name.c_str());
		exit(-1);

	}

	return r;

}

void getInts(std::string name, std::vector<int>& tokens, const std::string& text, char sep)
{
	size_t start = 0, end = 0;
	tokens.clear();
	int item;

	try {

		while ((end = text.find(sep, start)) != std::string::npos) {
			item = std::stoi(text.substr(start, end - start));
			tokens.push_back(item);
			start = end + 1;
		}

		item = std::stoi(text.substr(start));
		tokens.push_back(item);

	}
	catch (std::invalid_argument&) {

		printf("Invalid %s argument, number expected\n", name.c_str());
		exit(-1);

	}
}

void parseFile(std::string fileName, std::vector<std::vector<uint8_t>>& inputHashes)
{
	inputHashes.clear();
	// Check file
	FILE* fp = fopen(fileName.c_str(), "rb");
	if (fp == NULL) {
		::printf("Error: Cannot open %s %s\n", fileName.c_str(), strerror(errno));
	}
	else {
		fclose(fp);
		int nbLine = 0;
		std::string line;
		std::ifstream inFile(fileName);
		while (getline(inFile, line)) {
			// Remove ending \r\n
			int l = (int)line.length() - 1;
			while (l >= 0 && isspace(line.at(l))) {
				line.pop_back();
				l--;
			}
			if (line.length() > 0) {
				auto ret = hex2bytes(line);
				if (ret.size() == 20) {
					inputHashes.push_back(ret);
				}
				else {
					::printf("Error: Cannot read hash at line %d, \n", nbLine);
				}
			}
			nbLine++;
		}
		//::printf("Loaded range history: %d\n", nbLine);
	}
}

void trim(std::string& s) {
    // Placeholder implementation
    s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));
    s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    // Placeholder implementation
    std::vector<std::string> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

void getInts(const char* name, std::vector<int>& v, const std::string& str, char separator) {
    // Placeholder implementation - this should parse str into v
    v.clear();
    std::vector<std::string> parts = split(str, separator);
    for(const auto& p : parts) {
        try {
            v.push_back(std::stoi(p));
        } catch(const std::exception& e) {
            //fprintf(stderr, "Warning: could not parse int '%s' for %s\n", p.c_str(), name);
        }
    }
}

// Implementation for u64_array_to_hex_string
// arr[0] is LSW, arr[3] is MSW. Output is 64 chars, MSB of number first.
std::string u64_array_to_hex_string(const uint64_t arr[4]) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 3; i >= 0; --i) { // Iterate from MSW (arr[3]) to LSW (arr[0])
        oss << std::setw(16) << arr[i];
    }
    return oss.str();
}

// Implementation for set_u64_array_to_power_of_2
// n can be 0 to 255. arr[0] will be LSW, arr[3] MSW.
void set_u64_array_to_power_of_2(int n, uint64_t arr[4]) {
    std::fill(arr, arr + 4, 0ULL); // Initialize all to 0
    if (n < 0 || n > 255) {
        // Invalid bit position, array remains 0
        return;
    }
    int arr_idx = n / 64;       // Which uint64_t in the array (0-3)
    int bit_pos_in_u64 = n % 64; // Bit position within that uint64_t (0-63)
    arr[arr_idx] = 1ULL << bit_pos_in_u64;
}

// Implementation for set_u64_array_to_all_ones
// num_bits can be 1 to 256. Fills `num_bits` least significant bits with 1.
void set_u64_array_to_all_ones(int num_bits, uint64_t arr[4]) {
    std::fill(arr, arr + 4, 0ULL);
    if (num_bits <= 0) return;
    if (num_bits > 256) num_bits = 256;

    int current_bit = 0;
    for (int i = 0; i < 4; ++i) { // LSW to MSW
        if (current_bit >= num_bits) break;
        int bits_in_this_word = std::min(64, num_bits - current_bit);
        if (bits_in_this_word == 64) {
            arr[i] = 0xFFFFFFFFFFFFFFFFULL;
        } else {
            arr[i] = (1ULL << bits_in_this_word) - 1;
        }
        current_bit += bits_in_this_word;
    }
}

// Implementation for N_to_256bit_range
// As per user: -bits N means range [2^N, 2^(N+1) - 1]
void N_to_256bit_range(int n_param, std::string& start_hex, std::string& end_hex) {
    uint64_t temp_arr[4]; // Reusable temporary array

    if (n_param < 0 || n_param > 255) { 
        fprintf(stderr, "Warning: N for --bits should be effectively between 0 and 254 for a [2^N, 2^(N+1)-1] range up to 256 bits. N=%d is out of typical bounds.\n", n_param);
        std::fill(temp_arr, temp_arr + 4, 0ULL);
        start_hex = u64_array_to_hex_string(temp_arr);
        std::fill(temp_arr, temp_arr + 4, 0xFFFFFFFFFFFFFFFFULL); // Max value for end_hex on error
        end_hex = u64_array_to_hex_string(temp_arr);
        return;
    }

    // Calculate start = 2^N
    set_u64_array_to_power_of_2(n_param, temp_arr);
    start_hex = u64_array_to_hex_string(temp_arr);

    // Calculate end = 2^(N+1) - 1
    if (n_param == 255) { // Special case: end is 2^256 - 1 (all FFs)
        std::fill(temp_arr, temp_arr + 4, 0xFFFFFFFFFFFFFFFFULL);
    } else {
        // For 2^(N+1) - 1, we need N+1 bits set to 1.
        set_u64_array_to_all_ones(n_param + 1, temp_arr);
    }
    end_hex = u64_array_to_hex_string(temp_arr);
}

// Implementation for parse_range_string
void parse_range_string(const std::string& range_str, std::string& start_hex, std::string& end_hex) {
    // Default to full range in case of any error
    uint64_t arr_zero[4]; std::fill(arr_zero, arr_zero+4, 0ULL);
    uint64_t arr_max[4]; std::fill(arr_max, arr_max+4, 0xFFFFFFFFFFFFFFFFULL);
    start_hex = u64_array_to_hex_string(arr_zero);
    end_hex = u64_array_to_hex_string(arr_max);

    size_t colon_pos = range_str.find(':');
    if (colon_pos == std::string::npos) {
        fprintf(stderr, "Error: Invalid range format. Expected <start_hex>:<end_hex>. Using default full range.\n");
        return;
    }

    std::string s_hex = range_str.substr(0, colon_pos);
    std::string e_hex = range_str.substr(colon_pos + 1);

    trim(s_hex); // Assuming trim is defined elsewhere in Utils.cpp
    trim(e_hex);

    bool s_valid = (s_hex.length() == 64) && std::all_of(s_hex.begin(), s_hex.end(), ::isxdigit);
    bool e_valid = (e_hex.length() == 64) && std::all_of(e_hex.begin(), e_hex.end(), ::isxdigit);

    if (s_valid && e_valid) {
        start_hex = s_hex;
        end_hex = e_hex;
    } else {
        if (s_hex.length() != 64 || e_hex.length() != 64){
            fprintf(stderr, "Error: Range hex strings must be 64 characters long. Using default full range.\n");
        }
        if (!std::all_of(s_hex.begin(), s_hex.end(), ::isxdigit) || !std::all_of(e_hex.begin(), e_hex.end(), ::isxdigit)){
             fprintf(stderr, "Error: Range hex strings contain non-hex characters. Using default full range.\n");
        }       
    }
}