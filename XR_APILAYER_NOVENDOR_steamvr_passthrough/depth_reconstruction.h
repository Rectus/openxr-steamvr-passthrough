#pragma once

#include "layer_structs.h"
#include "openvr_manager.h"
#include "config_manager.h"
#include "camera_manager.h"
#include "async_renderer.h"
#include "perfutil.h"

#include <opencv2/imgproc/types_c.h>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc.hpp>

class DepthReconstruction
{
public:
	DepthReconstruction(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, std::shared_ptr<ICameraManager> cameraManager, std::shared_ptr<AsyncRenderer> asyncRenderer);
	~DepthReconstruction();

	FramePtr<DepthFrame> GetDepthFrame();
	void ReleaseDepthFrame(std::shared_ptr<DepthFrame> frame);

	UVDistortionParameters& GetDistortionParameters()
	{
		return m_distortionParams;
	}
	float GetReconstructionPerfTime() { return m_reconstructionTimer.GetAverageTimeMS(); }
	float GetRenderPerfTime() { return m_renderTimer.GetAverageTimeMS(); }
	void CalculateCameraProjection(std::shared_ptr<CameraGPUFrame>& cameraFrame, FrameRenderParameters& renderParams);
private:
	void InitReconstruction();
	void RunThread();
	void CreateDistortionMap();

	std::thread m_thread;
	std::atomic_bool m_bRunThread;
	std::mutex m_serveMutex;

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;
	std::shared_ptr<ICameraManager> m_cameraManager;
	std::shared_ptr<AsyncRenderer> m_asyncRenderer;

	FrameQueue<DepthFrame> m_depthFrameQueue;
	int m_depthFrameIndex = 0;
	UVDistortionParameters m_distortionParams;

	XrVector2f m_cameraCenter[2];
	XrVector2f m_cameraFocalLength[2];
	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameWidth;
	uint32_t m_cameraFrameHeight;
	uint32_t m_cvImageWidth;
	uint32_t m_cvImageHeight;
	EStereoFrameLayout m_frameLayout;

	uint32_t m_lastFrameSequence;
	uint32_t m_downscaleFactor;
	float m_fovScale;
	int m_maxDisparity;
	bool m_bUseMulticore;
	bool m_bUseColor;
	bool m_bDisparityBothEyes;

	cv::Mat m_intrinsicsLeft;
	cv::Mat m_intrinsicsRight;
	cv::Mat m_distortionParamsLeft;
	cv::Mat m_distortionParamsRight;

	cv::Mat m_leftMap1;
	cv::Mat m_leftMap2;
	cv::Mat m_rightMap1;
	cv::Mat m_rightMap2;

	XrMatrix4x4f m_disparityToDepth;

	XrMatrix4x4f m_rectifiedRotationLeft;
	XrMatrix4x4f m_rectifiedRotationRight;

	XrMatrix4x4f m_fishEyeProjectionLeft;
	XrMatrix4x4f m_fishEyeProjectionRight;
	
	cv::Ptr<cv::StereoMatcher> m_stereoLeftMatcher;
	cv::Ptr<cv::StereoMatcher> m_stereoRightMatcher;

	cv::Ptr<cv::ximgproc::DisparityWLSFilter> m_wlsFilterLeft;
	cv::Ptr<cv::ximgproc::DisparityWLSFilter> m_wlsFilterRight;

	cv::Mat m_rawInputFrame;
	cv::Mat m_inputFrame;
	cv::Mat m_inputFrameColor;
	std::vector<uint8_t> m_inputFrameColorBuffer;
	cv::Mat m_inputFrameLeft;
	cv::Mat m_inputFrameRight;
	cv::Mat m_inputAlphaLeft;
	cv::Mat m_inputAlphaRight;

	cv::Mat m_rectifiedFrameLeft;
	cv::Mat m_rectifiedFrameRight;
	cv::Mat m_scaledFrameLeft;
	cv::Mat m_scaledFrameRight;

	cv::Mat m_scaledExtFrameLeft;
	cv::Mat m_scaledExtFrameRight;

	cv::Mat m_rawDisparityLeft;
	cv::Mat m_rawDisparityRight;
	cv::Mat m_filteredDisparityLeft;
	cv::Mat m_filteredDisparityRight;

	cv::Mat m_confidenceLeft;
	cv::Mat m_confidenceRight;
	cv::Mat m_bilateralDisparityLeft;
	cv::Mat m_bilateralDisparityRight;

	cv::Mat m_outputDisparity;
	cv::Mat m_outputDisparityLeft;
	cv::Mat m_outputDisparityRight;

	cv::Mat m_outputConfidence;
	cv::Mat m_outputConfidenceLeft;
	cv::Mat m_outputConfidenceRight;

	cv::Mat m_outputCameraFrame;
	cv::Mat m_outputCameraFrameLeft;
	cv::Mat m_outputCameraFrameRight;

	std::vector<uint8_t> m_outputDisparityBuffer;
	std::vector<uint8_t> m_outputConfidenceBuffer;
	std::vector<uint8_t> m_outputCameraFrameBuffer;

	PerfTimer m_reconstructionTimer{ 20 };
	PerfTimer m_renderTimer{ 20 };

	cv::Mat m_colorRectifyInput;
	cv::Mat m_colorRectifyLeft;
	cv::Mat m_colorRectifyRight;
	cv::Mat m_colorRectifiedLeft;
	cv::Mat m_colorRectifiedRight;
	cv::Mat m_colorRectifiedOutput;

	cv::Mat m_maskMat;
};

