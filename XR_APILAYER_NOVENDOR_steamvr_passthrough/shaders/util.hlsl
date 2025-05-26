
#ifndef _UTIL_INCLUDED
#define _UTIL_INCLUDED

static const float PI = 3.1415926535897932384626433f;


inline float Remap(float value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

inline float2 Remap(float2 value, float2 inMin, float2 inMax, float2 outMin, float2 outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

inline float3 Remap(float3 value, float3 inMin, float3 inMax, float3 outMin, float3 outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

inline float4 Remap(float4 value, float4 inMin, float4 inMax, float4 outMin, float4 outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

inline float2 Remap(float2 value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

inline float3 Remap(float3 value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

inline float4 Remap(float4 value, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}


inline float LoadTextureNearestClamped(in Texture2D<float> tex, in float2 uv)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    return tex.Load(int3(floor(saturate(uv) * float2(texW, texH)), 0));
}

inline float2 LoadTextureNearestClamped(in Texture2D<float2> tex, in float2 uv)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    return tex.Load(int3(floor(saturate(uv) * float2(texW, texH)), 0));
}

inline float3 LoadTextureNearestClamped(in Texture2D<float3> tex, in float2 uv)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    return tex.Load(int3(floor(saturate(uv) * float2(texW, texH)), 0));
}

inline float4 LoadTextureNearestClamped(in Texture2D<float4> tex, in float2 uv)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    return tex.Load(int3(floor(saturate(uv) * float2(texW, texH)), 0));
}


#define RGBtoXYZMat float3x3( \
    0.4124564, 0.3575761, 0.1804375,\
    0.2126729, 0.7151522, 0.0721750,\
    0.0193339, 0.1191920, 0.9503041)\

#define XYZtoRGBMat float3x3( \
     3.2404542, -1.5371385, -0.4985314,\
    -0.9692660,  1.8760108,  0.0415560,\
     0.0556434, -0.2040259,  1.0572252)\

// D65 reference values predivided by 100.
#define D65Ref float3(0.95047, 1.00000, 1.08883)


float3 sRGBtoLAB_D65(in float3 rgb)
{
    rgb.r = (rgb.r > 0.04045) ? pow((rgb.r + 0.055) / 1.055, 2.4) : rgb.r / 12.92;
    rgb.g = (rgb.g > 0.04045) ? pow((rgb.g + 0.055) / 1.055, 2.4) : rgb.g / 12.92;
    rgb.b = (rgb.b > 0.04045) ? pow((rgb.b + 0.055) / 1.055, 2.4) : rgb.b / 12.92;

    float3 xyz = abs(mul(RGBtoXYZMat, rgb) / D65Ref);

    xyz.x = (xyz.x > 0.008856) ? pow(xyz.x, 1.0 / 3.0) : (7.787 * xyz.x) + (16.0 / 116.0);
    xyz.y = (xyz.y > 0.008856) ? pow(xyz.y, 1.0 / 3.0) : (7.787 * xyz.y) + (16.0 / 116.0);
    xyz.z = (xyz.z > 0.008856) ? pow(xyz.z, 1.0 / 3.0) : (7.787 * xyz.z) + (16.0 / 116.0);

    return float3(
        (116.0 * xyz.y) - 16.0,
        500.0 * (xyz.x - xyz.y),
        200.0 * (xyz.y - xyz.z)
        );
}


float3 LABtosRGB_D65(in float3 lab)
{
    float3 xyz;

    xyz.y = (lab.x + 16.0) / 116.0;
    xyz.x = lab.y / 500.0 + xyz.y;
    xyz.z = xyz.y - lab.z / 200.0;
    
    xyz = abs(xyz);

    xyz.x = ((xyz.x > 0.206897) ? pow(xyz.x, 3.0) : (xyz.x - 16.0 / 116.0) / 7.787);
    xyz.y = ((xyz.y > 0.206897) ? pow(xyz.y, 3.0) : (xyz.y - 16.0 / 116.0) / 7.787);
    xyz.z = ((xyz.z > 0.206897) ? pow(xyz.z, 3.0) : (xyz.z - 16.0 / 116.0) / 7.787);

    xyz *= D65Ref;

    float3 rgb = mul(XYZtoRGBMat, xyz);

    rgb.r = (rgb.r > 0.0031308) ? 1.055 * pow(rgb.r, (1.0 / 2.4)) - 0.055 : rgb.r * 12.92;
    rgb.g = (rgb.g > 0.0031308) ? 1.055 * pow(rgb.g, (1.0 / 2.4)) - 0.055 : rgb.g * 12.92;
    rgb.b = (rgb.b > 0.0031308) ? 1.055 * pow(rgb.b, (1.0 / 2.4)) - 0.055 : rgb.b * 12.92;

    return rgb;
}



