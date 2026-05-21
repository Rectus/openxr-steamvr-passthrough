
#pragma once

#include <mutex>
#include <shared_mutex>
#include "layer_structs.h"
#include "vr_blockqueue.h"

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

	inline vr::IVRPaths* GetVRPaths()
	{
		if (!CheckRuntimeIntialized()) { return nullptr; }
		return m_vrPaths;
	}

	inline vr::IVRBlockQueue* GetVRBlockQueue()
	{
		if (!CheckRuntimeIntialized()) { return nullptr; }
		return m_vrBlockQueue;
	}

	int GetHMDDeviceId() const
	{
		return m_hmdDeviceId;
	}
	

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
	vr::IVRPaths* m_vrPaths = nullptr;
	vr::IVRBlockQueue* m_vrBlockQueue = nullptr;
};

