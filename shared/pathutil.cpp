
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

std::string GetProcessFileName(bool bIncludeExtension)
{
    wchar_t buffer[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::string filePath = ToUTF8String(buffer);
    std::string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);
    if (bIncludeExtension)
    {
        return fileName;
    }
    else
    {
        return std::string(fileName.substr(0, fileName.find_last_of(".")));
    }
}

bool CreateDirectoryPath(const std::string_view& path)
{
    return std::filesystem::create_directories((char8_t const*)path.data());
}

bool EnsurePathForFile(const std::string_view& filePath, std::error_code* errorCode)
{
    std::filesystem::path dirPath((char8_t const*)filePath.data());
    dirPath.remove_filename();

    std::error_code retCode;

    if(std::filesystem::create_directories(dirPath, retCode) || retCode.value() == 0)
    {
        return true;
    }

    if (errorCode != nullptr)
    {
        *errorCode = retCode;
    }

    return false;
}
