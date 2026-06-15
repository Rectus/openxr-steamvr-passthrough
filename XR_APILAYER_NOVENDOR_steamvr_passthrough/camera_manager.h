#pragma once
#include <thread>
#include <mutex>
#include <atomic>
#include <xr_linear.h>
#include "layer_structs.h"
#include "passthrough_renderer.h"
#include "async_renderer.h"
#include "openvr_manager.h"
#include "mesh.h"
#include <opencv2/videoio.hpp>
#include "lodepng.h"
#include "pathutil.h"
#include "perfutil.h"
#include "frame_queue.h"


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
#define FRAME_TIMEOUT_MS 1000

class ICameraManager
{
public:

	//virtual ICameraManager(std::shared_ptr<IPassthroughRenderer> renderer, ERenderAPI renderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager);
	virtual ~ICameraManager() {};

	virtual bool InitCamera() = 0;
	virtual void DeinitCamera() = 0;
	virtual void SetPaused(bool bIsPaused) = 0;

	virtual EPassthroughCameraState GetCameraState() const = 0;
	virtual void GetCameraDisplayStats(uint32_t& width, uint32_t& height, float& fps, ECameraProvider& provider, bool& bIsActive) const = 0;
	virtual void GetDistortedTextureSize(uint32_t& width, uint32_t& height) const = 0;
	virtual void GetDistortedFrameSize(uint32_t& width, uint32_t& height) const = 0;
	virtual void GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const = 0;
	virtual void GetDistortionCoefficients(ECameraDistortionCoefficients& coeffs) const = 0;
	virtual EStereoFrameLayout GetFrameLayout() const = 0;
	virtual bool IsUsingFisheyeModel() const = 0;
	virtual XrMatrix4x4f GetLeftToRightCameraTransform() const = 0;
	virtual void UpdateStaticCameraParameters() = 0;
	virtual float GetGPUFrameRetrievalPerfTime() { return -1.0f; }
	virtual float GetCPUFrameRetrievalPerfTime() { return -1.0f; }
	virtual FramePtr<CameraGPUFrame> AcquireCameraGPUFrame() = 0;
	virtual void ReleaseCameraGPUFrame(std::shared_ptr<CameraGPUFrame> frame) = 0;
	virtual FramePtr<CameraCPUFrame> AcquireCameraCPUFrame() = 0;

	const void DumpCameraFrameTexture(const std::shared_ptr<std::vector<uint8_t>> frameBuffer, const uint32_t width, const uint32_t height, const std::string cameraProvider)
	{
		if (!frameBuffer.get() || frameBuffer->size() == 0)
		{
			g_logger->warn("No framebuffer to write!");
			return;
		}

		const auto time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
	
		const std::string fileName = GetLocalAppData() + std::format("\\{} Camera Frame {:%Y-%m-%d %H-%M-%S}.png", cameraProvider, time);

		uint32_t error = lodepng::encode(fileName, frameBuffer->data(), width, height);

		if (error)
		{
			g_logger->error("Failed to write texture to file {} {}", fileName.data(), lodepng_error_text(error));
		}
		else
		{
			g_logger->info("Dumped camera frame to file: {}", fileName.data());
		}
	}
};

class CameraManagerOpenVR : public ICameraManager
{
public:

	CameraManagerOpenVR(std::shared_ptr<IPassthroughRenderer> inlineRenderer, std::shared_ptr<AsyncRenderer> asyncRenderer, ERenderAPI renderAPI, ERenderAPI appRenderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, EProjectionMode projectionMode);
	~CameraManagerOpenVR();

	bool InitCamera();
	void DeinitCamera();
	void SetPaused(bool bIsPaused)
	{
		m_bIsPaused = bIsPaused;
	}

	EPassthroughCameraState GetCameraState() const;
	void GetCameraDisplayStats(uint32_t& width, uint32_t& height, float& fps, ECameraProvider& provider, bool& bIsActive) const;
	void GetDistortedTextureSize(uint32_t& width, uint32_t& height) const;
	void GetDistortedFrameSize(uint32_t& width, uint32_t& height) const;
	void GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const;
	void GetDistortionCoefficients(ECameraDistortionCoefficients& coeffs) const;
	EStereoFrameLayout GetFrameLayout() const;
	bool IsUsingFisheyeModel() const;
	XrMatrix4x4f GetLeftToRightCameraTransform() const;
	void UpdateStaticCameraParameters();
	float GetGPUFrameRetrievalPerfTime() { return m_gpuFrameTimer.GetAverageTimeMS(); }
	float GetCPUFrameRetrievalPerfTime() { return m_cpuFrameTimer.GetAverageTimeMS(); }
	FramePtr<CameraGPUFrame> AcquireCameraGPUFrame();
	void ReleaseCameraGPUFrame(std::shared_ptr<CameraGPUFrame> frame);
	FramePtr<CameraCPUFrame> AcquireCameraCPUFrame();

private:
	void ServeFrames();
	void ServeBlockQueueFrames();
	void CopyCPUFrameToGPU(std::shared_ptr<CameraCPUFrame> frame);
	bool GetHMDPoseForTime(XrMatrix4x4f& headToTrackingPose, const uint64_t time);
	void GetTrackedCameraEyePoses(XrMatrix4x4f& LeftPose, XrMatrix4x4f& RightPose, bool bForceOpenVRValue);
	void UpdateRoomViewProjectionMatrix();

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;

	bool m_bCameraInitialized = false;

	uint32_t m_cameraTextureWidth = 0;
	uint32_t m_cameraTextureHeight = 0;
	uint32_t m_cameraFrameBufferSize = 0;

