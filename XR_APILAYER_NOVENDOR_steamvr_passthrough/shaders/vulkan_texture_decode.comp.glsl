

#version 450

#ifndef VULKAN // To make the VS GLSL extension shut up
#extension GL_KHR_vulkan_glsl: enable
#endif

#define FrameFormat_Unknown 0
#define FrameFormat_RAW10 1
#define FrameFormat_NV12 2
#define FrameFormat_RGB24 3
#define FrameFormat_NV12_2 4
#define FrameFormat_YUYV16 5
#define FrameFormat_BAYER16BG 6
#define FrameFormat_MJPEG 7
#define FrameFormat_RGBX32 8


layout(constant_id = 0) const uint g_frameFormat = 0;
layout(constant_id = 1) const bool g_bDoColorAdjustment = false;
layout(constant_id = 2) const bool g_bInputIsSRGB = false;
layout(constant_id = 3) const bool g_bOutputIsSRGB = false;


layout(push_constant, std140 ) uniform DecodeConstants
{
    float brightness;
    float contrast;
    float saturation;
    float gammaCorrection;

} g_push;

layout(binding = 0) uniform sampler2D g_rawFrame;
layout(binding = 1) uniform writeonly image2D g_outputFrame;


#define RGBtoXYZMat mat3x3( \
    0.4124564, 0.3575761, 0.1804375,\
    0.2126729, 0.7151522, 0.0721750,\
    0.0193339, 0.1191920, 0.9503041)\

#define XYZtoRGBMat mat3x3( \
     3.2404542, -1.5371385, -0.4985314,\
    -0.9692660,  1.8760108,  0.0415560,\
     0.0556434, -0.2040259,  1.0572252)\

// D65 reference values predivided by 100.
#define D65Ref vec3(0.95047, 1.00000, 1.08883)

vec3 LinearRGBtoLAB_D65(in vec3 rgb)
{
    vec3 xyz = abs(rgb * RGBtoXYZMat / D65Ref);

    xyz.x = (xyz.x > 0.008856) ? pow(xyz.x, 1.0 / 3.0) : (7.787 * xyz.x) + (16.0 / 116.0);
    xyz.y = (xyz.y > 0.008856) ? pow(xyz.y, 1.0 / 3.0) : (7.787 * xyz.y) + (16.0 / 116.0);
    xyz.z = (xyz.z > 0.008856) ? pow(xyz.z, 1.0 / 3.0) : (7.787 * xyz.z) + (16.0 / 116.0);

    return vec3(
        (116.0 * xyz.y) - 16.0,
        500.0 * (xyz.x - xyz.y),
        200.0 * (xyz.y - xyz.z)
    );
}

vec3 LABtoLinearRGB_D65(in vec3 lab)
{
    vec3 xyz;

    xyz.y = (lab.x + 16.0) / 116.0;
    xyz.x = lab.y / 500.0 + xyz.y;
    xyz.z = xyz.y - lab.z / 200.0;

    xyz = abs(xyz);
    
    xyz.x = ((xyz.x > 0.206897) ? pow(xyz.x, 3.0) : (xyz.x - 16.0 / 116.0) / 7.787);
    xyz.y = ((xyz.y > 0.206897) ? pow(xyz.y, 3.0) : (xyz.y - 16.0 / 116.0) / 7.787);
    xyz.z = ((xyz.z > 0.206897) ? pow(xyz.z, 3.0) : (xyz.z - 16.0 / 116.0) / 7.787);

    xyz *= D65Ref;

    return xyz * XYZtoRGBMat;
}

vec3 SRGBToLinear(in vec3 sRGB)
{
    vec3 linearRGB;

    linearRGB.r = sRGB.r <= 0.04045 ? sRGB.r / 12.92 : pow((sRGB.r + 0.055) / 1.055, 2.4);
    linearRGB.g = sRGB.g <= 0.04045 ? sRGB.g / 12.92 : pow((sRGB.g + 0.055) / 1.055, 2.4);
    linearRGB.b = sRGB.b <= 0.04045 ? sRGB.b / 12.92 : pow((sRGB.b + 0.055) / 1.055, 2.4);

    return linearRGB;
}

vec3 LinearToSRGB(in vec3 linearRGB)
{
    vec3 sRGB;

    sRGB.r = linearRGB.r <= 0.0031308 ? linearRGB.r * 12.92 : pow(linearRGB.r, 1 / 2.4) * 1.055 - 0.055;
    sRGB.g = linearRGB.g <= 0.0031308 ? linearRGB.g * 12.92 : pow(linearRGB.g, 1 / 2.4) * 1.055 - 0.055;
    sRGB.b = linearRGB.b <= 0.0031308 ? linearRGB.b * 12.92 : pow(linearRGB.b, 1 / 2.4) * 1.055 - 0.055;

    return sRGB;
}


