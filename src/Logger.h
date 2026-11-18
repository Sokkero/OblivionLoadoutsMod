#pragma once

// Logging to Documents\My Games\Oblivion Remastered\OBSE\Logs\GearLoadouts.log.
// One file, appended across game starts, so the log of a crashed session survives the restart. When it grows past
// kMaxFileSize, the oldest lines are dropped until it is below kTrimmedSize.
// Only events are logged (switches, renames, problems), never per frame.

#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <fstream>
#include <share.h>

namespace logger
{
	inline constexpr std::uintmax_t kMaxFileSize = 50ull * 1024 * 1024;
	inline constexpr std::uintmax_t kTrimmedSize = 15ull * 1024 * 1024;

	template <class... Args>
	void info(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->info(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void warn(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->warn(a_fmt, std::forward<Args>(a_args)...); }
	template <class... Args>
	void error(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args) { spdlog::default_logger_raw()->error(a_fmt, std::forward<Args>(a_args)...); }

	// appends to one file; past kMaxFileSize the oldest lines are cut until it is below kTrimmedSize
	class TrimmingSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit TrimmingSink(std::filesystem::path a_path) :
			_path(std::move(a_path))
		{
			std::error_code ec;
			std::filesystem::create_directories(_path.parent_path(), ec);
			const auto size = std::filesystem::file_size(_path, ec);
			_size = ec ? 0 : size;
			if (_size > kMaxFileSize) {
				Trim();
			}
			Open();
		}

		~TrimmingSink() override
		{
			if (_file) {
				std::fclose(_file);
			}
		}

	protected:
		void sink_it_(const spdlog::details::log_msg& a_msg) override
		{
			spdlog::memory_buf_t buf;
			formatter_->format(a_msg, buf);
			if (_file) {
				_size += std::fwrite(buf.data(), 1, buf.size(), _file);
			}
			if (_size > kMaxFileSize) {
				Trim();
				Open();
			}
		}

		void flush_() override
		{
			if (_file) {
				std::fflush(_file);
			}
		}

	private:
		void Open()
		{
			if (!_file) {
				_file = _wfsopen(_path.c_str(), L"ab", _SH_DENYWR);
			}
		}

		// keeps the newest lines, starting at a line start, so the file ends up below kTrimmedSize
		void Trim()
		{
			if (_file) {
				std::fclose(_file);
				_file = nullptr;
			}
			std::string keep;
			{
				std::ifstream in(_path, std::ios::binary);
				if (!in) {
					_size = 0;
					return;
				}
				in.seekg(0, std::ios::end);
				const std::streamoff total = in.tellg();
				const std::streamoff from = total > static_cast<std::streamoff>(kTrimmedSize) ? total - static_cast<std::streamoff>(kTrimmedSize) : 0;
				in.seekg(from);
				keep.resize(static_cast<std::size_t>(total - from));
				in.read(keep.data(), static_cast<std::streamsize>(keep.size()));
				keep.resize(static_cast<std::size_t>(in.gcount()));
				if (from > 0) {
					const auto nl = keep.find('\n');
					keep.erase(0, nl == std::string::npos ? keep.size() : nl + 1);
				}
			}
			const std::string note = "[older lines were removed to keep this log small]\n";
			std::ofstream out(_path, std::ios::binary | std::ios::trunc);
			out << note << keep;
			_size = note.size() + keep.size();
		}

		std::filesystem::path _path;
		std::FILE*            _file = nullptr;
		std::uintmax_t        _size = 0;
	};

	// call after OBSE::Init with log = false
	inline void init()
	{
		wchar_t* documents = nullptr;
		if (REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &documents) != 0 || !documents) {
			return;   // no file log; spdlog's default console logger stays
		}
		std::filesystem::path dir = documents;
		REX::W32::CoTaskMemFree(documents);
		dir /= std::format("My Games/{}/OBSE/Logs", OBSE::GetSaveFolderName());
		// older versions rotated into GearLoadouts.1.log / .2.log: no longer written
		std::error_code ec;
		std::filesystem::remove(dir / "GearLoadouts.1.log", ec);
		std::filesystem::remove(dir / "GearLoadouts.2.log", ec);
		try {
			auto sink = std::make_shared<TrimmingSink>(dir / "GearLoadouts.log");
			auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
			log->set_level(spdlog::level::info);
			log->flush_on(spdlog::level::info);   // lines are rare; flushed at once so nothing is lost in a crash
			log->set_pattern("[%m-%d %T.%e] [%5t] [%L] %v");
			spdlog::set_default_logger(std::move(log));
			spdlog::default_logger_raw()->info("------------------------------------------------------------ game started");
		} catch (const spdlog::spdlog_ex&) {
		}
	}
}