	uint32_t m_cameraUndistortedTextureWidth = 0;
	uint32_t m_cameraUndistortedTextureHeight = 0;
	uint32_t m_cameraUndistortedFrameBufferSize = 0;

	uint32_t m_cameraFrameWidth = 0;
	uint32_t m_cameraFrameHeight = 0;

	uint32_t m_cameraUndistortedFrameWidth = 0;
	uint32_t m_cameraUndistortedFrameHeight = 0;

	float m_projectionDistanceFar;
	bool m_useAlternateProjectionCalc;

	std::weak_ptr<IPassthroughRenderer> m_inlineRenderer;
	std::weak_ptr<AsyncRenderer> m_asyncRenderer;
	ERenderAPI m_renderAPI;
	ERenderAPI m_appRenderAPI;
	EProjectionMode m_projectionMode = Projection_RoomView2D;
	std::thread m_serveThread;
	std::thread m_serveThreadBlockQueue;
	std::atomic_bool m_bRunThread = true;
	std::mutex m_serveMutex;
	std::mutex m_serveMutexCPU;
	std::unique_lock<std::shared_mutex> m_extrenalFrameWriteLock;
	bool m_bIsPaused = false;
	std::atomic_bool m_bWaitingForCamera = false;
	bool m_bCameraFailed = false;
	bool m_bPoseAvailable = false;
	std::atomic_bool m_bUseBlockQueue = false;

	FrameQueue<CameraGPUFrame> m_gpuFrameQueue;
	FrameQueue<CameraCPUFrame> m_cpuFrameQueue;

	int m_hmdDeviceId = -1;
	vr::TrackedCameraHandle_t m_cameraHandle = INVALID_TRACKED_CAMERA_HANDLE;
	EStereoFrameLayout m_frameLayout = FrameLayout_Mono;

	XrMatrix4x4f m_cameraRoomViewProjectionInvLeft{};
	XrMatrix4x4f m_cameraRoomViewProjectionInvRight{};

	XrMatrix4x4f m_cameraToHMDLeft{};
	XrMatrix4x4f m_cameraToHMDRight{};
	XrMatrix4x4f m_HMDToCameraLeft{};
	XrMatrix4x4f m_HMDToCameraRight{};

	XrMatrix4x4f m_cameraLeftToRightPose{};

	PerfTimer m_gpuFrameTimer{ 20 };
	PerfTimer m_cpuFrameTimer{ 20 };
};


class CameraManagerOpenCV : public ICameraManager
{
public:

	CameraManagerOpenCV(std::shared_ptr<IPassthroughRenderer> inlineRenderer, std::shared_ptr<AsyncRenderer> asyncRenderer, ERenderAPI renderAPI, ERenderAPI appRenderAPI, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, EProjectionMode projectionMode, bool bIsAugmented = false);
	~CameraManagerOpenCV();

	bool InitCamera();
	void DeinitCamera();
	void SetPaused(bool bIsPaused)
	{
		m_bIsPaused = bIsPaused;
	}

	EPassthroughCameraState GetCameraState() const;
	void GetCameraDisplayStats(uint32_t& width, uint32_t& height, float& fps, ECameraProvider& provider, bool& bIsActive) const;
	void GetDistortedTextureSize(uint32_t& width, uint32_t& height) const;
	void GetDistortedFrameSize(uint32_t& width, uint32_t& height) const;
	void GetIntrinsics(const ERenderEye cameraEye, XrVector2f& focalLength, XrVector2f& center) const;
	void GetDistortionCoefficients(ECameraDistortionCoefficients& coeffs) const;
	EStereoFrameLayout GetFrameLayout() const;
	bool IsUsingFisheyeModel() const;
	XrMatrix4x4f GetLeftToRightCameraTransform() const;
	void UpdateStaticCameraParameters();
	float GetGPUFrameRetrievalPerfTime() { return m_gpuFrameTimer.GetAverageTimeMS(); }
	float GetCPUFrameRetrievalPerfTime() { return m_cpuFrameTimer.GetAverageTimeMS(); }
	FramePtr<CameraGPUFrame> AcquireCameraGPUFrame();
	void ReleaseCameraGPUFrame(std::shared_ptr<CameraGPUFrame> frame);
	FramePtr<CameraCPUFrame> AcquireCameraCPUFrame();

private:
	void ServeFrames();
	void CopyCPUFrameToGPU(std::shared_ptr<CameraCPUFrame> frame);

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;

	bool m_bIsAugmented = false;
	bool m_bCameraInitialized = false;
	bool m_bIsPaused = false;

	cv::VideoCapture m_videoCapture;

	uint32_t m_cameraTextureWidth = 0;
	uint32_t m_cameraTextureHeight = 0;
	uint32_t m_cameraFrameBufferSize = 0;

	uint32_t m_cameraFrameWidth = 0;
	uint32_t m_cameraFrameHeight = 0;

	float m_projectionDistanceFar;
	bool m_useAlternateProjectionCalc;

	std::weak_ptr<IPassthroughRenderer> m_inlineRenderer;
	std::weak_ptr<AsyncRenderer> m_asyncRenderer;
	ERenderAPI m_renderAPI;
	ERenderAPI m_appRenderAPI;
	EProjectionMode m_projectionMode;
	std::thread m_serveThread;
	std::atomic_bool m_bRunThread = true;
	std::mutex m_serveMutex;
	std::mutex m_serveMutexCPU;

	FrameQueue<CameraGPUFrame> m_gpuFrameQueue;
	FrameQueue<CameraCPUFrame> m_cpuFrameQueue;

	int m_hmdDeviceId = -1;
	EStereoFrameLayout m_frameLayout;

	PerfTimer m_gpuFrameTimer{ 20 };
	PerfTimer m_cpuFrameTimer{ 20 };
};