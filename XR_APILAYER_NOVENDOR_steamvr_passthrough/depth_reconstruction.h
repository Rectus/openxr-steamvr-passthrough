#pragma once

#include "layer.h"
#include "openvr_manager.h"
#include "config_manager.h"
#include "camera_manager.h"

#include <opencv2/imgproc/types_c.h>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc.hpp>

class DepthReconstruction
{
public:
	DepthReconstruction(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, std::shared_ptr<CameraManager> cameraManager);
	~DepthReconstruction();

	std::shared_ptr<DepthFrame> GetDepthFrame();
	UVDistortionParameters& GetDistortionParameters()
	{
		return m_distortionParams;
	}
	float GetReconstructionPerfTime() { return m_averageReconstructionTime; }

private:
	void InitReconstruction();
	void RunThread();
	void CreateDistortionMap();

	std::thread m_thread;
	std::atomic_bool m_bRunThread;
	std::mutex m_serveMutex;

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;
	std::shared_ptr<CameraManager> m_cameraManager;

	std::shared_ptr<DepthFrame> m_servedDepthFrame;
	std::shared_ptr<DepthFrame> m_depthFrame;
	std::shared_ptr<DepthFrame> m_underConstructionDepthFrame;

	UVDistortionParameters m_distortionParams;

	XrVector2f m_cameraCenter[2];
	XrVector2f m_cameraFocalLength[2];
	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameWidth;
	uint32_t m_cameraFrameHeight;
	uint32_t m_cvImageWidth;
	uint32_t m_cvImageHeight;
	uint32_t m_cameraFrameBufferSize;
	XrMatrix4x4f m_cameraLeftToRightTransform;
	EStereoFrameLayout m_frameLayout;

	uint32_t m_lastFrameSequence;
	uint32_t m_downscaleFactor;
	float m_fovScale;
	float m_depthOffsetCalibration;
	int m_maxDisparity;
	bool m_bUseMulticore;

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
	
	cv::Ptr<cv::StereoSGBM> m_stereoSGBM;
	cv::Ptr<cv::StereoBM> m_stereoBM;
	cv::Ptr<cv::StereoMatcher> m_rightMatcher;

	cv::Ptr<cv::ximgproc::DisparityWLSFilter> m_wlsFilter;

	cv::Mat m_inputFrame;
	cv::Mat m_inputFrameLeft;
	cv::Mat m_inputFrameRight;
	cv::Mat m_inputFrameGreyLeft;
	cv::Mat m_inputFrameGreyRight;

	cv::Mat m_rectifiedFrameLeft;
	cv::Mat m_rectifiedFrameRight;

	cv::Mat m_scaledFrameLeft;
	cv::Mat m_scaledFrameRight;

	cv::Mat m_scaledExtFrameLeft;
	cv::Mat m_scaledExtFrameRight;

	cv::Mat m_rawDisparity;
	cv::Mat m_rightDisparity;
	cv::Mat m_filteredDisparity;
	cv::Mat m_disparityMatrix;

	cv::Mat m_confidence;
	cv::Mat m_bilateralDisparity;

	std::deque<float> m_reconstructionTimes;
	float m_averageReconstructionTime;

	cv::Mat m_colorRectifyInput;
	cv::Mat m_colorRectifyLeft;
	cv::Mat m_colorRectifyRight;
	cv::Mat m_colorRectifiedLeft;
	cv::Mat m_colorRectifiedRight;
	cv::Mat m_colorRectifiedOutput;
};

