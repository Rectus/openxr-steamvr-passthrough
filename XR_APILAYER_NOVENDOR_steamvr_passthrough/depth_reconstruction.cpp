
#include "pch.h"
#include "depth_reconstruction.h"

#include "mathutil.h"
#include "perfutil.h"

#include <opencv2/imgcodecs.hpp>


DepthReconstruction::DepthReconstruction(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, std::shared_ptr<ICameraManager> cameraManager, std::shared_ptr<AsyncRenderer> asyncRenderer)
    : m_depthFrameQueue(5)
    , m_asyncRenderer(asyncRenderer)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_cameraManager(cameraManager)
    , m_distortionParams()
{
    Config_Stereo& stereoConfig = m_configManager->GetConfig_Stereo();

    m_maxDisparity = stereoConfig.StereoMaxDisparity;
    m_downscaleFactor = stereoConfig.StereoDownscaleFactor;

    m_fovScale = m_configManager->GetConfig_Main().FieldOfViewScale;
    m_bUseColor = stereoConfig.StereoUseColor;
    m_bDisparityBothEyes = stereoConfig.StereoDisparityBothEyes;

    m_bUseMulticore = stereoConfig.StereoUseMulticore;
    cv::setNumThreads(m_bUseMulticore ? -1 : 0);

    InitReconstruction();

    m_bRunThread = true;
    m_thread = std::thread(&DepthReconstruction::RunThread, this);
}

DepthReconstruction::~DepthReconstruction()
{
    if (m_thread.joinable())
    {
        m_bRunThread = false;
        m_thread.join();
    }
}

FramePtr<DepthFrame> DepthReconstruction::GetDepthFrame()
{
    return m_depthFrameQueue.AcquireRead();
}

void DepthReconstruction::CalculateCameraProjection(std::shared_ptr<CameraGPUFrame>& cameraFrame, FrameRenderParameters& renderParams)
{
    std::shared_lock readLock(m_distortionParams.readWriteMutex);

    XrMatrix4x4f worldToCameraView, worldToRectView;

    XrMatrix4x4f_Invert(&worldToCameraView, &cameraFrame->CameraViewToWorldLeft);
    XrMatrix4x4f_Multiply(&worldToRectView, &m_distortionParams.rectifiedRotationLeft, &worldToCameraView);
    XrMatrix4x4f_Multiply(&cameraFrame->WorldToCameraProjectionLeft, &m_distortionParams.cameraProjectionLeft, &worldToRectView);

    if (m_frameLayout == EStereoFrameLayout::FrameLayout_Mono)
    {
        cameraFrame->WorldToCameraProjectionRight = cameraFrame->WorldToCameraProjectionLeft;
    }
    else
    {
        XrMatrix4x4f_Invert(&worldToCameraView, &cameraFrame->CameraViewToWorldRight);
        XrMatrix4x4f_Multiply(&worldToRectView, &m_distortionParams.rectifiedRotationRight, &worldToCameraView);
        XrMatrix4x4f_Multiply(&cameraFrame->WorldToCameraProjectionRight, &m_distortionParams.cameraProjectionRight, &worldToRectView);
    }
}

