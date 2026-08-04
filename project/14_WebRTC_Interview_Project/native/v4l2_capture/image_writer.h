#pragma once

#include <cstdint>
#include <string>
#include <vector>

void write_binary_file(const std::string& path, const void* data, std::size_t size);
void write_yuyv_as_ppm(const std::string& path, const std::vector<std::uint8_t>& data, int width, int height);
