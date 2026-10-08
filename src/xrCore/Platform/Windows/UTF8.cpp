#pragma once
#include "stdafx.h"

XRCORE_API wchar_t* Platform::ANSI_TO_TCHAR(const char* C)
{
	static thread_local std::vector<wchar_t> buffer;
	const int needed = C ? MultiByteToWideChar(CP_UTF8, 0, C, -1, nullptr, 0) : 0;
	buffer.assign(std::max(needed, 1), L'\0');
	if (needed) MultiByteToWideChar(CP_UTF8, 0, C, -1, buffer.data(), needed);
	return buffer.data();
}

XRCORE_API xr_string Platform::ANSI_TO_UTF8(const xr_string& ansi)
{
	wchar_t* wcs = nullptr;
	int need_length = MultiByteToWideChar(1251, 0, ansi.c_str(), (int)ansi.size(), wcs, 0);
	wcs = new wchar_t[need_length + 1];
	MultiByteToWideChar(1251, 0, ansi.c_str(), (int)ansi.size(), wcs, need_length);
	wcs[need_length] = L'\0';

	char* u8s = nullptr;
	need_length = WideCharToMultiByte(CP_UTF8, 0, wcs, (int)std::wcslen(wcs), u8s, 0, nullptr, nullptr);
	u8s = new char[need_length + 1];
	WideCharToMultiByte(CP_UTF8, 0, wcs, (int)std::wcslen(wcs), u8s, need_length, nullptr, nullptr);
	u8s[need_length] = '\0';

	xr_string result(u8s);
	delete[] wcs;
	delete[] u8s;
	return result;
}

XRCORE_API xr_string Platform::UTF8_to_CP1251(xr_string const& utf8)
{
	if (!utf8.empty() && IsUTF8(utf8.data()))
	{
		int wchlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), nullptr, 0);
		if (wchlen > 0 && wchlen != 0xFFFD)
		{
			xr_vector<wchar_t> wbuf(wchlen);
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), &wbuf[0], wchlen);
			xr_vector<char> buf(wchlen);
			WideCharToMultiByte(1251, 0, &wbuf[0], wchlen, &buf[0], wchlen, 0, 0);

			return xr_string(&buf[0], wchlen);
		}
	}

	return utf8;
}

XRCORE_API wchar_t* Platform::ANSI_TO_TCHAR_U8(const char* C)
{
	if (IsUTF8(C))
		return ANSI_TO_TCHAR(C);

	return ANSI_TO_TCHAR(ANSI_TO_UTF8(C).c_str());
}

static xr_string from_wide(const wchar_t* input, UINT codepage)
{
	if (!input || !*input) return {};
	const int length = (int)wcslen(input);
	const int size = WideCharToMultiByte(codepage, 0, input, length, nullptr, 0, nullptr, nullptr);
	if (size <= 0) return {};
	xr_string result;
	result.resize(size, '\0');
	if (!WideCharToMultiByte(codepage, 0, input, length, result.data(), size, nullptr, nullptr)) return {};
	return result;
}

XRCORE_API xr_string Platform::TCHAR_TO_ANSI_U8(const wchar_t* input)
{
	// Keep the legacy CP1251 return encoding despite the historical function name.
	return from_wide(input, 1251);
}

XRCORE_API xr_string Platform::CP_TCHAR_TO_ANSI_U8(const wchar_t* input)
{
	return from_wide(input, CP_UTF8);
}
