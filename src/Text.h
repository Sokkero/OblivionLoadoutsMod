#pragma once

// UTF-8 / UTF-16 / UTF-32 conversion for loadout names.

#include <optional>
#include <string>

namespace text
{
	inline std::wstring Wide(const std::string& a_utf8)
	{
		const int    n = MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
		std::wstring out(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
		if (n > 0) {
			MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), out.data(), n);
		}
		return out;
	}

	inline std::string Utf8(const wchar_t* a_data, int a_len)
	{
		const int   n = a_data && a_len > 0 ? WideCharToMultiByte(CP_UTF8, 0, a_data, a_len, nullptr, 0, nullptr, nullptr) : 0;
		std::string out(n > 0 ? static_cast<std::size_t>(n) : 0, '\0');
		if (n > 0) {
			WideCharToMultiByte(CP_UTF8, 0, a_data, a_len, out.data(), n, nullptr, nullptr);
		}
		return out;
	}

	inline bool IsHighSurrogate(char32_t a_c) { return a_c >= 0xD800 && a_c < 0xDC00; }
	inline bool IsLowSurrogate(char32_t a_c) { return a_c >= 0xDC00 && a_c < 0xE000; }
	inline char32_t Combine(char32_t a_high, char32_t a_low) { return 0x10000 + ((a_high - 0xD800) << 10) + (a_low - 0xDC00); }

	// nullopt on invalid UTF-8
	inline std::optional<std::u32string> Decode(const std::string& a_utf8)
	{
		if (a_utf8.empty()) {
			return std::u32string{};
		}
		const int size = static_cast<int>(a_utf8.size());
		const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_utf8.data(), size, nullptr, 0);
		if (n <= 0) {
			return std::nullopt;
		}
		std::wstring w(static_cast<std::size_t>(n), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_utf8.data(), size, w.data(), n);
		std::u32string out;
		for (std::size_t i = 0; i < w.size(); ++i) {
			const char32_t c = static_cast<char16_t>(w[i]);
			if (IsHighSurrogate(c) && i + 1 < w.size()) {
				out.push_back(Combine(c, static_cast<char16_t>(w[++i])));
			} else {
				out.push_back(c);
			}
		}
		return out;
	}

	inline std::string Encode(const std::u32string& a_cps)
	{
		std::string out;
		for (const char32_t cp : a_cps) {
			if (cp < 0x80) {
				out += static_cast<char>(cp);
			} else if (cp < 0x800) {
				out += static_cast<char>(0xC0 | (cp >> 6));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			} else if (cp < 0x10000) {
				out += static_cast<char>(0xE0 | (cp >> 12));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			} else {
				out += static_cast<char>(0xF0 | (cp >> 18));
				out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			}
		}
		return out;
	}

	inline bool IsControl(char32_t a_cp) { return a_cp < 0x20 || (a_cp >= 0x7F && a_cp < 0xA0); }
}
