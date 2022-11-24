# The list of OpenXR functions our layer will override.
override_functions = [
    "xrGetSystem",
    "xrCreateSession",
    "xrDestroySession",
    "xrEnumerateEnvironmentBlendModes",
    "xrCreateReferenceSpace",
    "xrDestroySpace",
    "xrCreateSwapchain",
    "xrDestroySwapchain",
	"xrAcquireSwapchainImage",
	"xrReleaseSwapchainImage",
    "xrBeginFrame",
    "xrEndFrame"
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
    "xrEndFrame"
]

# The list of OpenXR extensions our layer will either override or use.
extensions = []
