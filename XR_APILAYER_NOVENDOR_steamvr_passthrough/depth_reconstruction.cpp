#include "pch.h"
#include "depth_reconstruction.h"

#include <log.h>
#include "mathutil.h"


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;




DepthReconstruction::DepthReconstruction(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, std::shared_ptr<ICameraManager> cameraManager)
    : m_bRunThread(true)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_cameraManager(cameraManager)
    , m_distortionParams()
    , m_reconstructionTimes({0.0f})
    , m_averageReconstructionTime(0.0f)
{
    Config_Stereo& stereoConfig = m_configManager->GetConfig_Stereo();

    m_maxDisparity = stereoConfig.StereoMaxDisparity;
    m_downscaleFactor = stereoConfig.StereoDownscaleFactor;
    m_depthFrame = std::make_shared<DepthFrame>();
    m_servedDepthFrame = std::make_shared<DepthFrame>();
    m_underConstructionDepthFrame = std::make_shared<DepthFrame>();

    m_fovScale = m_configManager->GetConfig_Main().FieldOfViewScale;
    m_depthOffsetCalibration = m_configManager->GetConfig_Main().DepthOffsetCalibration;
    m_bUseColor = stereoConfig.StereoUseColor;
    m_bDisparityBothEyes = stereoConfig.StereoDisparityBothEyes;

    m_bUseMulticore = stereoConfig.StereoUseMulticore;
    cv::setNumThreads(m_bUseMulticore ? -1 : 0);

    InitReconstruction();

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

std::shared_ptr<DepthFrame> DepthReconstruction::GetDepthFrame()
{
    std::unique_lock<std::mutex> lock(m_serveMutex, std::try_to_lock);
    if (lock.owns_lock() && m_servedDepthFrame->bIsValid)
    {
        m_depthFrame->bIsValid = false;
        m_depthFrame.swap(m_servedDepthFrame);
    }

    return m_depthFrame;
}

void DepthReconstruction::InitReconstruction()
{
    m_frameLayout = m_cameraManager->GetFrameLayout();
    uint32_t frameBufferSize;
    m_cameraManager->GetDistortedTextureSize(m_cameraTextureWidth, m_cameraTextureHeight, frameBufferSize);
    m_cameraManager->GetDistortedFrameSize(m_cameraFrameWidth, m_cameraFrameHeight);

    m_cvImageHeight = m_cameraFrameHeight / m_downscaleFactor;
    m_cvImageWidth = m_cameraFrameWidth / m_downscaleFactor;

    m_cameraManager->GetIntrinsics(LEFT_EYE, m_cameraFocalLength[0], m_cameraCenter[0]);
    m_cameraManager->GetIntrinsics(RIGHT_EYE, m_cameraFocalLength[1], m_cameraCenter[1]);
    m_cameraLeftToRightTransform = m_cameraManager->GetLeftToRightCameraTransform();

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
   
    XrMatrix4x4f rotateX180Matrix, tempMatrix, leftToRightMatrix, leftToRightTransposed;
    XrMatrix4x4f_CreateRotation(&rotateX180Matrix, 180.0f, 0.0f, 0.0f);

    XrMatrix4x4f_Multiply(&tempMatrix, &m_cameraLeftToRightTransform, &rotateX180Matrix);
    XrMatrix4x4f_Multiply(&leftToRightMatrix, &rotateX180Matrix, &tempMatrix);

    double leftToRightTranslation[3] = { leftToRightMatrix.m[12], leftToRightMatrix.m[13], leftToRightMatrix.m[14] };

    leftToRightTranslation[0] *= m_depthOffsetCalibration;
    leftToRightTranslation[1] *= m_depthOffsetCalibration;
    leftToRightTranslation[2] *= m_depthOffsetCalibration;

    XrMatrix4x4f_Transpose(&leftToRightTransposed, &leftToRightMatrix);

    double leftToRightRotation[9] = {
        leftToRightTransposed.m[0], leftToRightTransposed.m[1], leftToRightTransposed.m[2],
        leftToRightTransposed.m[4], leftToRightTransposed.m[5], leftToRightTransposed.m[6],
        leftToRightTransposed.m[8], leftToRightTransposed.m[9], leftToRightTransposed.m[10] };

    cv::Mat R(cv::Size(3, 3), CV_64F, leftToRightRotation);
    cv::Mat T(3, 1, CV_64F, leftToRightTranslation);
    cv::Mat R1, R2, R3, P1, P2, Q;

    cv::Size textureSize(m_cameraFrameWidth, m_cameraFrameHeight);

    if (m_cameraManager->IsUsingFisheyeModel())
    {
        if (m_frameLayout != Mono)
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
        if (m_frameLayout != Mono)
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

    XrMatrix4x4f XR_R1 = CVMatToXrMatrix(R1);
    XrMatrix4x4f_Transpose(&m_rectifiedRotationLeft, &XR_R1);
    XrMatrix4x4f XR_R2 = CVMatToXrMatrix(R2);
    XrMatrix4x4f_Transpose(&m_rectifiedRotationRight, &XR_R2);

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

    {
        std::unique_lock writeLock(m_depthFrame->readWriteMutex);
        std::unique_lock writeLock2(m_servedDepthFrame->readWriteMutex);
        std::unique_lock writeLock3(m_underConstructionDepthFrame->readWriteMutex);

        m_depthFrame->disparityMap->resize(m_cvImageWidth * m_cvImageHeight * 2 * 2);
        m_servedDepthFrame->disparityMap->resize(m_cvImageWidth * m_cvImageHeight * 2 * 2);
        m_underConstructionDepthFrame->disparityMap->resize(m_cvImageWidth * m_cvImageHeight * 2 * 2);
    }
}


void DepthReconstruction::CreateDistortionMap()
{
    std::unique_lock writeLock(m_distortionParams.readWriteMutex);

    if (!m_distortionParams.uvDistortionMap.get())
    {
        m_distortionParams.uvDistortionMap = std::make_shared<std::vector<float>>();
    }

    m_distortionParams.cameraProjectionLeft = m_fishEyeProjectionLeft;
    m_distortionParams.cameraProjectionRight = m_fishEyeProjectionRight;
    m_distortionParams.rectifiedRotationLeft = m_rectifiedRotationLeft;
    m_distortionParams.rectifiedRotationRight = m_rectifiedRotationRight;

    m_distortionParams.fovScale = m_fovScale;

    std::vector<float>& distMap = *m_distortionParams.uvDistortionMap.get();

    distMap.resize(m_cameraTextureHeight * m_cameraTextureWidth * 2);

    if (m_frameLayout == StereoHorizontalLayout)
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
    else if (m_frameLayout == StereoVerticalLayout)
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


void DisparityFillHoles(cv::Mat& disparity, cv::Mat& confidence, int maxDisparity, int minDisparity, bool bRightToLeft)
{
    confidence = cv::Mat(disparity.rows, disparity.cols, CV_32F, 255.0);

    for (int x = (bRightToLeft ? disparity.cols - 1 : 0); bRightToLeft ? (x > 0) : (x < disparity.cols - 1); x += (bRightToLeft ? -1 : 1))
    {
        for (int y = 0; y < disparity.rows; y++)
        {
            int16_t disp = disparity.at<int16_t>(y, x);
            if (disp < maxDisparity * 16 && disp > minDisparity * 16)
            {
                int pos = x + (bRightToLeft ? -1 : 1);
                bool bFirst = true;
                while (pos >= 0 && pos < disparity.rows - 1)
                {
                    
                    int16_t& element = disparity.at<int16_t>(y, pos);

                    if (element <= maxDisparity * 16 && element >= minDisparity * 16)
                    {
                        if (!bFirst)
                        {
                            confidence.at<float>(y, pos) = 0.0;
                        }
                        break;
                    }

                    if (bFirst)
                    {
                        confidence.at<float>(y, x) = 0.0;
                    }

                    confidence.at<float>(y, pos) = 0.0;

                    element = disp;

                    pos += (bRightToLeft ? -1 : 1);
                }
            }
            else
            {
                disparity.at<int16_t>(y, x) = (minDisparity < 0 ? maxDisparity * 16 : minDisparity * 16);
                confidence.at<float>(y, x) = 0.0;
            }
        }
    }
}


void DepthReconstruction::RunThread()
{
    while (m_bRunThread)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));

        LARGE_INTEGER startReconstructionTime = StartPerfTimer();

        // Make local copies for consistency
        Config_Main mainConfig = m_configManager->GetConfig_Main();
        Config_Stereo stereoConfig = m_configManager->GetConfig_Stereo();


        if (m_maxDisparity != stereoConfig.StereoMaxDisparity ||
            m_downscaleFactor != stereoConfig.StereoDownscaleFactor ||
            m_fovScale != mainConfig.FieldOfViewScale ||
            m_depthOffsetCalibration != mainConfig.DepthOffsetCalibration ||
            m_bUseColor != stereoConfig.StereoUseColor ||
            m_bDisparityBothEyes != stereoConfig.StereoDisparityBothEyes)
        {
            m_maxDisparity = stereoConfig.StereoMaxDisparity;
            m_downscaleFactor = stereoConfig.StereoDownscaleFactor;
            m_fovScale = mainConfig.FieldOfViewScale;
            m_depthOffsetCalibration = mainConfig.DepthOffsetCalibration;
            m_bUseColor = stereoConfig.StereoUseColor;
            m_bDisparityBothEyes = stereoConfig.StereoDisparityBothEyes;

            InitReconstruction();
        }

        if (m_bUseMulticore != stereoConfig.StereoUseMulticore)
        {
            m_bUseMulticore = stereoConfig.StereoUseMulticore;
            cv::setNumThreads(m_bUseMulticore ? -1 : 0);
        }

        std::shared_ptr<CameraFrame> frame;
        XrMatrix4x4f viewToWorldLeft, viewToWorldRight;

        if (mainConfig.ProjectionMode != Projection_StereoReconstruction || stereoConfig.StereoReconstructionFreeze || !m_cameraManager->GetCameraFrame(frame))
        {
            continue;
        }

        {
            std::shared_lock readLock(frame->readWriteMutex);

            if(!frame->bHasFrameBuffer || 
                frame->frameLayout == Mono || 
                frame->frameBuffer->size() < m_cameraTextureHeight * m_cameraTextureWidth * 4 || 
                frame->header.nFrameSequence == m_lastFrameSequence || 
                frame->header.nFrameSequence % (stereoConfig.StereoFrameSkip + 1) != 0)
            {
                continue;
            }

            m_lastFrameSequence = frame->header.nFrameSequence;

            viewToWorldLeft = frame->cameraViewToWorldLeft;
            viewToWorldRight = frame->cameraViewToWorldRight;

            m_inputFrame = cv::Mat(m_cameraTextureHeight, m_cameraTextureWidth, CV_8UC4, frame->frameBuffer->data());
            
            cv::Rect frameROILeft, frameROIRight;

            if (m_frameLayout == StereoHorizontalLayout)
            {
                frameROILeft = cv::Rect(0, 0, m_cameraFrameWidth, m_cameraFrameHeight);
                frameROIRight = cv::Rect(m_cameraFrameWidth, 0, m_cameraFrameWidth, m_cameraFrameHeight);
            }
            else if (m_frameLayout == StereoVerticalLayout)
            {
                frameROILeft = cv::Rect(0, m_cameraFrameHeight, m_cameraFrameWidth, m_cameraFrameHeight);
                frameROIRight = cv::Rect(0, 0, m_cameraFrameWidth, m_cameraFrameHeight);
            }

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

        if (stereoConfig.StereoFiltering == StereoFiltering_None && stereoConfig.StereoFillHoles)
        {
            DisparityFillHoles(m_rawDisparityLeft, m_confidenceLeft, m_maxDisparity, minDisparity, false);

            if (m_bDisparityBothEyes)
            {
                DisparityFillHoles(m_rawDisparityRight, m_confidenceRight, minDisparity, -m_maxDisparity, true);
            }
        }


        if (stereoConfig.StereoFiltering == StereoFiltering_FBS)
        {
            DisparityFillHoles(m_rawDisparityLeft, m_confidenceLeft, m_maxDisparity, minDisparity, false);

            cv::ximgproc::fastBilateralSolverFilter(m_scaledExtFrameLeft, m_rawDisparityLeft, m_confidenceLeft, m_bilateralDisparityLeft, stereoConfig.StereoFBS_Spatial, stereoConfig.StereoFBS_Luma, stereoConfig.StereoFBS_Chroma, stereoConfig.StereoFBS_Lambda, stereoConfig.StereoFBS_Iterations);

            outputMatrixLeft = &m_bilateralDisparityLeft;

            

            if (m_bDisparityBothEyes)
            {
                DisparityFillHoles(m_rawDisparityRight, m_confidenceRight, minDisparity, -m_maxDisparity, true);

                cv::ximgproc::fastBilateralSolverFilter(m_scaledExtFrameRight, m_rawDisparityRight, m_confidenceRight, m_bilateralDisparityRight, stereoConfig.StereoFBS_Spatial, stereoConfig.StereoFBS_Luma, stereoConfig.StereoFBS_Chroma, stereoConfig.StereoFBS_Lambda, stereoConfig.StereoFBS_Iterations);

                outputMatrixRight = &m_bilateralDisparityRight;
            }
            else
            {
                outputMatrixRight = &m_bilateralDisparityLeft;
            }
        }
        else if(stereoConfig.StereoFiltering != StereoFiltering_None)
        {
            cv::Rect leftROI = cv::Rect(0, 0, m_cvImageWidth + numDisparities, m_cvImageHeight);

            if (!m_bDisparityBothEyes)
            {
                m_stereoRightMatcher = cv::ximgproc::createRightMatcher(m_stereoLeftMatcher);

                m_stereoRightMatcher->compute(m_scaledExtFrameRight, m_scaledExtFrameLeft, m_rawDisparityRight);

                leftROI = cv::Rect();
            }

            m_wlsFilterLeft = cv::ximgproc::createDisparityWLSFilter(m_stereoLeftMatcher);

            m_wlsFilterLeft->setLambda(stereoConfig.StereoWLS_Lambda);
            m_wlsFilterLeft->setSigmaColor(stereoConfig.StereoWLS_Sigma);
            m_wlsFilterLeft->setDepthDiscontinuityRadius((int)ceil(stereoConfig.StereoWLS_ConfidenceRadius * stereoConfig.StereoBlockSize));

            m_wlsFilterLeft->filter(m_rawDisparityLeft, m_scaledExtFrameLeft, m_filteredDisparityLeft, m_rawDisparityRight, leftROI, m_scaledExtFrameRight);


            if (m_bDisparityBothEyes)
            {
                m_wlsFilterRight = cv::ximgproc::createDisparityWLSFilter(m_stereoRightMatcher);

                m_wlsFilterRight->setLambda(stereoConfig.StereoWLS_Lambda);
                m_wlsFilterRight->setSigmaColor(stereoConfig.StereoWLS_Sigma);
                m_wlsFilterRight->setDepthDiscontinuityRadius((int)ceil(stereoConfig.StereoWLS_ConfidenceRadius * stereoConfig.StereoBlockSize));
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


            if (stereoConfig.StereoFiltering == StereoFiltering_WLS_FBS)
            {
                cv::ximgproc::fastBilateralSolverFilter(m_scaledExtFrameLeft, m_filteredDisparityLeft, m_confidenceLeft, m_bilateralDisparityLeft, stereoConfig.StereoFBS_Spatial, stereoConfig.StereoFBS_Luma, stereoConfig.StereoFBS_Chroma, stereoConfig.StereoFBS_Lambda, stereoConfig.StereoFBS_Iterations);

                outputMatrixLeft = &m_bilateralDisparityLeft;
                

                if (m_bDisparityBothEyes)
                {
                    cv::ximgproc::fastBilateralSolverFilter(m_scaledExtFrameRight, m_filteredDisparityRight, m_confidenceRight, m_bilateralDisparityRight, stereoConfig.StereoFBS_Spatial, stereoConfig.StereoFBS_Luma, stereoConfig.StereoFBS_Chroma, stereoConfig.StereoFBS_Lambda, stereoConfig.StereoFBS_Iterations);

                    outputMatrixRight = &m_bilateralDisparityRight;
                }
                else
                {
                    outputMatrixRight = &m_bilateralDisparityLeft;
                }
            }
        }

        {
            std::unique_lock writeLock(m_underConstructionDepthFrame->readWriteMutex);

            // Write disparity and confidence to texture

            m_outputDisparity = cv::Mat(m_cvImageHeight, m_cvImageWidth * 2, CV_16SC2, m_underConstructionDepthFrame->disparityMap->data());

            m_outputDisparityLeft = m_outputDisparity(cv::Rect(0, 0, m_cvImageWidth, m_cvImageHeight));
            m_outputDisparityRight = m_outputDisparity(cv::Rect(m_cvImageWidth, 0, m_cvImageWidth, m_cvImageHeight));

            cv::Mat leftIn[2];
            cv::Mat rightIn[2];

            leftIn[0] = (*outputMatrixLeft)(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight));
            rightIn[0] = (*outputMatrixRight)(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight));

            if (stereoConfig.StereoFiltering != StereoFiltering_None || stereoConfig.StereoFillHoles)
            {
                float confFactor = (stereoConfig.StereoFiltering != StereoFiltering_None) ? 32768.0f / 255.0f : 32768.0f;

                if ((uint32_t)outputConfMatrixLeft->size().width >= m_cvImageWidth + numDisparities)
                {
                    (*outputConfMatrixLeft)(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(leftIn[1], CV_16S, confFactor);

                    if (!m_bDisparityBothEyes)
                    {
                        (*outputConfMatrixLeft)(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(rightIn[1], CV_16S, confFactor);
                    }
                }
                else
                {
                    leftIn[1] = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);

                    if (!m_bDisparityBothEyes)
                    {
                        rightIn[1] = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);
                    }
                }

                if (m_bDisparityBothEyes)
                {
                    if ((uint32_t)(*outputConfMatrixRight).size().width >= m_cvImageWidth + numDisparities)
                    {
                        (*outputConfMatrixRight)(cv::Rect(numDisparities, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(rightIn[1], CV_16S, confFactor);
                    }
                    else
                    {
                        rightIn[1] = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);
                    }
                }

                int fromTo[4] = { 0,0 , 1,1 };
                cv::mixChannels(leftIn, 2, &m_outputDisparityLeft, 1, fromTo, 2);
                cv::mixChannels(rightIn, 2, &m_outputDisparityRight, 1, fromTo, 2);

            }
            else
            {
                leftIn[1] = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);
                rightIn[1] = cv::Mat::zeros(m_cvImageHeight, m_cvImageWidth, CV_16S);

                int fromTo[4] = { 0,0 , 1,1 };
                cv::mixChannels(leftIn, 2, &m_outputDisparityLeft, 1, fromTo, 2);
                cv::mixChannels(rightIn, 2, &m_outputDisparityRight, 1, fromTo, 2);
            }

            if (m_bDisparityBothEyes)
            {
                // Invert right eye disparity
                m_outputDisparityRight.forEach<cv::Vec2s>([this](cv::Vec2s& element, const int* position) -> void
                {
                    element[0] *= -1;
                });
            }
            

            XrMatrix4x4f_Multiply(&m_underConstructionDepthFrame->disparityViewToWorldLeft, &viewToWorldLeft, &m_rectifiedRotationLeft);
            if (m_bDisparityBothEyes)
            {
                XrMatrix4x4f_Multiply(&m_underConstructionDepthFrame->disparityViewToWorldRight, &viewToWorldRight, &m_rectifiedRotationRight);
            }
            else
            {
                m_underConstructionDepthFrame->disparityViewToWorldRight = m_underConstructionDepthFrame->disparityViewToWorldLeft;
            }
            m_underConstructionDepthFrame->disparityToDepth = m_disparityToDepth;
            m_underConstructionDepthFrame->disparityTextureSize[0] = m_cvImageWidth * 2;
            m_underConstructionDepthFrame->disparityTextureSize[1] = m_cvImageHeight;
            m_underConstructionDepthFrame->disparityDownscaleFactor = (float)m_downscaleFactor;
            m_underConstructionDepthFrame->minDisparity = stereoConfig.StereoMinDisparity / 2048.0f;
            m_underConstructionDepthFrame->maxDisparity = m_maxDisparity / 2048.0f;
            m_underConstructionDepthFrame->bIsValid = true;
            m_underConstructionDepthFrame->bIsFirstRender = true;

            {
                std::lock_guard<std::mutex> lock(m_serveMutex);
                m_underConstructionDepthFrame.swap(m_servedDepthFrame);
            }
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

                if (stereoConfig.StereoFiltering == StereoFiltering_WLS)
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
        
        m_averageReconstructionTime = UpdateAveragePerfTime(m_reconstructionTimes, EndPerfTimer(startReconstructionTime), 20);
    }
}



