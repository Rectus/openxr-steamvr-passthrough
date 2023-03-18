#include "pch.h"
#include "depth_reconstruction.h"

#include <log.h>


using namespace steamvr_passthrough;
using namespace steamvr_passthrough::log;


inline XrMatrix4x4f ToXRMatrix4x4(vr::HmdMatrix34_t& input)
{
    XrMatrix4x4f output =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], 0,
        input.m[0][1], input.m[1][1], input.m[2][1], 0,
        input.m[0][2], input.m[1][2], input.m[2][2], 0,
        input.m[0][3], input.m[1][3], input.m[2][3], 1
    };
    return output;
}

XrMatrix4x4f CVMatToXrMatrix(cv::Mat& inMatrix)
{
    XrMatrix4x4f outMatrix;
    XrMatrix4x4f_CreateIdentity(&outMatrix);
    for (int y = 0; y < inMatrix.rows; y++)
    {
        for (int x = 0; x < inMatrix.cols; x++)
        {
            outMatrix.m[x + y * 4] = (float)inMatrix.at<double>(y, x);
        }
    }
    return outMatrix;
}



DepthReconstruction::DepthReconstruction(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, std::shared_ptr<CameraManager> cameraManager)
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
    m_cameraManager->GetFrameSize(m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);

    if (m_frameLayout == StereoHorizontalLayout)
    {
        m_cameraFrameWidth = m_cameraTextureWidth / 2;
        m_cameraFrameHeight = m_cameraTextureHeight;
    }
    else if (m_frameLayout == StereoVerticalLayout)
    {
        m_cameraFrameWidth = m_cameraTextureWidth;
        m_cameraFrameHeight = m_cameraTextureHeight / 2;
    }
    else
    {
        m_cameraFrameWidth = m_cameraTextureWidth;
        m_cameraFrameHeight = m_cameraTextureHeight;
    }

    m_cvImageHeight = m_cameraFrameHeight / m_downscaleFactor;
    m_cvImageWidth = m_cameraFrameWidth / m_downscaleFactor;

    m_cameraManager->GetIntrinsics(0, m_cameraFocalLength[0], m_cameraCenter[0]);
    m_cameraManager->GetIntrinsics(1, m_cameraFocalLength[1], m_cameraCenter[1]);
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

    cv::fisheye::stereoRectify(m_intrinsicsLeft, m_distortionParamsLeft, m_intrinsicsRight, m_distortionParamsRight, textureSize, R, T, R1, R2, P1, P2, Q, cv::CALIB_ZERO_DISPARITY, textureSize, 0.0, m_fovScale);

    cv::fisheye::initUndistortRectifyMap(m_intrinsicsLeft, m_distortionParamsLeft, R1, P1, textureSize, CV_32FC1, m_leftMap1, m_leftMap2);
    cv::fisheye::initUndistortRectifyMap(m_intrinsicsRight, m_distortionParamsRight, R2, P2, textureSize, CV_32FC1, m_rightMap1, m_rightMap2);

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

    m_inputFrameLeft = cv::Mat(m_cameraFrameHeight, m_cameraFrameWidth, frameFormat);
    m_inputFrameRight = cv::Mat(m_cameraFrameHeight, m_cameraFrameWidth, frameFormat);
    m_rectifiedFrameLeft = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_rectifiedFrameRight = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_scaledFrameLeft = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_scaledFrameRight = cv::Mat(m_cvImageHeight, m_cvImageWidth, frameFormat);
    m_scaledExtFrameLeft = cv::Mat(m_cvImageHeight, m_cvImageWidth + m_maxDisparity, frameFormat);
    m_scaledExtFrameRight = cv::Mat(m_cvImageHeight, m_cvImageWidth + m_maxDisparity, frameFormat);
    

    m_rawDisparity = cv::Mat(m_cvImageHeight, m_cvImageWidth + m_maxDisparity, CV_16S);
    m_rightDisparity = cv::Mat(m_cvImageHeight, m_cvImageWidth + m_maxDisparity, CV_16S);
    m_filteredDisparity = cv::Mat(m_cvImageHeight, m_cvImageWidth + m_maxDisparity, CV_16S);
    m_bilateralDisparity = cv::Mat(m_cvImageHeight, m_cvImageWidth + m_maxDisparity, CV_16S);

    std::unique_lock writeLock(m_depthFrame->readWriteMutex);
    std::unique_lock writeLock2(m_servedDepthFrame->readWriteMutex);
    std::unique_lock writeLock3(m_underConstructionDepthFrame->readWriteMutex);

    m_depthFrame->disparityMap->resize(m_cvImageWidth * m_cvImageHeight);
    m_servedDepthFrame->disparityMap->resize(m_cvImageWidth * m_cvImageHeight);
    m_underConstructionDepthFrame->disparityMap->resize(m_cvImageWidth * m_cvImageHeight);
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
                distMap[rowStart + x] = (m_leftMap1.at<float>(y, x / 2) - x / 2) / m_cameraFrameWidth;
                distMap[rowStart + x + 1] = (m_leftMap2.at<float>(y, x / 2) - y) / m_cameraFrameHeight / 2;
            }

            rowStart = m_cameraFrameHeight * m_cameraTextureWidth * 2 + y * m_cameraTextureWidth * 2;

            for (uint32_t x = 0; x < m_cameraFrameWidth * 2; x += 2)
            {
                distMap[rowStart + x] = (m_rightMap1.at<float>(y, x / 2) - x / 2) / m_cameraFrameWidth;
                distMap[rowStart + x + 1] = (m_rightMap2.at<float>(y, x / 2) - y) / m_cameraFrameHeight / 2;
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

        Config_Main& mainConfig = m_configManager->GetConfig_Main();
        Config_Stereo& stereoConfig = m_configManager->GetConfig_Stereo();


        if (m_maxDisparity != stereoConfig.StereoMaxDisparity ||
            m_downscaleFactor != stereoConfig.StereoDownscaleFactor ||
            m_fovScale != mainConfig.FieldOfViewScale ||
            m_depthOffsetCalibration != mainConfig.DepthOffsetCalibration ||
            m_bUseColor != stereoConfig.StereoUseColor)
        {
            m_maxDisparity = stereoConfig.StereoMaxDisparity;
            m_downscaleFactor = stereoConfig.StereoDownscaleFactor;
            m_fovScale = mainConfig.FieldOfViewScale;
            m_depthOffsetCalibration = mainConfig.DepthOffsetCalibration;
            m_bUseColor = stereoConfig.StereoUseColor;

            InitReconstruction();
        }

        std::shared_ptr<CameraFrame> frame;

        if (mainConfig.ProjectionMode != Projection_StereoReconstruction || stereoConfig.StereoReconstructionFreeze || !m_cameraManager->GetCameraFrame(frame) || !frame->bHasFrameBuffer || frame->frameLayout == Mono)
        {
            continue;
        }

        if (m_bUseMulticore != stereoConfig.StereoUseMulticore)
        {
            m_bUseMulticore = stereoConfig.StereoUseMulticore;
            cv::setNumThreads(m_bUseMulticore ? -1: 0);
        }

        LARGE_INTEGER startReconstructionTime = StartPerfTimer();

        XrMatrix4x4f viewToWorld;

        {
            std::shared_lock readLock(frame->readWriteMutex);

            int frameSkipMod = stereoConfig.StereoFrameSkip + 1;

            if (frame->header.nFrameSequence == m_lastFrameSequence || frame->header.nFrameSequence % frameSkipMod != 0)
            {
                continue;
            }

            m_lastFrameSequence = frame->header.nFrameSequence;

            viewToWorld = ToXRMatrix4x4(frame->header.trackedDevicePose.mDeviceToAbsoluteTracking);

            int speckleRange = stereoConfig.StereoSGBM_SpeckleWindowSize > 0 ? stereoConfig.StereoSGBM_SpeckleRange : 0;

            if (stereoConfig.StereoAlgorithm == StereoAlgorithm_SGBM)
            {
                m_stereoSGBM = cv::StereoSGBM::create(stereoConfig.StereoMinDisparity, m_maxDisparity - stereoConfig.StereoMinDisparity, stereoConfig.StereoBlockSize,
                    stereoConfig.StereoSGBM_P1, stereoConfig.StereoSGBM_P2, stereoConfig.StereoSGBM_DispMaxDiff,
                    stereoConfig.StereoSGBM_PreFilterCap, stereoConfig.StereoSGBM_UniquenessRatio,
                    stereoConfig.StereoSGBM_SpeckleWindowSize, speckleRange,
                    (int)stereoConfig.StereoSGBM_Mode);
            }
            else if (stereoConfig.StereoAlgorithm == StereoAlgorithm_BM)
            {
                m_stereoBM = cv::StereoBM::create(m_maxDisparity - stereoConfig.StereoMinDisparity, stereoConfig.StereoBlockSize);
                m_stereoBM->setDisp12MaxDiff(stereoConfig.StereoSGBM_DispMaxDiff);
                m_stereoBM->setMinDisparity(stereoConfig.StereoMinDisparity);
                m_stereoBM->setPreFilterCap(stereoConfig.StereoSGBM_PreFilterCap);
                m_stereoBM->setSpeckleWindowSize(stereoConfig.StereoSGBM_SpeckleWindowSize);
                m_stereoBM->setSpeckleRange(speckleRange);
                m_stereoBM->setUniquenessRatio(stereoConfig.StereoSGBM_UniquenessRatio);
            }

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
            else
            {
                cv::cvtColor(m_inputFrame(frameROILeft), m_inputFrameLeft, cv::COLOR_RGBA2GRAY);
                cv::cvtColor(m_inputFrame(frameROIRight), m_inputFrameRight, cv::COLOR_RGBA2GRAY);
                
                // Uses B&W image in alpha channel of distorted frames, unsure if all headsets support this.
                //m_inputAlphaLeft = m_inputFrame(frameROILeft);
                //m_inputAlphaRight = m_inputFrame(frameROIRight);
                //int fromTo[2] = { 3, 0 };
                //cv::mixChannels(&m_inputAlphaLeft, 1, &m_inputFrameLeft, 1, fromTo, 1);
                //cv::mixChannels(&m_inputAlphaRight, 1, &m_inputFrameRight, 1, fromTo, 1);
            }
        }

        int filter = stereoConfig.StereoRectificationFiltering ? CV_INTER_LINEAR : CV_INTER_NN;

        cv::remap(m_inputFrameLeft, m_rectifiedFrameLeft, m_leftMap1, m_leftMap2, filter, cv::BORDER_CONSTANT);
        cv::remap(m_inputFrameRight, m_rectifiedFrameRight, m_rightMap1, m_rightMap2, filter, cv::BORDER_CONSTANT);
        
        cv::resize(m_rectifiedFrameLeft, m_scaledFrameLeft, cv::Size(m_cvImageWidth, m_cvImageHeight));
        cv::resize(m_rectifiedFrameRight, m_scaledFrameRight, cv::Size(m_cvImageWidth, m_cvImageHeight));      

        m_scaledFrameLeft.copyTo(m_scaledExtFrameLeft(cv::Rect(m_maxDisparity, 0, m_cvImageWidth, m_cvImageHeight)));
        m_scaledFrameRight.copyTo(m_scaledExtFrameRight(cv::Rect(m_maxDisparity, 0, m_cvImageWidth, m_cvImageHeight)));

        
        if (stereoConfig.StereoAlgorithm == StereoAlgorithm_SGBM)
        {
            m_stereoSGBM->compute(m_scaledExtFrameLeft, m_scaledExtFrameRight, m_rawDisparity);
        }
        else if (stereoConfig.StereoAlgorithm == StereoAlgorithm_BM)
        {
            m_stereoBM->compute(m_scaledExtFrameLeft, m_scaledExtFrameRight, m_rawDisparity);
        }
        

        cv::Mat* outputMatrix = &m_rawDisparity;

        if (stereoConfig.StereoFiltering != StereoFiltering_None)
        {
            m_rightMatcher = cv::ximgproc::createRightMatcher(m_stereoSGBM);
            m_rightMatcher->compute(m_scaledExtFrameRight, m_scaledExtFrameLeft, m_rightDisparity);

            m_wlsFilter = cv::ximgproc::createDisparityWLSFilter(m_stereoSGBM);

            m_wlsFilter->setLambda(stereoConfig.StereoWLS_Lambda);
            m_wlsFilter->setSigmaColor(stereoConfig.StereoWLS_Sigma);

            m_wlsFilter->filter(m_rawDisparity, m_scaledExtFrameLeft, m_filteredDisparity, m_rightDisparity, cv::Rect(), m_scaledExtFrameRight);

            if (stereoConfig.StereoFiltering == StereoFiltering_WLS_FBS)
            {
                m_confidence = m_wlsFilter->getConfidenceMap();

                cv::ximgproc::fastBilateralSolverFilter(m_scaledExtFrameLeft, m_filteredDisparity, m_confidence / 255.0f, m_bilateralDisparity, stereoConfig.StereoFBS_Spatial, stereoConfig.StereoFBS_Luma, stereoConfig.StereoFBS_Chroma, stereoConfig.StereoFBS_Lambda, stereoConfig.StereoFBS_Iterations);

                outputMatrix = &m_bilateralDisparity;
            }
            else
            {
                outputMatrix = &m_filteredDisparity;
            }
        }

        {
            std::unique_lock writeLock(m_underConstructionDepthFrame->readWriteMutex);

            m_disparityMatrix = cv::Mat(m_cvImageHeight, m_cvImageWidth, CV_16U, m_underConstructionDepthFrame->disparityMap->data());

            (*outputMatrix)(cv::Rect(m_maxDisparity, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(m_disparityMatrix, CV_16U);

            XrMatrix4x4f_Multiply(&m_underConstructionDepthFrame->disparityViewToWorldLeft, &viewToWorld, &m_rectifiedRotationLeft);
            //XrMatrix4x4f_Multiply(&m_underConstructionDepthFrame->disparityViewToWorldRight, &viewToWorld, &m_rectifiedRotationRight);
            m_underConstructionDepthFrame->disparityToDepth = m_disparityToDepth;
            m_underConstructionDepthFrame->disparityTextureSize[0] = m_cvImageWidth;
            m_underConstructionDepthFrame->disparityTextureSize[1] = m_cvImageHeight;
            m_underConstructionDepthFrame->disparityDownscaleFactor = (float)m_downscaleFactor;
            m_underConstructionDepthFrame->bIsValid = true;

            std::lock_guard<std::mutex> lock(m_serveMutex);

            m_underConstructionDepthFrame.swap(m_servedDepthFrame);
        }

        if(mainConfig.DebugTexture == DebugTexture_Disparity)
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
                cv::Mat debugTextureMat(m_cvImageHeight, m_cvImageWidth * 2, CV_16U, texture.Texture.data());

                cv::Mat left = debugTextureMat(cv::Rect(0, 0, m_cvImageWidth, m_cvImageHeight));
                cv::Mat right = debugTextureMat(cv::Rect(m_cvImageWidth, 0, m_cvImageWidth, m_cvImageHeight));

                (*outputMatrix)(cv::Rect(m_maxDisparity, 0, m_cvImageWidth, m_cvImageHeight)).convertTo(left, CV_16U);

                cv::Mat rightFlip;
                m_rightDisparity(cv::Rect(m_maxDisparity, 0, m_cvImageWidth, m_cvImageHeight)).copyTo(rightFlip);
                rightFlip *= -1;
                rightFlip.convertTo(right, CV_16U);

                debugTextureMat *= 50;

                if (texture.Width != m_cvImageWidth || texture.Height != m_cvImageHeight)
                {
                    texture.bDimensionsUpdated = true;
                }

                texture.Width = m_cvImageWidth * 2;
                texture.Height = m_cvImageHeight;
                texture.PixelSize = sizeof(uint16_t);
                texture.Format = DebugTextureFormat_R16U;
                texture.CurrentTexture = DebugTexture_Disparity;
            }
        }
        
        m_averageReconstructionTime = UpdateAveragePerfTime(m_reconstructionTimes, EndPerfTimer(startReconstructionTime), 20);
    }
}