void DepthReconstruction::InitReconstruction()
{
    m_frameLayout = m_cameraManager->GetFrameLayout();
    uint32_t frameBufferSize;
    m_cameraManager->GetDistortedTextureSize(m_cameraTextureWidth, m_cameraTextureHeight, frameBufferSize);
    m_cameraManager->GetDistortedFrameSize(m_cameraFrameWidth, m_cameraFrameHeight);

    m_cvImageHeight = m_cameraFrameHeight / m_downscaleFactor;
    m_cvImageWidth = m_cameraFrameWidth / m_downscaleFactor;

    m_cameraManager->GetIntrinsics(RenderEye_Left, m_cameraFocalLength[0], m_cameraCenter[0]);
    m_cameraManager->GetIntrinsics(RenderEye_Right, m_cameraFocalLength[1], m_cameraCenter[1]);

    ECameraDistortionCoefficients distCoeffs = { 0 };
    m_cameraManager->GetDistortionCoefficients(distCoeffs);

    m_intrinsicsLeft = cv::Mat::zeros(cv::Size(3, 3), CV_64F);
    m_intrinsicsRight = cv::Mat::zeros(cv::Size(3, 3), CV_64F);

    m_intrinsicsLeft.at<double>(0, 0) = m_cameraFocalLength[0].x;
    m_intrinsicsLeft.at<double>(0, 2) = m_cameraCenter[0].x;
    m_intrinsicsLeft.at<double>(1, 1) = m_cameraFocalLength[0].y;
    m_intrinsicsLeft.at<double>(1, 2) = m_cameraCenter[0].y;
    m_intrinsicsLeft.at<double>(2, 2) = 1.0;

    m_intrinsicsRight.at<double>(0, 0) = m_cameraFocalLength[1].x;
    m_intrinsicsRight.at<double>(0, 2) = m_cameraCenter[1].x;
    m_intrinsicsRight.at<double>(1, 1) = m_cameraFocalLength[1].y;
    m_intrinsicsRight.at<double>(1, 2) = m_cameraCenter[1].y;
    m_intrinsicsRight.at<double>(2, 2) = 1.0;

    m_distortionParamsLeft = cv::Mat(4, 1, CV_64F);
    m_distortionParamsRight = cv::Mat(4, 1, CV_64F);

    m_distortionParamsLeft.at<double>(0, 0) = distCoeffs.v[0];
    m_distortionParamsLeft.at<double>(1, 0) = distCoeffs.v[1];
    m_distortionParamsLeft.at<double>(2, 0) = distCoeffs.v[2];
    m_distortionParamsLeft.at<double>(3, 0) = distCoeffs.v[3];

    m_distortionParamsRight.at<double>(0, 0) = distCoeffs.v[8];
    m_distortionParamsRight.at<double>(1, 0) = distCoeffs.v[9];
    m_distortionParamsRight.at<double>(2, 0) = distCoeffs.v[10];
    m_distortionParamsRight.at<double>(3, 0) = distCoeffs.v[11];


    XrMatrix4x4f leftToRightMatrix = ChangeBasisToFromOpenCV(m_cameraManager->GetLeftToRightCameraTransform());
    
    double leftToRightTranslation[3] = { leftToRightMatrix.m[3], leftToRightMatrix.m[7], leftToRightMatrix.m[11] };

    double leftToRightRotation[9] = {
        leftToRightMatrix.m[0], leftToRightMatrix.m[1], leftToRightMatrix.m[2],
        leftToRightMatrix.m[4], leftToRightMatrix.m[5], leftToRightMatrix.m[6],
        leftToRightMatrix.m[8], leftToRightMatrix.m[9], leftToRightMatrix.m[10] };

    cv::Mat R(cv::Size(3, 3), CV_64F, leftToRightRotation);
    cv::Mat T(3, 1, CV_64F, leftToRightTranslation);
    cv::Mat R1, R2, R3, P1, P2, Q;

    cv::Size textureSize(m_cameraFrameWidth, m_cameraFrameHeight);

    if (m_cameraManager->IsUsingFisheyeModel())
    {
        if (m_frameLayout != FrameLayout_Mono)
        {
            cv::fisheye::stereoRectify(m_intrinsicsLeft, m_distortionParamsLeft, m_intrinsicsRight, m_distortionParamsRight, textureSize, R, T, R1, R2, P1, P2, Q, cv::CALIB_ZERO_DISPARITY, textureSize, 0.0, m_fovScale);
        }
        else
        {
            P1 = m_intrinsicsLeft.clone();
            P2 = m_intrinsicsRight.clone();
            Q = cv::Mat::eye(4, 4, CV_64F);
        }

        cv::fisheye::initUndistortRectifyMap(m_intrinsicsLeft, m_distortionParamsLeft, R1, P1, textureSize, CV_32FC1, m_leftMap1, m_leftMap2);
        cv::fisheye::initUndistortRectifyMap(m_intrinsicsRight, m_distortionParamsRight, R2, P2, textureSize, CV_32FC1, m_rightMap1, m_rightMap2);
    }
    else 
    {
        if (m_frameLayout != FrameLayout_Mono)
        {
            cv::stereoRectify(m_intrinsicsLeft, m_distortionParamsLeft, m_intrinsicsRight, m_distortionParamsRight, textureSize, R, T, R1, R2, P1, P2, Q, cv::CALIB_ZERO_DISPARITY, 1.0, textureSize);
        }
        else
        {
            P1 = m_intrinsicsLeft.clone();
            P2 = m_intrinsicsRight.clone();
            Q = cv::Mat::eye(4, 4, CV_64F);
        }

        P1.at<double>(0, 0) /= m_fovScale;
        P1.at<double>(1, 1) /= m_fovScale;

        P2.at<double>(0, 0) /= m_fovScale;
        P2.at<double>(1, 1) /= m_fovScale;

        cv::initUndistortRectifyMap(m_intrinsicsLeft, m_distortionParamsLeft, R1, P1, textureSize, CV_32FC1, m_leftMap1, m_leftMap2);
        cv::initUndistortRectifyMap(m_intrinsicsRight, m_distortionParamsRight, R2, P2, textureSize, CV_32FC1, m_rightMap1, m_rightMap2);
    }

    m_fishEyeProjectionLeft = CVMatToXrMatrix(P1);
    m_fishEyeProjectionRight = CVMatToXrMatrix(P2);

    m_rectifiedRotationLeft = CVMatToXrMatrix(R1);
    m_rectifiedRotationRight = CVMatToXrMatrix(R2);

    XrMatrix4x4f XR_Q = CVMatToXrMatrix(Q);
    XrMatrix4x4f_Transpose(&m_disparityToDepth, &XR_Q);
    
    CreateDistortionMap();

    int frameFormat = m_bUseColor ? CV_8UC3 : CV_8U;

    int disparityWidth = m_bDisparityBothEyes ? m_cvImageWidth + m_maxDisparity * 2 : m_cvImageWidth + m_maxDisparity;

    m_inputFrameLeft = cv::Mat(m_cameraFrameHeight, m_cameraFrameWidth, frameFormat);
    m_inputFrameRight = cv::Mat(m_cameraFrameHeight, m_cameraFrameWidth, frameFormat);
    m_rectifiedFrameLeft = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_rectifiedFrameRight = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_scaledFrameLeft = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_scaledFrameRight = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_scaledExtFrameLeft = cv::Mat(m_cvImageHeight, disparityWidth, frameFormat);
    m_scaledExtFrameRight = cv::Mat(m_cvImageHeight, disparityWidth, frameFormat);
    

    m_rawDisparityLeft = cv::Mat(m_cvImageHeight, disparityWidth, CV_16S);
    m_rawDisparityRight = cv::Mat(m_cvImageHeight, disparityWidth, CV_16S);
    m_filteredDisparityLeft = cv::Mat(m_cvImageHeight, disparityWidth, CV_16S);
    m_bilateralDisparityLeft = cv::Mat(m_cvImageHeight, disparityWidth, CV_16S);
    m_filteredDisparityRight = cv::Mat(m_cvImageHeight, disparityWidth, CV_16S);
    m_bilateralDisparityRight = cv::Mat(m_cvImageHeight, disparityWidth, CV_16S);
}


