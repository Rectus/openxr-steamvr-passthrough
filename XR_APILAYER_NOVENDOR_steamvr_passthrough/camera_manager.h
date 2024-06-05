#pragma once
#include <thread>
#include <mutex>
#include <atomic>
#include <xr_linear.h>
#include "layer.h"
#include "passthrough_renderer.h"
#include "openvr_manager.h"
#include "mesh.h"


enum ETrackedCameraFrameType
{
	VRFrameType_Distorted = 0,
	VRFrameType_Undistorted,
	VRFrameType_MaximumUndistorted
};

struct ECameraDistortionCoefficients
{
	double v[16];
};

#define POSTFRAME_SLEEP_INTERVAL (std::chrono::milliseconds(10))
#define FRAME_POLL_INTERVAL (std::chrono::microseconds(100))


class CameraManager
{
public:

	CameraManager(std::shared_ptr<IPassthroughRenderer> renderer, ERenderAPI renderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager);
	~CameraManager();

	bool InitCamera();
	void DeinitCamera();

	void GetDistortedFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const;
	void GetUndistortedFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize) const;
	void GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const;
	void GetDistortionCoefficients(ECameraDistortionCoefficients& coeffs) const;
	EStereoFrameLayout GetFrameLayout() const;
	XrMatrix4x4f GetLeftToRightCameraTransform() const;
	void UpdateStaticCameraParameters();
	float GetFrameRetrievalPerfTime() { return m_averageFrameRetrievalTime; }
	bool GetCameraFrame(std::shared_ptr<CameraFrame>& frame);
	void CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, float timeToPhotons, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams);

private:
	void ServeFrames();
	void UpdateRenderModels();
	void GetTrackedCameraEyePoses(XrMatrix4x4f& LeftPose, XrMatrix4x4f& RightPose);
	XrMatrix4x4f GetHMDWorldToViewMatrix(const ERenderEye eye, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo);
	void UpdateProjectionMatrix(std::shared_ptr<CameraFrame>& frame);
	void CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, const XrCompositionLayerProjection& layer, const XrReferenceSpaceCreateInfo& refSpaceInfo, UVDistortionParameters& distortionParams);

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;

	bool m_bCameraInitialized = false;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;

	uint32_t m_cameraUndistortedTextureWidth;
	uint32_t m_cameraUndistortedTextureHeight;
	uint32_t m_cameraUndistortedFrameBufferSize;

	uint32_t m_cameraFrameWidth;
	uint32_t m_cameraFrameHeight;

	uint32_t m_cameraUndistortedFrameWidth;
	uint32_t m_cameraUndistortedFrameHeight;

	float m_projectionDistanceFar;
	bool m_useAlternateProjectionCalc;

	std::weak_ptr<IPassthroughRenderer> m_renderer;
	ERenderAPI m_renderAPI;
	std::thread m_serveThread;
	std::atomic_bool m_bRunThread = true;
	std::mutex m_serveMutex;

	std::shared_ptr<CameraFrame> m_renderFrame;
	std::shared_ptr<CameraFrame> m_servedFrame;
	std::shared_ptr<CameraFrame> m_underConstructionFrame;

	int m_hmdDeviceId = -1;
	vr::TrackedCameraHandle_t m_cameraHandle;
	EStereoFrameLayout m_frameLayout;

	XrMatrix4x4f m_HMDViewToProjectionLeft{};
	XrMatrix4x4f m_HMDViewToProjectionRight{};
	XrMatrix4x4f m_viewToHMDLeft{};
	XrMatrix4x4f m_viewToHMDRight{};
	XrMatrix4x4f m_HMDToViewLeft{};
	XrMatrix4x4f m_HMDToViewRight{};

	XrMatrix4x4f m_cameraProjectionInvFarLeft{};
	XrMatrix4x4f m_cameraProjectionInvFarRight{};

	XrMatrix4x4f m_cameraToHMDLeft{};
	XrMatrix4x4f m_cameraToHMDRight{};
	XrMatrix4x4f m_HMDToCameraLeft{};
	XrMatrix4x4f m_HMDToCameraRight{};

	XrMatrix4x4f m_cameraLeftToRightPose{};
	XrMatrix4x4f m_cameraRightToLeftPose{};

	XrMatrix4x4f m_lastCameraProjectionToWorldLeft;
	XrMatrix4x4f m_lastCameraProjectionToWorldRight;
	XrMatrix4x4f m_lastWorldToCameraProjectionLeft;
	XrMatrix4x4f m_lastWorldToCameraProjectionRight;
	XrMatrix4x4f m_lastWorldToHMDProjectionLeft;
	XrMatrix4x4f m_lastWorldToHMDProjectionRight;
	uint32_t m_lastFrameSequence;

	std::deque<float> m_frameRetrievalTimes;
	float m_averageFrameRetrievalTime;

	std::shared_ptr<std::vector<RenderModel>> m_renderModels;
};

