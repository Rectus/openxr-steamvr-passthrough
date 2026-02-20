
#include "pch.h"
#include "pathutil.h"

#include <pathcch.h>
#include <shlobj_core.h>


std::wstring ToWideString(const std::string_view& input)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.length()), NULL, 0);

    std::wstring output = std::wstring(length, '\0');

    if (MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.length()), output.data(), length) == 0)
    {
        return std::wstring();
    }

    return output;
}

std::string ToUTF8String(const std::wstring_view& input)
{
    int length = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.length()), NULL, 0, NULL, NULL);

    std::string output = std::string(length, '\0');

    if (WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.length()), output.data(), length, NULL, NULL) == 0)
    {
        return std::string();
    }

    return output;
}


std::string GetLocalAppData()
{
    PWSTR path;

    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path);

    std::string output = ToUTF8String(std::wstring_view(path));

    CoTaskMemFree(path);

    return output;
}

std::string GetRoamingAppData()
{
    PWSTR path;

    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path);

    std::string output = ToUTF8String(std::wstring_view(path));

    CoTaskMemFree(path);

    return output;
}