float3 LinearRGBtoLAB_D65(in float3 rgb)
{
    float3 xyz = abs(mul(RGBtoXYZMat, rgb) / D65Ref);

    xyz.x = (xyz.x > 0.008856) ? pow(xyz.x, 1.0 / 3.0) : (7.787 * xyz.x) + (16.0 / 116.0);
    xyz.y = (xyz.y > 0.008856) ? pow(xyz.y, 1.0 / 3.0) : (7.787 * xyz.y) + (16.0 / 116.0);
    xyz.z = (xyz.z > 0.008856) ? pow(xyz.z, 1.0 / 3.0) : (7.787 * xyz.z) + (16.0 / 116.0);

    return float3(
        (116.0 * xyz.y) - 16.0,
        500.0 * (xyz.x - xyz.y),
        200.0 * (xyz.y - xyz.z)
    );
}


float3 LABtoLinearRGB_D65(in float3 lab)
{
    float3 xyz;

    xyz.y = (lab.x + 16.0) / 116.0;
    xyz.x = lab.y / 500.0 + xyz.y;
    xyz.z = xyz.y - lab.z / 200.0;

    xyz = abs(xyz);
    
    xyz.x = ((xyz.x > 0.206897) ? pow(xyz.x, 3.0) : (xyz.x - 16.0 / 116.0) / 7.787);
    xyz.y = ((xyz.y > 0.206897) ? pow(xyz.y, 3.0) : (xyz.y - 16.0 / 116.0) / 7.787);
    xyz.z = ((xyz.z > 0.206897) ? pow(xyz.z, 3.0) : (xyz.z - 16.0 / 116.0) / 7.787);

    xyz *= D65Ref;

    return mul(XYZtoRGBMat, xyz);
}





float sinc(float x)
{
    return sin(x * PI) / (x * PI);
}

float lanczosWeight(float distance, float n)
{
    return (distance == 0) ? 1 : (distance * distance < n * n ? sinc(distance) * sinc(distance / n) : 0);
}

float2 lanczos2(in Texture2D<float2> tex, float2 uvs, float2 res)
{
    float2 center = uvs - (((uvs * res) % 1) - 0.5) / res;
    float2 offset = (uvs - center) * res;
    
    float2 output = 0;
    float totalWeight = 0;
    
    for (int y = -2; y < 2; y++)
    {
        for (int x = -2; x < 2; x++)
        {
            float weight = lanczosWeight(x - offset.x, 2) * lanczosWeight(y - offset.y, 2);
            
            output += tex.Load(int3(floor(uvs * res) + int2(x, y), 0)) * weight;
            totalWeight += weight;
        }
    }
    
    return output / totalWeight;
}

float4 lanczos2(in Texture2D<half4> tex, float2 uv, float2 res)
{
    float2 center = uv - (((uv * res) % 1) - 0.5) / res;
    float2 offset = (uv - center) * res;
    
    float4 output = 0;
    float totalWeight = 0;
    
    for (int y = -2; y < 2; y++)
    {
        for (int x = -2; x < 2; x++)
        {
            float weight = lanczosWeight(x - offset.x, 2) * lanczosWeight(y - offset.y, 2);
            
            output += tex.Load(int3(floor(uv * res) + int2(x, y), 0)) * weight;
            //output += tex.SampleLevel(g_samplerState, center + float2(x, y) / res, 0) * weight;
            totalWeight += weight;
        }
    }
    
    return output / totalWeight;
}





