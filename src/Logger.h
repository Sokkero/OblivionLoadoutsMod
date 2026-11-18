#pragma once

// Logging to Documents\My Games\Oblivion Remastered\OBSE\Logs\GearLoadouts.log.
// Every game start moves the previous log to GearLoadouts.1.log (and .1 to .2), so the log of a crashed session
// survives the restart. Each file is capped at kMaxFileSize: at most 3 files, 15 MB in total.
// Only events are logged (switches, renames, problems), never per frame.

#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace logger
{
	inline constexpr std::size_t kMaxFileSize = 5 * 1024 * 1024;
	inline constexpr std::size_t kOldFiles = 2;

	template <class... Args>
	void info(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->info(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void warn(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->warn(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void error(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->error(a_fmt, std::forward<Args>(a_args)...); }

	// call after OBSE::Init with log = false
	inline void init()
	{
		wchar_t* documents = nullptr;
		if (REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &documents) != 0 || !documents) {
			return;   // no file log; spdlog's default console logger stays
		}
		std::filesystem::path path = documents;
		REX::W32::CoTaskMemFree(documents);
		path /= std::format("My Games/{}/OBSE/Logs/GearLoadouts.log", OBSE::GetSaveFolderName());
		try {
			auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path.string(), kMaxFileSize, kOldFiles, true);
			auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
			log->set_level(spdlog::level::info);
			log->flush_on(spdlog::level::info);   // lines are rare; flushed at once so nothing is lost in a crash
			log->set_pattern("[%m-%d %T.%e] [%5t] [%L] %v");
			spdlog::set_default_logger(std::move(log));
		} catch (const spdlog::spdlog_ex&) {
		}
	}
}