void ApplyColorAdjustment(inout vec3 linearRGB)
{
    linearRGB = pow(abs(linearRGB), vec3(g_push.gammaCorrection));
        
	// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
	vec3 labColor = LinearRGBtoLAB_D65(linearRGB);

	float LPrime = clamp((labColor.x - 50.0) * g_push.contrast + 50.0, 0.0, 100.0);
	float LBis = clamp(LPrime + g_push.brightness, 0.0, 100.0);
	vec2 ab = labColor.yz * g_push.saturation;

	linearRGB = LABtoLinearRGB_D65(vec3(LBis, ab.xy));
}


// Converting YUV to sRGB with the models specified by the Valve Index UVC profile
vec3 YUVToRGB(in vec3 inYUV)
{
    // Decoding narrow "TV" range according to ITU-T BT.709.
    vec3 yuvDecoded = (vec3(inYUV.x - (16.0 / 256.0), inYUV.y - 0.5, inYUV.z - 0.5) * 
        vec3(256.0 / 219.0, 256.0 / 224.0, 256.0 / 224.0));

    // Using ITU-T BT.601 (SMPTE 170M) YCrCb factors.
    mat3x3 conversion = mat3x3(
        1.0,  0.000,           1.402,
	    1.0, -0.202008/0.587, -0.419198/0.587,
	    1.0,                   1.772, 0.000
    );
    vec3 rgbGamma = yuvDecoded * conversion;

    vec3 linearRGB;

    if(g_bInputIsSRGB)
    {
        linearRGB = SRGBToLinear(rgbGamma);
    }
    else
    {
        // Conversion from ITU-T BT.709 gamma to sRGB
        linearRGB.r = rgbGamma.r < 0.0812 ? rgbGamma.r / 4.5 : pow((rgbGamma.r + 0.099) / 1.099, 1 / 0.45);
        linearRGB.g = rgbGamma.g < 0.0812 ? rgbGamma.g / 4.5 : pow((rgbGamma.g + 0.099) / 1.099, 1 / 0.45);
        linearRGB.b = rgbGamma.b < 0.0812 ? rgbGamma.b / 4.5 : pow((rgbGamma.b + 0.099) / 1.099, 1 / 0.45);
    }
    return linearRGB;
}