void DepthReconstruction::CreateDistortionMap()
{
    std::unique_lock writeLock(m_distortionParams.readWriteMutex);

    if (!m_distortionParams.uvDistortionMap.get())
    {
        m_distortionParams.uvDistortionMap = std::make_shared<std::vector<float>>();
    }

    XrMatrix4x4f frameProjectionLeft, frameProjectionRight;
    XrMatrix4x4f_Transpose(&frameProjectionLeft, &m_fishEyeProjectionLeft);
    XrMatrix4x4f_Transpose(&frameProjectionRight, &m_fishEyeProjectionRight);

    // Adjust camera space projection matrix to project to clip space.
    frameProjectionLeft.m[0] = 2.0f * frameProjectionLeft.m[0] / (float)m_cameraFrameWidth;
    frameProjectionLeft.m[5] = -2.0f * frameProjectionLeft.m[5] / (float)m_cameraFrameHeight;
    frameProjectionLeft.m[8] = (1.0f - 2.0f * frameProjectionLeft.m[8] / (float)m_cameraFrameWidth);
    frameProjectionLeft.m[9] = (1.0f - 2.0f * frameProjectionLeft.m[9] / (float)m_cameraFrameHeight);
    frameProjectionLeft.m[10] = 0.0f;
    frameProjectionLeft.m[11] = -1.0f;
    frameProjectionLeft.m[12] = 0.0f;
    frameProjectionLeft.m[13] = 0.0f;
    frameProjectionLeft.m[14] = -NEAR_PROJECTION_DISTANCE;
    frameProjectionLeft.m[15] = 0.0f;

    frameProjectionRight.m[0] = 2.0f * frameProjectionRight.m[0] / (float)m_cameraFrameWidth;
    frameProjectionRight.m[5] = -2.0f * frameProjectionRight.m[5] / (float)m_cameraFrameHeight;
    frameProjectionRight.m[8] = (1.0f - 2.0f * frameProjectionRight.m[8] / (float)m_cameraFrameWidth);
    frameProjectionRight.m[9] = (1.0f - 2.0f * frameProjectionRight.m[9] / (float)m_cameraFrameHeight);
    frameProjectionRight.m[10] = 0.0f;
    frameProjectionRight.m[11] = -1.0f;
    frameProjectionRight.m[12] = 0.0f;
    frameProjectionRight.m[13] = 0.0f;
    frameProjectionRight.m[14] = -NEAR_PROJECTION_DISTANCE;
    frameProjectionRight.m[15] = 0.0f;

    m_distortionParams.cameraProjectionLeft = frameProjectionLeft;
    m_distortionParams.cameraProjectionRight = frameProjectionRight;
    m_distortionParams.rectifiedRotationLeft = ChangeBasisToFromOpenCV(m_rectifiedRotationLeft);
    m_distortionParams.rectifiedRotationRight = ChangeBasisToFromOpenCV(m_rectifiedRotationRight);

    m_distortionParams.fovScale = m_fovScale;

    std::vector<float>& distMap = *m_distortionParams.uvDistortionMap.get();

    distMap.resize(m_cameraTextureHeight * m_cameraTextureWidth * 2);

    if (m_frameLayout == FrameLayout_StereoHorizontal)
    {
        for (uint32_t y = 0; y < m_cameraTextureHeight; y++)
        {
            int rowStart = y * m_cameraTextureWidth * 2;

            for (uint32_t x = 0; x < m_cameraFrameWidth * 2; x += 2)
            {
                distMap[rowStart + x] = (m_leftMap1.at<float>(y, x / 2) - x / 2) / m_cameraFrameWidth / 2;
                distMap[rowStart + x + 1] = (m_leftMap2.at<float>(y, x / 2) - y) / m_cameraFrameHeight;
            }

            rowStart = y * m_cameraTextureWidth * 2 + m_cameraFrameWidth * 2;

            for (uint32_t x = 0; x < m_cameraFrameWidth * 2; x += 2)
            {
                distMap[rowStart + x] = (m_rightMap1.at<float>(y, x / 2) - x / 2) / m_cameraFrameWidth / 2;
                distMap[rowStart + x + 1] = (m_rightMap2.at<float>(y, x / 2) - y) / m_cameraFrameHeight;
            }
        }
    }
    else if (m_frameLayout == FrameLayout_StereoVertical)
    {
        for (uint32_t y = 0; y < m_cameraFrameHeight; y++)
        {
            int rowStart = y * m_cameraTextureWidth * 2;

            for (uint32_t x = 0; x < m_cameraFrameWidth * 2; x += 2)
            {
                distMap[rowStart + x] = (m_rightMap1.at<float>(y, x / 2) - x / 2) / m_cameraFrameWidth;
                distMap[rowStart + x + 1] = (m_rightMap2.at<float>(y, x / 2) - y) / m_cameraFrameHeight / 2;
                
            }

            rowStart = m_cameraFrameHeight * m_cameraTextureWidth * 2 + y * m_cameraTextureWidth * 2;

            for (uint32_t x = 0; x < m_cameraFrameWidth * 2; x += 2)
            {
                distMap[rowStart + x] = (m_leftMap1.at<float>(y, x / 2) - x / 2) / m_cameraFrameWidth;
                distMap[rowStart + x + 1] = (m_leftMap2.at<float>(y, x / 2) - y) / m_cameraFrameHeight / 2;
            }
        }
    }
    else
    {
        for (uint32_t y = 0; y < m_cameraTextureHeight; y++)
        {
            int rowStart = y * m_cameraTextureWidth * 2;

            for (uint32_t x = 0; x < m_cameraFrameWidth * 2; x += 2)
            {
                distMap[rowStart + x] = (m_leftMap1.at<float>(y, x / 2) - x / 2) / m_cameraFrameWidth;
                distMap[rowStart + x + 1] = (m_leftMap2.at<float>(y, x / 2) - y) / m_cameraFrameHeight;
            }
        }
    }
}





