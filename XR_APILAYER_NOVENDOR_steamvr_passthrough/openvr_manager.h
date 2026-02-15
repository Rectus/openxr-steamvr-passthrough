
#pragma once

#include <mutex>
#include <shared_mutex>
#include "layer.h"

namespace vr {
	class IVRClientCore;
};

class OpenVRManager
{
public:

	OpenVRManager();
	~OpenVRManager();

	inline vr::IVRSystem* GetVRSystem()
	{
		if (!CheckRuntimeIntialized()) { return nullptr; }
		return m_vrSystem;
	}

	inline vr::IVRCompositor* GetVRCompositor()
	{
		if (!CheckRuntimeIntialized()) { return nullptr; }
		return m_vrCompositor;
	}

	inline vr::IVRTrackedCamera* GetVRTrackedCamera()
	{
		if (!CheckRuntimeIntialized()) { return nullptr; }
		return m_vrTrackedCamera;
	}

	inline vr::IVROverlay* GetVROverlay()
	{
		if (!CheckRuntimeIntialized()) { return nullptr; }
		return m_vrOverlay;
	}

	inline vr::IVRRenderModels* GetVRRenderModels()
	{
		if (!CheckRuntimeIntialized()) { return nullptr; }
		return m_vrRenderModels;
	}

	int GetHMDDeviceId() const
	{
		return m_hmdDeviceId;
	}

	void GetCameraDebugProperties(std::vector<DeviceDebugProperties>& properties);
	void GetDeviceIdentProperties(std::vector<DeviceIdentProperties>& properties);
	

private:
	bool InitRuntime();

	inline bool CheckRuntimeIntialized()
	{
		if (!m_bRuntimeInitialized)
		{
			return InitRuntime();
		}

		return true;
	}

	bool m_bRuntimeInitialized;
	int m_hmdDeviceId;

	std::mutex m_runtimeMutex;

	vr::IVRClientCore* m_vrClientCore = nullptr;
	vr::IVRSystem* m_vrSystem = nullptr;
	vr::IVRCompositor* m_vrCompositor= nullptr;
	vr::IVRTrackedCamera* m_vrTrackedCamera = nullptr;
	vr::IVROverlay* m_vrOverlay = nullptr;
	vr::IVRRenderModels* m_vrRenderModels = nullptr;
};

