
#pragma once


enum ERenderAPI
{
	RenderAPI_None,
	RenderAPI_Direct3D11,
	RenderAPI_Direct3D12,
	RenderAPI_Vulkan,
	RenderAPI_OpenGL
};

enum ERenderEye
{
	RenderEye_Left,
	RenderEye_Right
};

enum EPassthroughBlendMode
{
	Masked = 0,
	Opaque = 1,
	Additive = 2,
	AlphaBlendPremultiplied = 3,
	AlphaBlendUnpremultiplied = 4,
	AlphaTest = 5
};

enum ECameraProvider
{
	CameraProvider_None = -1,
	CameraProvider_OpenVR = 0,
	CameraProvider_OpenCV = 1,
	CameraProvider_Augmented = 2
};

enum EProjectionMode
{
	Projection_RoomView2D = 0,
	Projection_Custom2D = 1,
	Projection_StereoReconstruction = 2
};

enum EStereoFrameLayout
{
	FrameLayout_Mono = 0,
	FrameLayout_StereoVertical = 1, // Stereo frames are Bottom/Top (for left/right respectively)
	FrameLayout_StereoHorizontal = 2 // Stereo frames are Left/Right
};

enum EPassthroughCameraState
{
	CameraState_Uninitialized = 0,
	CameraState_Idle,
	CameraState_Waiting,
	CameraState_Active,
	CameraState_Error
};

// Matches vr::ECameraVideoStreamFormat
enum ECameraFrameFormat
{
	FrameFormat_Unknown = 0,
	FrameFormat_RAW10 = 1,     // 10 bpp, TODO
	FrameFormat_NV12 = 2,      // 12 bpp, planar YCbCr
	FrameFormat_RGB24 = 3,     // 24 bpp, R8G8B8
	FrameFormat_NV12_2 = 4,    // 12 bpp, planar YCbCr - image split into two sets of planes vertically (why?!)
	FrameFormat_YUYV16 = 5,	   // 16 bpp, packed YUV2 YCbCr
	FrameFormat_BAYER16BG = 6, // 16 bpp, 10-bit Bayer BG TODO
	FrameFormat_MJPEG = 7,     // Variable Motion JPEG encoding
	FrameFormat_RGBX32 = 8,	   // 32 bpp, R8G8B8X8
};


struct CameraDebugProperties
{
	vr::HmdVector2_t DistortedFocalLength;
	vr::HmdVector2_t UndistortedFocalLength;
	vr::HmdVector2_t MaximumUndistortedFocalLength;

	vr::HmdVector2_t DistortedOpticalCenter;
	vr::HmdVector2_t UndistortedOpticalCenter;
	vr::HmdVector2_t MaximumUndistortedOpticalCenter;

	vr::HmdMatrix44_t UndistortedProjecton;
	vr::HmdMatrix44_t MaximumUndistortedProjecton;

	vr::HmdMatrix34_t CameraToHeadTransform;
	vr::HmdVector4_t WhiteBalance;
	vr::EVRDistortionFunctionType DistortionFunction;
	double DistortionCoefficients[8];
};

struct DeviceDebugProperties
{
	vr::ETrackedDeviceClass DeviceClass;
	uint32_t DeviceId;
	std::string DeviceName;
	bool bHasCamera;
	uint32_t NumCameras;
	CameraDebugProperties CameraProps[4];

	uint32_t DistortedFrameHeight;
	uint32_t DistortedFrameWidth;
	uint32_t UndistortedFrameHeight;
	uint32_t UndistortedFrameWidth;
	uint32_t MaximumUndistortedFrameHeight;
	uint32_t MaximumUndistortedFrameWidth;

	vr::EVRTrackedCameraFrameLayout CameraFrameLayout;
	int32_t	CameraStreamFormat;
	vr::HmdMatrix34_t CameraToHeadTransform;
	uint64_t CameraFirmwareVersion;
	std::string CameraFirmwareDescription;
	int32_t CameraCompatibilityMode;
	bool bCameraSupportsCompatibilityModes;
	float CameraExposureTime;
	float CameraGlobalGain;

	uint64_t HMDFirmwareVersion;
	uint64_t FPGAFirmwareVersion;
	bool bHMDSupportsRoomViewDirect;
	bool bSupportsRoomViewDepthProjection;
	bool bAllowCameraToggle;
	bool bAllowLightSourceFrequency;
};

struct BlockQueueDebugProperties
{
	bool bInterfaceFound;
	bool bBlockQueueFound;
	int32_t Format;
	int32_t Width;
	int32_t Height;
	bool bFrameAvailable;
	int32_t FrameSize;
	uint64_t FrameSequence;
	double FrameTimeMonotonic;
	uint64_t ServerTimeTicks;
	double DeliveryRate;
	double ElapsedTime;
};

struct DeviceIdentProperties
{
	uint32_t DeviceId;
	std::string DeviceName;
	std::string DeviceSerial;
};

struct alignas(8) ClientDataValues
{
	uint32_t ApplicationVersion = 0;
	uint32_t EngineVersion = 0;
	uint64_t XRVersion = 0;
	uint32_t ApplicationPID = 0;
	bool bSessionActive = false;
	bool bDepthBlendingActive = false;
	ERenderAPI RenderAPI = RenderAPI_None;
	ERenderAPI AppRenderAPI = RenderAPI_None;
	
	int NumCompositionLayers = 0;
	bool bDepthLayerSubmitted = false;
	int FrameBufferWidth = 0;
	int FrameBufferHeight = 0;
	XrCompositionLayerFlags FrameBufferFlags = 0;
	int64_t FrameBufferFormat = 0;
	int64_t DepthBufferFormat = 0;
	float NearZ = 0.0f;
	float FarZ = 0.0f;

	float FrameToRenderLatencyMS = 0.0f;
	float FrameToPhotonsLatencyMS = 0.0f;
	float DepthToRenderLatencyMS = 0.0f;
	float DepthToPhotonsLatencyMS = 0.0f;
	float RenderTimeMS = 0.0f;
	float StereoReconstructionTimeMS = 0.0f;
	float StereoRenderTimeMS = 0.0f;
	float GPUFrameRetrievalTimeMS = 0.0f;
	float CPUFrameRetrievalTimeMS = 0.0f;
	uint64_t LastFrameTimestamp = 0;
	uint64_t LastCameraTimestamp = 0;

	bool bCorePassthroughActive = false;
	bool bFBPassthroughActive = false;
	bool bFBPassthroughDepthActive = false;
	int CoreCurrentMode = 0;

	bool bExtInvertedAlphaActive = false;
	bool bAndroidPassthroughStateActive = false;
	bool bFBPassthroughExtensionActive = false;
	bool bVarjoDepthEstimationExtensionActive = false;
	bool bVarjoDepthCompositionExtensionActive = false;

	uint32_t CameraFrameWidth = 0;
	uint32_t CameraFrameHeight = 0;
	float CameraFrameRate = 0.0f;
	ECameraProvider CameraProvider = CameraProvider_None;
	bool bCameraActive = false;
	EPassthroughCameraState CameraState = CameraState_Uninitialized;
};

struct ClientData
{
	std::string ApplicationModuleName;
	std::string ApplicationName;
	std::string EngineName;
	ClientDataValues Values = {};
	bool bTransientUpdatePending = false;
};

