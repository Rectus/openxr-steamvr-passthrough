#include "pch.h"
#include "camera_enumerator.h"
#include <mfidl.h>
#include <mfapi.h>
#include <mfreadwrite.h>
#include <strsafe.h>


void CameraEnumerator::EnumerateCameras(std::vector<std::string>& deviceList)
{
    uint32_t deviceCount = 0;
    IMFAttributes* attributes = NULL;
    IMFActivate** devices = NULL;

    deviceList.clear();

    HRESULT result = MFCreateAttributes(&attributes, 1);

    if (FAILED(result))
    {
        return;
    }

    result = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);


    if (FAILED(result))
    {
        attributes->Release();
        return;
    }

    result = MFEnumDeviceSources(attributes, &devices, &deviceCount);

    if (FAILED(result))
    {
        CoTaskMemFree(devices);
        attributes->Release();
        return;
    }

    for (uint32_t i = 0; i < deviceCount; i++)
    {
        WCHAR* displayName = NULL;
        char buffer[128] = { 0 };

        UINT32 nameLength;
        HRESULT result = devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &displayName, &nameLength);

        if (SUCCEEDED(result) && displayName)
        {
            WideCharToMultiByte(CP_UTF8, 0, displayName, nameLength, buffer, 127, NULL, NULL);
            deviceList.push_back(buffer);
        }
        else
        {
            deviceList.push_back("ERROR");
        }
        CoTaskMemFree(displayName);
    }


    for (uint32_t i = 0; i < deviceCount; i++)
    {
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    attributes->Release();
}