#pragma once

// UE headers before Windows.h: wingdi's TRUE/FALSE macros break the EName enum
#include <UE/Unreal.h>
#include <RE/Oblivion.h>
#include <OBSE/OBSE.h>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifdef ERROR
#	undef ERROR   // wingdi.h; breaks REX::ERROR
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Logger.h"

using json = nlohmann::json;
using namespace std::literals;
