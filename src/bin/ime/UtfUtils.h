#pragma once

#include <string>
#include <string_view>

namespace IME::UtfUtils
{
	std::string WideToUtf8(std::wstring_view text);
	std::wstring Utf8ToWide(std::string_view text);
	std::u32string WideToCodepoints(std::wstring_view text);
}
