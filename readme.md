OpenXR SteamVR Passthrough API Layer
---

This OpenXR API layer adds camera passthrough support to the SteamVR OpenXR runtime. It allows OpenXR applications that use the OpenXR passthrough feature to enable it when using the SteamVR runtime. 

The SteamVR runtime itself does not currently support any OpenXR passthrough features, but provides access to the camera video feeds and projection data through the proprietary OpenVR interface. This layer acts as a compositor in-between the application and runtime, retrieves the passthrough data from OpenVR, and renders it on the frames submitted by the application before passing them on to the runtime.

Please report any issues! Any comments and suggestions are also appreciated.

### DISCLAIMER ###
This is an experimental release. Please be careful when using the passthrough. 

This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risk!

Using the 3D stereo mode may induce heavy flickering on the display. Exercise caution if you are photosensitive.


### Features ###

- Supports both application selectable environment blend modes in the OpenXR core specification: Alpha Blended and Additive.
- Configuration menu available in the SteamVR dashboard.
- User adjustable color parameters and opacity.
- Override mode for applying passthrough to applications that do not support it. The passthrough view can be blended using chroma keying.
- The floor projection height can be shifted up to get correct projection on an horizontal surface such as a desk.
- EXPERIMENTAL: Supports 3D stereo reconstruction to estimate projection depth, using OpenCV. Includes support for Weighted Least Squares disparity filtering, and Fast Bilateral Solver filtering.
- Supports custom fisheye lens rectification instead of using the OpenVR pre-rectified output.
- Supports compositing the passthrough based on scene depth, for applications that supply depth buffers.
- EXPERIMENTAL: Support for USB camera input. This can be used alone or in conjunction with the depth provided by a stereoscopic HMD camera. The camera can either be attached to a tracked SteamVR device, or be set up in a static position. Manual calibration is required.


### Limitations ###

- Only the SteamVR runtime is supported.
- Only headsets that provide the passthrough camera feed to SteamVR are supported. If the SteamVR Room View does not work, this will not work.
- OpenVR applications are not supported.
- Only applications that use the core specification passthrough by submitting frames with `environmentBlendMode` set are supported.
- Applications using the Meta `XR_FB_passthrough` extension are not currently supported.
- The default passthrough view only supports a fixed depth reconstruction, while clamping the projection depth to the floor plane of the playspace. This works the same as the the SteamVR 2D Room View mode.
- The depth reconstruction from the 3D Room View is not supported. It is not currently accessible to developers. A custom depth reconstruction is used instead.
- The passthrough view has higher latency than the SteamVR compositor.
- OpenGL applications are not currently supported.
- The 3D stereo reconstruction mode is not supported with Vulkan.
- Depth blending requires the application to submit depth buffers using the `XR_KHR_composition_layer_depth` extension, which only a few do.

### Supported Headsets ###

- Valve Index - Supported
- HTC Vive - Untested, driver only provides correct pose data in 60 Hz mode.
- HTC Vive Pro - Untested
- HTC Vive Pro 2 - Untested
- Other HTC headsets - Unknown
- Varjo XR headsets - Unknown
- Windows Mixed Reality headsets - Unsupported, no passthrough support in driver
- Oculus/Meta headsets - Unsupported, no passthrough support in driver
- PSVR2 - Unsupported, no passthrough support in driver

The SteamVR settings UI will misconfigure the original HTC Vive camera if the frame rate is not set the right way. To correctly set it, click the right end of the slider instead of dragging it. The USB drivers may need to be reset if the camera is incorrectly configured.

The Valve Index camera poses may be poorly calibrated from the factory. The options menu allows adjusting the distance between cameras for better depth-perception when using the custom projection modes.


### Installation ###

1. Download and install the [Visual Studio C++ Redistributable (64 bit) ](https://aka.ms/vs/17/release/vc_redist.x64.exe)
2. Download the API layer release from the GitHub Releases page, and extract the files to the location you want to keep them in.
3. Run the `passthrough-setup.exe` utility, and select the Install option to install the API layer. Note that if you want to move the files, you will need to run the utility again.
4. If you want to disable or uninstall the API layer, run the `passthrough-setup.exe` utility and select the Uninstall option.


### Usage ###
Starting an OpenXR application will automatically start the API layer. If the application natively supports passthrough, the API layer will by default notify the application that additional environment blend modes are available. If the application does not support the OpenXR passthrough features, it is still possible to enable limited passthrough modes (see below).

While an application is running, the SteamVR dashboard will have an additional button in the bottom left, pressing it will open the settings overlay.

![Settings menu](https://github.com/Rectus/openxr-steamvr-passthrough/blob/main/settings_menu.png?raw=true)

The options under the OpenXR Core allow setting what passthrough modes are available for the application to use, as well as what mode it should prefer. Some applications may automatically switch to the preferred mode even though they don't support passthrough.

The options under Overrides allow forcing the passthrough mode regardless of whether the application has support for passthrough. Note that the Alpha Blend mode will show nothing unless the application submits alpha channels to the compositor.

The Additive mode will blend the passthrough on top of the view.

The Opaque mode will replace the application view with the passthrough.

The Masked mode allows setting a chroma key color that gets replaced with the passthrough view, as well as range for how similar colors get replaced, and a smoothness of the transition.

The settings can also be edited from `%APPDATA%\OpenXR SteamVR Passthrough\config.ini`

See the project [Wiki](https://github.com/Rectus/openxr-steamvr-passthrough/wiki) for more information.

### Building from source ###
The following are required:
- Visual Studio 2022 
- The MSVC build tools, and the Windows 10 SDK (installed via the Visual Studio Installer as "Desktop development with C++").
- Python 3 interpreter (installed via the Visual Studio Installer or externally available in your PATH).
- [OpenXR SDK Source](https://github.com/KhronosGroup/OpenXR-SDK-Source) (Included as Git submodule)
- [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) (Included as Git submodule)
- [OpenVR](https://github.com/ValveSoftware/openvr) (Included as Git submodule, the project is setup for static linking by default - requires custom source build)
- [LodePNG](https://github.com/lvandeve/lodepng) (Included as Git submodule)
- [SimpleINI](https://github.com/brofield/simpleini) (Included as Git submodule)
- [Dear ImGui](https://github.com/ocornut/imgui) (Included as Git submodule)
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (Uses the VULKAN_SDK environment variable)
- [OpenCV 4.10.0](https://github.com/opencv/opencv) (The project is setup for static linking by default - requires custom source build)
- [OpenCV-Contrib](https://github.com/opencv/opencv_contrib) (The ximgproc module needs to be built along with OpenCV for WLS and FBS filtering support.)

### Possible improvements ###

- Add partial support for the `XR_FB_passthrough` extension
- OpenGL support
- Add edge shader modes
- Hand depth projection + masking on 3D mode
- Improvements to 3D reconstruction
- `XR_HTC_passthrough` extension support (no headsets or applications use this yet)
- Linux support (does passthrough work on Linux?)
- Passthrough override support for OpenVR apps (better as independent application)



### Notes ###
Based on [OpenXR-Layer-Template](https://github.com/mbucchia/OpenXR-Layer-Template)

