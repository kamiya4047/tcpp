#pragma once
#include <stdexcept>
#include <string>
#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)
