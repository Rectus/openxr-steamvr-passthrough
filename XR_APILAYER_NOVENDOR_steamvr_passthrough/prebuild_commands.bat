python framework\dispatch_generator.py

%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S vert -e main --vn g_FullscreenQuadVS shaders/fullscreen_quad_vs.hlsl  -o shaders\fullscreen_quad_vs.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S vert -e main --vn g_PassthroughVS shaders/passthrough_vs.hlsl  -o shaders\passthrough_vs.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S vert -e main --vn g_PassthroughStereoVS shaders/passthrough_stereo_vs.hlsl  -o shaders\passthrough_stereo_vs.spv.h

%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_AlphaPrepassPS shaders/alpha_prepass_ps.hlsl  -o shaders\alpha_prepass_ps.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_AlphaPrepassMaskedPS shaders/alpha_prepass_masked_ps.hlsl  -o shaders\alpha_prepass_masked_ps.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_AlphaCopyMaskedPS shaders/alpha_copy_masked_ps.hlsl  -o shaders\alpha_copy_masked_ps.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_PassthroughPS shaders/passthrough_ps.hlsl  -o shaders\passthrough_ps.spv.h

