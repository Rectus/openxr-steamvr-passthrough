
#pragma once

#include "shared_structs.h"

class OpenVRManager
{
public:

	OpenVRManager();
	~OpenVRManager();

	bool InitRuntime();

	inline bool IsRuntimeIntialized()
	{
		return m_bRuntimeInitialized;
	}

	inline vr::IVRSystem* GetVRSystem()
	{
		if (!IsRuntimeIntialized()) { return nullptr; }
		return m_vrSystem;
	}

	inline vr::IVRCompositor* GetVRCompositor()
	{
		if (!IsRuntimeIntialized()) { return nullptr; }
		return m_vrCompositor;
	}

	inline vr::IVRTrackedCamera* GetVRTrackedCamera()
	{
		if (!IsRuntimeIntialized()) { return nullptr; }
		return m_vrTrackedCamera;
	}

	inline vr::IVROverlay* GetVROverlay()
	{
		if (!IsRuntimeIntialized()) { return nullptr; }
		return m_vrOverlay;
	}

	inline vr::IVRRenderModels* GetVRRenderModels()
	{
		if (!IsRuntimeIntialized()) { return nullptr; }
		return m_vrRenderModels;
	}

	int GetHMDDeviceId() const
	{
		return m_hmdDeviceId;
	}

	void GetCameraDebugProperties(std::vector<DeviceDebugProperties>& properties);
	void GetDeviceIdentProperties(std::vector<DeviceIdentProperties>& properties);
	
private:
	bool m_bRuntimeInitialized;
	int m_hmdDeviceId;

	std::mutex m_runtimeMutex;

	vr::IVRSystem* m_vrSystem;
	vr::IVRCompositor* m_vrCompositor;
	vr::IVRTrackedCamera* m_vrTrackedCamera;
	vr::IVROverlay* m_vrOverlay;
	vr::IVRRenderModels* m_vrRenderModels;
};