// B-spline as in http://vec3.ca/bicubic-filtering-in-fewer-taps/
float2 bicubic_b_spline_4tap(in Texture2D<float2> tex, in SamplerState linearSampler, in float2 uv)
{
    uint texW, texH;
    tex.GetDimensions(texW, texH);
    float2 texSize = float2(texW, texH);
    
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    float2 f = samplePos - texPos1;
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    
    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1.0 - w0 - w1 - w3;
 
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);
    
    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
 
    float2 f0 = w1 / (w0 + w1);
    float2 f1 = w3 / (w2 + w3);
 
    float2 t0 = (texPos1 - 1 + f0) / texSize;
    float2 t1 = (texPos1 + 1 + f1) / texSize;

    float2 result = 0;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t0.y), 0) * s0.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t0.y), 0) * s1.x * s0.y;
    result += tex.SampleLevel(linearSampler, float2(t0.x, t1.y), 0) * s0.x * s1.y;
    result += tex.SampleLevel(linearSampler, float2(t1.x, t1.y), 0) * s1.x * s1.y;

    return result;
}


// Based on the code in https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1 and http://vec3.ca/bicubic-filtering-in-fewer-taps/
float4 catmull_rom_9tap(in Texture2D<half4> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0) * w0.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0) * w3.x * w0.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0) * w0.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0) * w3.x * w3.y;

    return result;
}







// Based on the code in https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1 and http://vec3.ca/bicubic-filtering-in-fewer-taps/
float2 catmull_rom_9tap(in Texture2D<float2> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float2 result = 0;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0) * w0.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0) * w3.x * w0.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0) * w0.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0) * w3.x * w3.y;

    return result;
}



// B-spline as in http://vec3.ca/bicubic-filtering-in-fewer-taps/
float bicubic_b_spline_4tap(in Texture2D<float> tex, in SamplerState bilinearSampler, in float2 uv, in float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    float2 f = samplePos - texPos1;
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    
    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1.0 - w0 - w1 - w3;
 
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);
    
    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
 
    float2 f0 = w1 / (w0 + w1);
    float2 f1 = w3 / (w2 + w3);
 
    float2 t0 = (texPos1 - 1 + f0) / texSize;
    float2 t1 = (texPos1 + 1 + f1) / texSize;

    float result = 0;
    result += tex.SampleLevel(bilinearSampler, float2(t0.x, t0.y), 0) * s0.x * s0.y;
    result += tex.SampleLevel(bilinearSampler, float2(t1.x, t0.y), 0) * s1.x * s0.y;
    result += tex.SampleLevel(bilinearSampler, float2(t0.x, t1.y), 0) * s0.x * s1.y;
    result += tex.SampleLevel(bilinearSampler, float2(t1.x, t1.y), 0) * s1.x * s1.y;

    return result;
}


// B-spline as in http://vec3.ca/bicubic-filtering-in-fewer-taps/
float4 bicubic_b_spline_4tap(in Texture2D<half4> tex, in SamplerState bilinearSampler, in float2 uv, in float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    float2 f = samplePos - texPos1;
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    
    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1.0 - w0 - w1 - w3;
 
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);
    
    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
 
    float2 f0 = w1 / (w0 + w1);
    float2 f1 = w3 / (w2 + w3);
 
    float2 t0 = (texPos1 - 1 + f0) / texSize;
    float2 t1 = (texPos1 + 1 + f1) / texSize;

    float4 result = 0;
    result += tex.SampleLevel(bilinearSampler, float2(t0.x, t0.y), 0) * s0.x * s0.y;
    result += tex.SampleLevel(bilinearSampler, float2(t1.x, t0.y), 0) * s1.x * s0.y;
    result += tex.SampleLevel(bilinearSampler, float2(t0.x, t1.y), 0) * s0.x * s1.y;
    result += tex.SampleLevel(bilinearSampler, float2(t1.x, t1.y), 0) * s1.x * s1.y;

    return result;
}

#endif //_UTIL_INCLUDED
