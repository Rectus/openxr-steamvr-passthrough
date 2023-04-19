python framework\dispatch_generator.py

%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S vert -e main --vn g_FullscreenQuadShaderVS shaders/fullscreen_quad_vs.hlsl  -o shaders\fullscreen_quad_vs.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S vert -e main --vn g_PassthroughShaderVS shaders/passthrough_vs.hlsl  -o shaders\passthrough_vs.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S vert -e main --vn g_PassthroughStereoShaderVS shaders/passthrough_stereo_vs.hlsl  -o shaders\passthrough_stereo_vs.spv.h

%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_AlphaPrepassShaderPS shaders/alpha_prepass_ps.hlsl  -o shaders\alpha_prepass_ps.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_AlphaPrepassMaskedShaderPS shaders/alpha_prepass_masked_ps.hlsl  -o shaders\alpha_prepass_masked_ps.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_AlphaCopyMaskedShaderPS shaders/alpha_copy_masked_ps.hlsl  -o shaders\alpha_copy_masked_ps.spv.h
%VULKAN_SDK%\Bin\glslangValidator.exe -D -V -S frag -e main --vn g_PassthroughShaderPS shaders/passthrough_ps.hlsl  -o shaders\passthrough_ps.spv.h

