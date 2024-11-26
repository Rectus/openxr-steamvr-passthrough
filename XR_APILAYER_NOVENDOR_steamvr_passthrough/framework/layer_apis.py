# The list of OpenXR functions our layer will override.
override_functions = [
    "xrGetSystem",
    "xrGetVulkanDeviceExtensionsKHR",
    "xrCreateVulkanDeviceKHR",
    "xrCreateSession",
    "xrDestroySession",
    "xrEnumerateEnvironmentBlendModes",
    "xrCreateReferenceSpace",
    "xrDestroySpace",
    "xrCreateSwapchain",
    "xrDestroySwapchain",
	"xrAcquireSwapchainImage",
    "xrWaitSwapchainImage",
	"xrReleaseSwapchainImage",
    "xrBeginFrame",
    "xrEndFrame",
    "xrSetEnvironmentDepthEstimationVARJO"
]

# The list of OpenXR functions our layer will use from the runtime.
# Might repeat entries from override_functions above.
requested_functions = [
    "xrGetInstanceProperties",
    "xrGetSystemProperties",
    "xrEnumerateSwapchainFormats",
    "xrEnumerateViewConfigurationViews",
    "xrLocateViews",
    "xrLocateSpace",
    "xrCreateReferenceSpace",
    "xrDestroySpace",
    "xrCreateSwapchain",
    "xrDestroySwapchain",
    "xrEnumerateSwapchainImages",
    "xrAcquireSwapchainImage",
    "xrWaitSwapchainImage",
    "xrReleaseSwapchainImage",
    "xrBeginFrame",
    "xrEndFrame",
    "xrConvertTimeToWin32PerformanceCounterKHR"
]

# The list of OpenXR extensions our layer will either override or use.
extensions = [
    "XR_KHR_vulkan_enable",
    "XR_KHR_vulkan_enable2",
    "XR_KHR_win32_convert_performance_counter_time",
    "XR_VARJO_composition_layer_depth_test",
    "XR_VARJO_environment_depth_estimation"
]