layout( local_size_x = 32, local_size_y = 32, local_size_z = 1 ) in;
void main()
{
    ivec2 inSize = textureSize(g_rawFrame, 0);

    if(any(greaterThanEqual(gl_GlobalInvocationID.xy, inSize)))
    {
        return;
    }

    if(g_frameFormat == FrameFormat_YUYV16)
    {
        ivec2 outPos = ivec2(gl_GlobalInvocationID.x * 2, gl_GlobalInvocationID.y);
        ivec2 inPos = ivec2(gl_GlobalInvocationID.xy);

        vec4 inputYUYV = texelFetch(g_rawFrame, inPos, 0);
        vec4 leftYUYV = texelFetch(g_rawFrame, clamp(inPos + ivec2(-1, 0), ivec2(0, 0), ivec2(inSize)) , 0);
        vec4 rightYUYV = texelFetch(g_rawFrame, clamp(inPos + ivec2(1, 0), ivec2(0, 0), ivec2(inSize)), 0);

        //Midpoint chroma sampling.
        vec3 leftYUV;
        leftYUV.x = inputYUYV.x;
        leftYUV.y = inputYUYV.y * 0.75 + leftYUYV.y * 0.25;
        leftYUV.z = inputYUYV.w * 0.75 + leftYUYV.w * 0.25;

        vec3 rightYUV;
        rightYUV.x = inputYUYV.z;
        rightYUV.y = inputYUYV.y * 0.75 + rightYUYV.y * 0.25;
        rightYUV.z = inputYUYV.w * 0.75 + rightYUYV.w * 0.25;

        vec3 leftRGB = YUVToRGB(leftYUV);
        vec3 rightRGB = YUVToRGB(rightYUV);

        if (g_bDoColorAdjustment)
        {
            ApplyColorAdjustment(leftRGB);
            ApplyColorAdjustment(rightRGB);
        }

        if(g_bOutputIsSRGB)
        {
            leftRGB = LinearToSRGB(leftRGB);
            rightRGB = LinearToSRGB(rightRGB);
        }

        imageStore(g_outputFrame, outPos, vec4(leftRGB, 1));
        imageStore(g_outputFrame, outPos + ivec2(1, 0), vec4(rightRGB, 1));
    }
    else if(g_frameFormat == FrameFormat_NV12 || g_frameFormat == FrameFormat_NV12_2)
    {
        ivec2 outSize = imageSize(g_outputFrame);

        ivec2 outPos = ivec2(gl_GlobalInvocationID.xy);

        ivec2 inPosLuma;
        ivec2 inPosCb;

        // NV12_2 has two sets of image planes stacked vertically, 
        // mapping to the top and bottom halves of the image.
        if(g_frameFormat == FrameFormat_NV12_2)
        {
            if(outPos.y < outSize.y / 2) // Top CbCr half-plane after luma half-plane
            {
                inPosLuma = ivec2(outPos);
                inPosCb = ivec2(outPos.x - mod(outPos.x, 2), outSize.y / 2 + outPos.y / 2);
            }
            else // Bottom luma and CbCr half-planes
            {
                inPosLuma = ivec2(outPos.x, outPos.y + inSize.y / 2);
                inPosCb = ivec2(outPos.x - mod(outPos.x, 2), inSize.y / 2 + outSize.y / 2 + outPos.y / 2);
            }
        }
        else //Regular NV12 has one luma plane with the same dimensions as the output textures,
             // with a half-height CbCr plane below it.
        {
            inPosLuma = ivec2(outPos);
            inPosCb = ivec2(outPos.x - mod(outPos.x, 2), outSize.y + outPos.y / 2);
        }

        // Chroma neighbor sample offsets.
        ivec2 offsetH = ivec2(mod(outPos.x, 2) * 2 - 1, 0);
        ivec2 offsetV = ivec2(0, mod(outPos.y, 2) * 2 - 1);

        ivec2 inPosCbH = clamp(inPosCb + offsetH, ivec2(0, 0), ivec2(inSize));
        ivec2 inPosCbV = clamp(inPosCb + offsetV, ivec2(0, 0), ivec2(inSize));
        ivec2 inPosCbHV = clamp(inPosCb + offsetH + offsetV, ivec2(0, 0), ivec2(inSize));

        ivec2 inPosCr = inPosCb + ivec2(1, 0);
        ivec2 inPosCrH = clamp(inPosCr + offsetH, ivec2(0, 0), ivec2(inSize));
        ivec2 inPosCrV = clamp(inPosCr + offsetV, ivec2(0, 0), ivec2(inSize));
        ivec2 inPosCrHV = clamp(inPosCr + offsetH + offsetV, ivec2(0, 0), ivec2(inSize));

        float inputLuma = texelFetch(g_rawFrame, inPosLuma, 0).x;

        // Bilinear chroma sampling has to be done manually since the pixels are interleaved.
        // Separating the planes into two textures would allow automatic sampling.
        float inputCb = texelFetch(g_rawFrame, inPosCb, 0).x;
        float inputCbH = texelFetch(g_rawFrame, inPosCbH, 0).x;
        float inputCbV = texelFetch(g_rawFrame, inPosCbV, 0).x;
        float inputCbHV = texelFetch(g_rawFrame, inPosCbHV, 0).x;

        float inputCr = texelFetch(g_rawFrame, inPosCr, 0).x;
        float inputCrH = texelFetch(g_rawFrame, inPosCrH, 0).x;
        float inputCrV = texelFetch(g_rawFrame, inPosCrV, 0).x;
        float inputCrHV = texelFetch(g_rawFrame, inPosCrHV, 0).x;

        //Midpoint chroma sampling.
        vec3 colorYCbCr;
        colorYCbCr.x = inputLuma;
        colorYCbCr.y = mix(inputCb * 0.75 + inputCbH * 0.25, inputCbV * 0.75 + inputCbHV * 0.25, 0.25);
        colorYCbCr.z = mix(inputCr * 0.75 + inputCrH * 0.25, inputCrV * 0.75 + inputCrHV * 0.25, 0.25);

        vec3 colorRGB = YUVToRGB(colorYCbCr);


        if (g_bDoColorAdjustment)
        {
            ApplyColorAdjustment(colorRGB);
        }

        if(g_bOutputIsSRGB)
        {
            colorRGB = LinearToSRGB(colorRGB);
        }

        imageStore(g_outputFrame, outPos, vec4(colorRGB, 1));
    }
    else // RGB textures.
    {
        ivec2 inPos = ivec2(gl_GlobalInvocationID.xy);

        vec3 RGB = texelFetch(g_rawFrame, inPos, 0).rgb;

        if(g_bInputIsSRGB)
        {
            RGB = LinearToSRGB(RGB);
        }

        if (g_bDoColorAdjustment)
        {
            ApplyColorAdjustment(RGB);
        }

        if(g_bOutputIsSRGB)
        {
            RGB = LinearToSRGB(RGB);
        }
        imageStore(g_outputFrame, inPos, vec4(RGB, 1));

    }
}