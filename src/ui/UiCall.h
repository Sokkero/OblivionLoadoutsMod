#pragma once

// Calling Unreal functions through reflection: parameters are laid out by name from the UFunction's properties.
// CommonLibOB64 declares no UMG types, so every UI call goes through here. Game thread only.
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md).

#include <string>
#include <string_view>
#include <vector>

namespace ui
{
	class Call
	{
	public:
		Call(UE::UObject* a_obj, const wchar_t* a_fn);
		explicit operator bool() const { return m_fn != nullptr; }

		void*        At(std::string_view a_name);   // the parameter's place in the frame, nullptr when absent
		std::int32_t Size(std::string_view a_name) const;

		template <class T>
		bool Set(std::string_view a_name, const T& a_value)
		{
			void* p = At(a_name);
			if (p) {
				std::memcpy(p, &a_value, sizeof(T));
			}
			return p != nullptr;
		}

		template <class T>
		T Get(std::string_view a_name)
		{
			T v{};
			if (void* p = At(a_name)) {
				std::memcpy(&v, p, sizeof(T));
			}
			return v;
		}

		bool Run();
		bool RunGuarded();   // SEH-guarded, for world-context calls that can fault during a quit or load

	private:
		UE::UObject*              m_obj;
		UE::UFunction*            m_fn;
		std::vector<std::uint8_t> m_params;
	};

	// a call whose first parameter is given as raw bytes
	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size);

	template <class T>
	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const T& a_value)
	{
		return CallFirst(a_obj, a_fn, &a_value, sizeof(T));
	}

	UE::UClass*  Class(const wchar_t* a_path);   // a loaded class by path, nullptr when not loaded
	UE::UObject* PlayerController();             // cached; rescanned at most every 2 s
	std::string  Name(UE::UObject* a_object);    // the object's FName, "-" for nullptr
}