void DepthReconstruction::RunThread()
{
    while (m_bRunThread)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));

        m_reconstructionTimer.StartPerfTimer();

        // Make local copies for consistency
        Config_Main mainConfig = m_configManager->GetConfig_Main();
        Config_Stereo stereoConfig = m_configManager->GetConfig_Stereo();
        Config_Camera cameraConfig = m_configManager->GetConfig_Camera();


        if (m_maxDisparity != stereoConfig.StereoMaxDisparity ||
            m_downscaleFactor != stereoConfig.StereoDownscaleFactor ||
            m_fovScale != mainConfig.FieldOfViewScale ||
            m_bUseColor != stereoConfig.StereoUseColor ||
            m_bDisparityBothEyes != stereoConfig.StereoDisparityBothEyes)
        {
            m_maxDisparity = stereoConfig.StereoMaxDisparity;
            m_downscaleFactor = stereoConfig.StereoDownscaleFactor;
            m_fovScale = mainConfig.FieldOfViewScale;
            m_bUseColor = stereoConfig.StereoUseColor;
            m_bDisparityBothEyes = stereoConfig.StereoDisparityBothEyes;

            InitReconstruction();
        }

        if (m_bUseMulticore != stereoConfig.StereoUseMulticore)
        {
            m_bUseMulticore = stereoConfig.StereoUseMulticore;
            cv::setNumThreads(m_bUseMulticore ? -1 : 0);
        }

        FramePtr<CameraCPUFrame> frame = m_cameraManager->AcquireCameraCPUFrame();
        XrMatrix4x4f viewToWorldLeft, viewToWorldRight;
        uint64_t frameTimestamp;

        if (mainConfig.ProjectionMode != Projection_StereoReconstruction || mainConfig.DebugStereoReconstructionFreeze || !frame.HasFrame())
        {
            continue;
        }     

        {
            cv::Rect frameROILeft, frameROIRight;

            if (m_frameLayout == FrameLayout_StereoHorizontal)
            {
                frameROILeft = cv::Rect(0, 0, m_cameraFrameWidth, m_cameraFrameHeight);
                frameROIRight = cv::Rect(m_cameraFrameWidth, 0, m_cameraFrameWidth, m_cameraFrameHeight);
            }
            else if (m_frameLayout == FrameLayout_StereoVertical)
            {
                frameROILeft = cv::Rect(0, m_cameraFrameHeight, m_cameraFrameWidth, m_cameraFrameHeight);
                frameROIRight = cv::Rect(0, 0, m_cameraFrameWidth, m_cameraFrameHeight);
            }

            int frameMinMemSize = m_cameraTextureHeight * m_cameraTextureWidth * 4;

            if (frame->bIsRaw)
            {
                switch (frame->RawFrameFormat)
                {
                case FrameFormat_RGB24:
                    frameMinMemSize = frame->RawFrameSize[0] * frame->RawFrameSize[1] * 3;
                    break;

                case FrameFormat_RAW10:
                case FrameFormat_YUYV16:
                case FrameFormat_BAYER16BG:
                    frameMinMemSize = frame->RawFrameSize[0] * frame->RawFrameSize[1] * 2;
                    break;

                case FrameFormat_NV12:
                case FrameFormat_NV12_2:
                    frameMinMemSize = frame->RawFrameSize[0] * frame->RawFrameSize[1] * 12 / 8;
                    break;

                case FrameFormat_MJPEG:
                    frameMinMemSize = 1;
                    break;

                case FrameFormat_RGBX32:
                default:
                    frameMinMemSize = frame->RawFrameSize[0] * frame->RawFrameSize[1] * 4;
                }
            }

            if (!frame->bIsValid ||
                frame->FrameLayout == FrameLayout_Mono ||
                frame->FrameBuffer->size() < frameMinMemSize ||
                frame->FrameSequence == m_lastFrameSequence ||
                frame->FrameSequence % (stereoConfig.StereoFrameSkip + 1) != 0)
            {
                continue;
            }

            m_lastFrameSequence = static_cast<uint32_t>(frame->FrameSequence);

            viewToWorldLeft = frame->CameraViewToWorldLeft;
            viewToWorldRight = frame->CameraViewToWorldRight;
            frameTimestamp = frame->FrameExposureTimestamp;

            if (frame->bIsRaw)
            {
                switch (frame->RawFrameFormat)
                {
                case FrameFormat_RGBX32:
                {
                    m_inputFrame = cv::Mat(frame->RawFrameSize[1], frame->RawFrameSize[0], CV_8UC4, frame->FrameBuffer->data());

                    if (m_bUseColor)
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_RGBA2RGB);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_RGBA2RGB);
                    }
                    else
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_RGBA2GRAY);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_RGBA2GRAY);
                    }
                    break;
                }
                case FrameFormat_RGB24:
                {
                    m_inputFrame = cv::Mat(frame->RawFrameSize[1], frame->RawFrameSize[0], CV_8UC3, frame->FrameBuffer->data());

                    if (m_bUseColor)
                    {
                        m_inputFrame(frameROILeft).copyTo(m_inputFrameLeft);
                        m_inputFrame(frameROIRight).copyTo(m_inputFrameRight);
                    }
                    else
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_RGB2GRAY);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_RGB2GRAY);
                    }
                    break;
                }
                case FrameFormat_YUYV16:
                {
                    m_inputFrame = cv::Mat(frame->RawFrameSize[1], frame->RawFrameSize[0], CV_8UC2, frame->FrameBuffer->data());

                    if (m_bUseColor)
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_YUV2RGB_YUY2);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_YUV2RGB_YUY2);
                    }
                    else
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_YUV2GRAY_YUY2);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_YUV2GRAY_YUY2);
                    }
                    break;
                }
                case FrameFormat_NV12:
                {
                    m_inputFrame = cv::Mat(frame->RawFrameSize[1], frame->RawFrameSize[0], CV_8UC1, frame->FrameBuffer->data());

                    m_inputFrameColor = cv::Mat(m_cameraTextureHeight, m_cameraTextureWidth, CV_8UC3);

                    cv::cvtColor(m_inputFrame, m_inputFrameColor, cv::COLOR_YUV2RGB_NV12);

                    if (m_bUseColor)
                    {
                        m_inputFrameColor(frameROILeft).copyTo(m_inputFrameLeft);
                        m_inputFrameColor(frameROIRight).copyTo(m_inputFrameRight);
                    }
                    else
                    {
                        cv::cvtColor(m_inputFrameColor(frameROILeft), m_inputFrameLeft, cv::COLOR_RGB2GRAY);
                        cv::cvtColor(m_inputFrameColor(frameROIRight), m_inputFrameRight, cv::COLOR_RGB2GRAY);
                    }
                    break;
                }
                case FrameFormat_BAYER16BG:
                {
                    m_inputFrame = cv::Mat(frame->RawFrameSize[1], frame->RawFrameSize[0], CV_16UC1, frame->FrameBuffer->data());

                    if (m_bUseColor)
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_BayerBG2RGB);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_BayerBG2RGB);
                    }
                    else
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_BayerBG2GRAY);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_BayerBG2GRAY);
                    }
                    break;
                }
                case FrameFormat_MJPEG:
                {
                    m_inputFrame = cv::imdecode(*frame->FrameBuffer.get(), cv::IMREAD_COLOR, &m_rawInputFrame);

                    if (m_inputFrame.empty())
                    {
                        g_logger->warn("Falied to decode MJPEG camera frame!");
                        continue;
                    }

                    if (m_bUseColor)
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_BGR2RGB);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_BGR2RGB);
                    }
                    else
                    {
                        cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_BGR2GRAY);
                        cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_BGR2GRAY);
                    }
                    break;
                }

                case FrameFormat_RAW10: // TODO
                case FrameFormat_NV12_2: // TODO
                default:
                {
                    continue;
                }
                }
            }
            else
            {
                m_inputFrame = cv::Mat(m_cameraTextureHeight, m_cameraTextureWidth, CV_8UC4, frame->FrameBuffer->data());

                if (m_bUseColor)
                {
                    cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_RGBA2RGB);
                    cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_RGBA2RGB);
                }
                else if (stereoConfig.StereoUseBWInputAlpha)
                {
                    // Uses B&W image in alpha channel of distorted frames, unsure if all headsets support this.
                    m_inputAlphaLeft = m_inputFrame(frameROILeft);
                    m_inputAlphaRight = m_inputFrame(frameROIRight);
                    int fromTo[2] = { 3, 0 };
                    cv::mixChannels(&m_inputAlphaLeft, 1, &m_inputFrameLeft, 1, fromTo, 1);
                    cv::mixChannels(&m_inputAlphaRight, 1, &m_inputFrameRight, 1, fromTo, 1);
                }
                else
                {
                    cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_RGBA2GRAY);
                    cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_RGBA2GRAY);
                }
            }
        }

        int filter = stereoConfig.StereoRectificationFiltering ? CV_INTER_LINEAR : CV_INTER_NN;

        cv::remap(m_inputFrameLeft, m_rectifiedFrameLeft, m_leftMap1, m_leftMap2, filter, cv::BORDER_CONSTANT);
        cv::remap(m_inputFrameRight, m_rectifiedFrameRight, m_rightMap1, m_rightMap2, filter, cv::BORDER_CONSTANT);
        
        int resizeFilter = stereoConfig.StereoRectificationFiltering ? CV_INTER_AREA : CV_INTER_LINEAR;

        cv::resize(m_rectifiedFrameLeft, m_scaledFrameLeft, cv::Size(m_cvImageWidth, m_cvImageHeight), resizeFilter);
        cv::resize(m_rectifiedFrameRight, m_scaledFrameRight, cv::Size(m_cvImageWidth, m_cvImageHeight), resizeFilter);      
        int minDisparity = stereoConfig.StereoMinDisparity;
        int numDisparities = m_maxDisparity - stereoConfig.StereoMinDisparity;

        m_scaledFrameLeft.copyTo(m_scaledExtFrameLeft(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)));
        m_scaledFrameRight.copyTo(m_scaledExtFrameRight(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)));

        int filterMultiplier = stereoConfig.StereoBlockSize * stereoConfig.StereoBlockSize;
        int speckleRange = stereoConfig.StereoSGBM_SpeckleWindowSize > 0 ? stereoConfig.StereoSGBM_SpeckleRange : 0;

        m_stereoLeftMatcher = cv::StereoSGBM::create(minDisparity, numDisparities, stereoConfig.StereoBlockSize,
            stereoConfig.StereoSGBM_P1 * filterMultiplier, stereoConfig.StereoSGBM_P2 * filterMultiplier, stereoConfig.StereoSGBM_DispMaxDiff,
            stereoConfig.StereoSGBM_PreFilterCap, stereoConfig.StereoSGBM_UniquenessRatio,
            stereoConfig.StereoSGBM_SpeckleWindowSize, speckleRange,
            (int)stereoConfig.StereoSGBM_Mode);

        m_stereoLeftMatcher->compute(m_scaledExtFrameLeft, m_scaledExtFrameRight, m_rawDisparityLeft);

        cv::Mat* outputMatrixLeft;
        cv::Mat* outputMatrixRight;

        cv::Mat* outputConfMatrixLeft;
        cv::Mat* outputConfMatrixRight;


        if (m_bDisparityBothEyes)
        {
            m_stereoRightMatcher = cv::StereoSGBM::create(-m_maxDisparity, numDisparities, stereoConfig.StereoBlockSize,
                stereoConfig.StereoSGBM_P1 * filterMultiplier, stereoConfig.StereoSGBM_P2 * filterMultiplier, stereoConfig.StereoSGBM_DispMaxDiff,
                stereoConfig.StereoSGBM_PreFilterCap, stereoConfig.StereoSGBM_UniquenessRatio,
                stereoConfig.StereoSGBM_SpeckleWindowSize, speckleRange,
                (int)stereoConfig.StereoSGBM_Mode);

            m_stereoRightMatcher->compute(m_scaledExtFrameRight, m_scaledExtFrameLeft, m_rawDisparityRight);

            outputMatrixLeft = &m_rawDisparityLeft;
            outputMatrixRight = &m_rawDisparityRight;

            outputConfMatrixLeft = &m_confidenceLeft;
            outputConfMatrixRight = &m_confidenceRight;
        }
        else
        {
            outputMatrixLeft = &m_rawDisparityLeft;
            outputMatrixRight = &m_rawDisparityLeft;

            outputConfMatrixLeft = &m_confidenceLeft;
            outputConfMatrixRight = &m_confidenceLeft;
        }


        if(stereoConfig.StereoFilteringWLS_Enable)
        {
            cv::Rect leftROI = cv::Rect(0, 0, m_cvImageWidth + numDisparities, m_cvImageHeight);

            if (!m_bDisparityBothEyes)
            {
                m_stereoRightMatcher = cv::ximgproc::createRightMatcher(m_stereoLeftMatcher);

                m_stereoRightMatcher->compute(m_scaledExtFrameRight, m_scaledExtFrameLeft, m_rawDisparityRight);

                leftROI = cv::Rect();
            }

            m_wlsFilterLeft = cv::ximgproc::createDisparityWLSFilter(m_stereoLeftMatcher);

            m_wlsFilterLeft->setLambda(stereoConfig.StereoFilteringWLS_Lambda);
            m_wlsFilterLeft->setSigmaColor(stereoConfig.StereoFilteringWLS_Sigma);
            m_wlsFilterLeft->setDepthDiscontinuityRadius((int)ceil(stereoConfig.StereoFilteringWLS_ConfidenceRadius * stereoConfig.StereoBlockSize));

            m_wlsFilterLeft->filter(m_rawDisparityLeft, m_scaledExtFrameLeft, m_filteredDisparityLeft, m_rawDisparityRight, leftROI, m_scaledExtFrameRight);


            if (m_bDisparityBothEyes)
            {
                m_wlsFilterRight = cv::ximgproc::createDisparityWLSFilter(m_stereoRightMatcher);

                m_wlsFilterRight->setLambda(stereoConfig.StereoFilteringWLS_Lambda);
                m_wlsFilterRight->setSigmaColor(stereoConfig.StereoFilteringWLS_Sigma);
                m_wlsFilterRight->setDepthDiscontinuityRadius((int)ceil(stereoConfig.StereoFilteringWLS_ConfidenceRadius * stereoConfig.StereoBlockSize));
                cv::Rect filterROI = cv::Rect(0, 0, m_cvImageWidth + numDisparities, m_cvImageHeight);

                m_wlsFilterRight->filter(m_rawDisparityRight, m_scaledExtFrameRight, m_filteredDisparityRight, m_rawDisparityLeft, filterROI, m_scaledExtFrameLeft);

                m_confidenceLeft = m_wlsFilterLeft->getConfidenceMap();
                m_confidenceRight = m_wlsFilterRight->getConfidenceMap();

                outputMatrixLeft = &m_filteredDisparityLeft;
                outputMatrixRight = &m_filteredDisparityRight;
            }
            else
            {
                m_confidenceLeft = m_wlsFilterLeft->getConfidenceMap();
                m_confidenceRight = m_wlsFilterLeft->getConfidenceMap();

                outputMatrixLeft = &m_filteredDisparityLeft;
                outputMatrixRight = &m_filteredDisparityLeft;
            }
        }

        {
            FramePtr<DepthFrame> frame = m_depthFrameQueue.AcquireWrite();

            if (frame->DisparityTextureIndex < 0)
            {
                frame->DisparityTextureIndex = m_depthFrameIndex++;
            }

            XrMatrix4x4f rectifiedRotationInvLeft;
            XrMatrix4x4f_Transpose(&rectifiedRotationInvLeft, &m_rectifiedRotationLeft);
            XrMatrix4x4f_Multiply(&frame->DisparityViewToWorldLeft, &viewToWorldLeft, &rectifiedRotationInvLeft);

            if (m_bDisparityBothEyes)
            {
                XrMatrix4x4f rectifiedRotationInvRight;
                XrMatrix4x4f_Transpose(&rectifiedRotationInvRight, &m_rectifiedRotationRight);
                XrMatrix4x4f_Multiply(&frame->DisparityViewToWorldRight, &viewToWorldRight, &rectifiedRotationInvRight);
            }
            else
            {
                frame->DisparityViewToWorldRight = frame->DisparityViewToWorldLeft;
            }

            int outputScale = stereoConfig.StereoFilteringBilateral_Enable ? stereoConfig.StereoFilteringBilateral_OutputScale : 1;


            // Precompute disparity-to-depth scaling factors for the shader inputs.
            frame->DisparityToDepth = m_disparityToDepth;
            frame->DisparityToDepth.m[0] *= (float)m_cvImageWidth;
            frame->DisparityToDepth.m[5] *= -(float)m_cvImageHeight;
            frame->DisparityToDepth.m[12] /= (float)m_downscaleFactor;
            frame->DisparityToDepth.m[13] /= -(float)m_downscaleFactor;
            frame->DisparityToDepth.m[14] /= -(float)m_downscaleFactor;
            // Convert z to int16 range with 4 bit fixed decimal: 65536 / 2 / 16 = 2048
            frame->DisparityToDepth.m[11] *= 2048.0f;

            frame->InputDisparityTextureSize[0] = m_cvImageWidth * 2;
            frame->InputDisparityTextureSize[1] = m_cvImageHeight;
            frame->OutputDisparityTextureSize[0] = m_cvImageWidth * 2 * outputScale;
            frame->OutputDisparityTextureSize[1] = m_cvImageHeight * outputScale;
            frame->CameraFrameTextureSize[0] = m_cameraFrameWidth * 2;
            frame->CameraFrameTextureSize[1] = m_cameraFrameHeight;
            frame->DisparityDownscaleFactor = (float)m_downscaleFactor / outputScale;
            frame->FrameExposureTimestamp = frameTimestamp;
            frame->MinDisparity = stereoConfig.StereoMinDisparity / 2048.0f;
            frame->MaxDisparity = m_maxDisparity / 2048.0f;
            frame->bIsValid = true;
            frame->bIsFirstRender = true;


            m_reconstructionTimer.EndPerfTimer();
            m_renderTimer.StartPerfTimer();


            if (!m_asyncRenderer->BeginRender(frame.GetSharedPointer()))
            {
                continue;
            }

            // Write disparity and confidence to texture

            cv::Rect copySrcRegion = cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight);

            m_outputDisparityBuffer.resize(m_cvImageHeight * 2 * m_cvImageWidth * 2);

            m_outputDisparity = cv::Mat(m_cvImageHeight, m_cvImageWidth * 2, CV_16S, m_outputDisparityBuffer.data());

            m_outputDisparityLeft = m_outputDisparity(cv::Rect(0, 0, m_cvImageWidth, m_cvImageHeight));
            m_outputDisparityRight = m_outputDisparity(cv::Rect(m_cvImageWidth, 0, m_cvImageWidth, m_cvImageHeight));

            (*outputMatrixLeft)(copySrcRegion).copyTo(m_outputDisparityLeft);

            if (m_bDisparityBothEyes)
            {
                // Invert right eye disparity
                (*outputMatrixRight)(copySrcRegion).convertTo(m_outputDisparityRight, CV_16S, -1);
            }
            else
            {
                (*outputMatrixRight)(copySrcRegion).copyTo(m_outputDisparityRight);
            }

            m_asyncRenderer->CopyDisparityToGPU(m_outputDisparityBuffer);

            m_outputConfidenceBuffer.resize(m_cvImageHeight * 2 * m_cvImageWidth * 2);

            m_outputConfidence = cv::Mat(m_cvImageHeight, m_cvImageWidth * 2, CV_16S, m_outputConfidenceBuffer.data());

            m_outputConfidenceLeft = m_outputConfidence(cv::Rect(0, 0, m_cvImageWidth, m_cvImageHeight));
            m_outputConfidenceRight = m_outputConfidence(cv::Rect(m_cvImageWidth, 0, m_cvImageWidth, m_cvImageHeight));

            if (stereoConfig.StereoFilteringWLS_Enable)
            {
                float confFactor = stereoConfig.StereoFilteringWLS_Enable ? 32768.0f / 255.0f : 32768.0f;

                if ((uint32_t)outputConfMatrixLeft->size().width >= m_cvImageWidth + numDisparities)
                {
                    (*outputConfMatrixLeft)(copySrcRegion).convertTo(m_outputConfidenceLeft, CV_16S, confFactor);

                    if (!m_bDisparityBothEyes)
                    {
                        (*outputConfMatrixLeft)(copySrcRegion).convertTo(m_outputConfidenceRight, CV_16S, confFactor);
                    }
                }
                else
                {
                    m_outputConfidenceLeft = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);

                    if (!m_bDisparityBothEyes)
                    {
                        m_outputConfidenceRight = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);
                    }
                }

                if (m_bDisparityBothEyes)
                {
                    if ((uint32_t)(*outputConfMatrixRight).size().width >= m_cvImageWidth + numDisparities)
                    {
                        (*outputConfMatrixRight)(copySrcRegion).convertTo(m_outputConfidenceRight, CV_16S, confFactor);
                    }
                    else
                    {
                        m_outputConfidenceRight = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);
                    }
                }
            }
            else
            {
                m_outputConfidenceLeft = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);
                m_outputConfidenceRight = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);
            }

            m_asyncRenderer->CopyConfidenceToGPU(m_outputConfidenceBuffer);

            // Copy rectified camera frame associated with disparity
            if(stereoConfig.StereoFilteringBilateral_Enable)
            {
                m_outputCameraFrameBuffer.resize(m_cameraFrameWidth * 2 * m_cameraFrameHeight);

                m_outputCameraFrame = cv::Mat(m_cameraFrameHeight, m_cameraFrameWidth * 2, CV_8U, m_outputCameraFrameBuffer.data());

                m_outputCameraFrameLeft = m_outputCameraFrame(cv::Rect(0, 0, m_cameraFrameWidth, m_cameraFrameHeight));
                m_outputCameraFrameRight = m_outputCameraFrame(cv::Rect(m_cameraFrameWidth, 0, m_cameraFrameWidth, m_cameraFrameHeight));

                if (m_bUseColor)
                {
                    cv::cvtColor(m_rectifiedFrameLeft, m_outputCameraFrameLeft, cv::COLOR_RGB2GRAY);
                    cv::cvtColor(m_rectifiedFrameRight, m_outputCameraFrameRight, cv::COLOR_RGB2GRAY);
                }
                else
                {
                    m_rectifiedFrameLeft.copyTo(m_outputCameraFrameLeft);
                    m_rectifiedFrameRight.copyTo(m_outputCameraFrameRight);
                }

                m_asyncRenderer->CopyBWRectifiedCameraFrameToGPU(m_outputCameraFrameBuffer);
            }

            m_asyncRenderer->Render(frame.GetSharedPointer(), stereoConfig);

            frame.CommitWrite();
        }

        if (mainConfig.DebugTexture != DebugTexture_None)
        {
            DebugTexture& texture = m_configManager->GetDebugTexture();
            std::lock_guard<std::mutex> writelock(texture.RWMutex);

            if (mainConfig.DebugTexture == DebugTexture_Disparity)
            {
                if (texture.CurrentTexture != DebugTexture_Disparity)
                {
                    texture.Texture = std::vector<uint8_t>();
                    texture.Texture.resize(m_cvImageWidth * 2 * m_cvImageHeight * sizeof(uint16_t));
                }
                cv::Mat debugTextureMat(m_cvImageHeight, m_cvImageWidth * 2, CV_16S, texture.Texture.data());

                cv::Mat left = debugTextureMat(cv::Rect(0, 0, m_cvImageWidth, m_cvImageHeight));
                cv::Mat right = debugTextureMat(cv::Rect(m_cvImageWidth, 0, m_cvImageWidth, m_cvImageHeight));

                (*outputMatrixLeft)(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(left, CV_16S);

                cv::Mat rightFlip;
                (*outputMatrixRight)(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)).copyTo(rightFlip);

                rightFlip.convertTo(right, CV_16S);

                debugTextureMat *= 8;

                if (texture.Width != m_cvImageWidth || texture.Height != m_cvImageHeight)
                {
                    texture.bDimensionsUpdated = true;
                }

                texture.Width = m_cvImageWidth * 2;
                texture.Height = m_cvImageHeight;
                texture.PixelSize = sizeof(uint16_t);
                texture.Format = DebugTextureFormat_R16S;
                texture.CurrentTexture = DebugTexture_Disparity;

            }
            else if (mainConfig.DebugTexture == DebugTexture_Confidence)
            {
                if (texture.CurrentTexture != DebugTexture_Confidence)
                {
                    texture.Texture = std::vector<uint8_t>();
                    texture.Texture.resize(m_cvImageWidth * 2 * m_cvImageHeight * sizeof(uint16_t));
                }
                cv::Mat debugTextureMat(m_cvImageHeight, m_cvImageWidth * 2, CV_8U, texture.Texture.data());

                cv::Mat left = debugTextureMat(cv::Rect(0, 0, m_cvImageWidth, m_cvImageHeight));
                cv::Mat right = debugTextureMat(cv::Rect(m_cvImageWidth, 0, m_cvImageWidth, m_cvImageHeight));

                if (stereoConfig.StereoFilteringWLS_Enable)
                {
                    m_confidenceLeft = m_wlsFilterLeft->getConfidenceMap();
                    if (m_bDisparityBothEyes)
                    {
                        m_confidenceRight = m_wlsFilterRight->getConfidenceMap();
                    }
                    else
                    {
                        m_confidenceRight = m_wlsFilterLeft->getConfidenceMap();
                    }
                }

                if ((uint32_t)m_confidenceLeft.size().width >= m_cvImageWidth + numDisparities)
                {
                    m_confidenceLeft(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(left, CV_8U);
                }
                if ((uint32_t)m_confidenceRight.size().width >= m_cvImageWidth + numDisparities)
                {
                    m_confidenceRight(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(right, CV_8U);
                }

                if (texture.Width != m_cvImageWidth || texture.Height != m_cvImageHeight)
                {
                    texture.bDimensionsUpdated = true;
                }

                texture.Width = m_cvImageWidth * 2;
                texture.Height = m_cvImageHeight;
                texture.PixelSize = sizeof(uint8_t);
                texture.Format = DebugTextureFormat_R8;
                texture.CurrentTexture = DebugTexture_Confidence;

            }
        }
        
        m_renderTimer.EndPerfTimer();
    }
}

