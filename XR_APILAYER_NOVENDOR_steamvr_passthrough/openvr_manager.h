
#pragma once

#include <mutex>
#include <shared_mutex>


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

	int GetHMDDeviceId() const
	{
		return m_hmdDeviceId;
	}

private:
	bool InitRuntime();

	inline bool CheckRuntimeIntialized()
	{
		{
			std::lock_guard<std::mutex> lock(m_runtimeMutex);
			if (!m_bRuntimeInitialized)
			{
				return InitRuntime();
			}
		}
		return true;
	}

	bool m_bRuntimeInitialized;
	int m_hmdDeviceId;

	std::mutex m_runtimeMutex;

	vr::IVRSystem* m_vrSystem;
	vr::IVRCompositor* m_vrCompositor;
	vr::IVRTrackedCamera* m_vrTrackedCamera;
	vr::IVROverlay* m_vrOverlay;
};

