


#version 450

#define FrameFormat_Unknown 0
#define FrameFormat_RAW10 1
#define FrameFormat_NV12 2
#define FrameFormat_RGB24 3
#define FrameFormat_NV12_2 4
#define FrameFormat_YUYV16 5
#define FrameFormat_BAYER16BG 6
#define FrameFormat_MJPEG 7
#define FrameFormat_RGBX32 8



layout(push_constant, std140 ) uniform DecodeConstants
{
    uint frameFormat;
    float brightness;
    float contrast;
    float saturation;
    float gammaCorrection;
    bool bDoColorAdjustment;

} g_pass;

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

    linearRGB.r = sRGB.r <= 0.0031308 ? sRGB.r / 12.92 : pow((sRGB.r + 0.055) / 1.055, 2.4);
    linearRGB.g = sRGB.g <= 0.0031308 ? sRGB.g / 12.92 : pow((sRGB.g + 0.055) / 1.055, 2.4);
    linearRGB.b = sRGB.b <= 0.0031308 ? sRGB.b / 12.92 : pow((sRGB.b + 0.055) / 1.055, 2.4);

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
    linearRGB = pow(abs(linearRGB), vec3(g_pass.gammaCorrection));
        
	// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
	vec3 labColor = LinearRGBtoLAB_D65(linearRGB);

	float LPrime = clamp((labColor.x - 50.0) * g_pass.contrast + 50.0, 0.0, 100.0);
	float LBis = clamp(LPrime + g_pass.brightness, 0.0, 100.0);
	vec2 ab = labColor.yz * g_pass.saturation;

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
    
    // Conversion from ITU-T BT.709 gamma to sRGB
    vec3 linearRGB;
    linearRGB.r = rgbGamma.r < 0.0812 ? rgbGamma.r / 4.5 : pow((rgbGamma.r + 0.099) / 1.099, 1 / 0.45);
    linearRGB.g = rgbGamma.g < 0.0812 ? rgbGamma.g / 4.5 : pow((rgbGamma.g + 0.099) / 1.099, 1 / 0.45);
    linearRGB.b = rgbGamma.b < 0.0812 ? rgbGamma.b / 4.5 : pow((rgbGamma.b + 0.099) / 1.099, 1 / 0.45);

    return linearRGB;
}



layout( local_size_x = 32, local_size_y = 32, local_size_z = 1 ) in;
void main()
{
    ivec2 inSize = textureSize(g_rawFrame, 0);

    if(g_pass.frameFormat == FrameFormat_YUYV16)
    {
        if(any(greaterThanEqual(gl_GlobalInvocationID.xy, inSize)))
        {
            return;
        }

        ivec2 outPos = ivec2(gl_GlobalInvocationID.x * 2, gl_GlobalInvocationID.y);
        ivec2 inPos = ivec2(gl_GlobalInvocationID.xy);

        vec4 inputYUYV = texelFetch(g_rawFrame, inPos, 0);
        vec4 leftYUYV = texelFetch(g_rawFrame, clamp(inPos + ivec2(-1, 0), ivec2(0, 0), ivec2(inSize)) , 0);
        vec4 rightYUYV = texelFetch(g_rawFrame, clamp(inPos + ivec2(1, 0), ivec2(0, 0), ivec2(inSize)), 0);

        //Midpoint chroma sampling. (The official implemetnation seems to use the packed chroma directly.)
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

        if (g_pass.bDoColorAdjustment)
        {
            ApplyColorAdjustment(leftRGB);
            ApplyColorAdjustment(rightRGB);
        }

        vec3 leftSRGB = LinearToSRGB(leftRGB);
        vec3 rightSRGB = LinearToSRGB(rightRGB);

        imageStore(g_outputFrame, outPos, vec4(leftSRGB, 1));
        imageStore(g_outputFrame, outPos + ivec2(1, 0), vec4(rightSRGB, 1));
    }
    else if(g_pass.frameFormat == FrameFormat_RGBX32)
    {
        if(any(greaterThanEqual(gl_GlobalInvocationID.xy, inSize)))
        {
            return;
        }

        ivec2 inPos = ivec2(gl_GlobalInvocationID.xy);

        vec3 RGB = texelFetch(g_rawFrame, inPos, 0).rgb;

        if (g_pass.bDoColorAdjustment)
        {
            ApplyColorAdjustment(RGB);
        }

        vec3 SRGB = LinearToSRGB(RGB);

        imageStore(g_outputFrame, inPos, vec4(SRGB, 1));

    }
    else
    {
        imageStore(g_outputFrame, ivec2(gl_GlobalInvocationID.xy), vec4(1, 0, 1, 1));
    }

}