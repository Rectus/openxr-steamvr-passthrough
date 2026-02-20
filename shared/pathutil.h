
#pragma once



std::wstring ToWideString(const std::string_view& input);
std::string ToUTF8String(const std::wstring_view& input);
std::string GetLocalAppData();
std::string GetRoamingAppData();