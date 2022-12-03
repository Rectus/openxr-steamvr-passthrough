


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

    float3 xyz = mul(RGBtoXYZMat, rgb) / D65Ref;

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
    float3 xyz = mul(RGBtoXYZMat, rgb) / D65Ref;

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

    
    xyz.x = ((xyz.x > 0.206897) ? pow(xyz.x, 3.0) : (xyz.x - 16.0 / 116.0) / 7.787);
    xyz.y = ((xyz.y > 0.206897) ? pow(xyz.y, 3.0) : (xyz.y - 16.0 / 116.0) / 7.787);
    xyz.z = ((xyz.z > 0.206897) ? pow(xyz.z, 3.0) : (xyz.z - 16.0 / 116.0) / 7.787);

    xyz *= D65Ref;

    return mul(XYZtoRGBMat, xyz);
}
