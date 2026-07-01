#include "UtfUtils.h"

#include <Windows.h>

namespace
{
	std::string WideToUtf8Impl(std::wstring_view text, DWORD flags)
	{
		if (text.empty()) {
			return {};
		}

		const int required = WideCharToMultiByte(
			CP_UTF8,
			flags,
			text.data(),
			static_cast<int>(text.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (required <= 0) {
			return {};
		}

		std::string utf8(static_cast<std::size_t>(required), '\0');
		const int converted = WideCharToMultiByte(
			CP_UTF8,
			flags,
			text.data(),
			static_cast<int>(text.size()),
			utf8.data(),
			required,
			nullptr,
			nullptr);
		if (converted <= 0) {
			return {};
		}

		return utf8;
	}

	std::wstring Utf8ToWideImpl(std::string_view text, DWORD flags)
	{
		if (text.empty()) {
			return {};
		}

		const int required = MultiByteToWideChar(
			CP_UTF8,
			flags,
			text.data(),
			static_cast<int>(text.size()),
			nullptr,
			0);
		if (required <= 0) {
			return {};
		}

		std::wstring wide(static_cast<std::size_t>(required), L'\0');
		const int converted = MultiByteToWideChar(
			CP_UTF8,
			flags,
			text.data(),
			static_cast<int>(text.size()),
			wide.data(),
			required);
		if (converted <= 0) {
			return {};
		}

		return wide;
	}
}

namespace IME::UtfUtils
{
	std::string WideToUtf8(std::wstring_view text)
	{
		std::string utf8 = WideToUtf8Impl(text, WC_ERR_INVALID_CHARS);
		if (!utf8.empty() || text.empty()) {
			return utf8;
		}

		return WideToUtf8Impl(text, 0);
	}

	std::wstring Utf8ToWide(std::string_view text)
	{
		std::wstring wide = Utf8ToWideImpl(text, MB_ERR_INVALID_CHARS);
		if (!wide.empty() || text.empty()) {
			return wide;
		}

		return Utf8ToWideImpl(text, 0);
	}

	std::u32string WideToCodepoints(std::wstring_view text)
	{
		std::u32string codepoints;
		codepoints.reserve(text.size());

		for (std::size_t i = 0; i < text.size(); ++i) {
			const auto current = static_cast<std::uint16_t>(text[i]);
			if (current >= 0xD800 && current <= 0xDBFF) {
				if (i + 1 < text.size()) {
					const auto next = static_cast<std::uint16_t>(text[i + 1]);
					if (next >= 0xDC00 && next <= 0xDFFF) {
						const std::uint32_t codepoint =
							0x10000u + (((static_cast<std::uint32_t>(current) - 0xD800u) << 10u) |
								(static_cast<std::uint32_t>(next) - 0xDC00u));
						codepoints.push_back(static_cast<char32_t>(codepoint));
						++i;
						continue;
					}
				}

				codepoints.push_back(U'\uFFFD');
				continue;
			}

			if (current >= 0xDC00 && current <= 0xDFFF) {
				codepoints.push_back(U'\uFFFD');
				continue;
			}

			codepoints.push_back(static_cast<char32_t>(current));
		}

		return codepoints;
	}
}
