#pragma once

#define _USE_MATH_DEFINES
#include <math.h>
#include <vector>
#include <algorithm>
#include <opencv2/core/matx.hpp>
#include <opencv2/core/quaternion.hpp>
#include <xr_linear.h>


struct EulerAngles
{
    double X;
    double Y;
    double Z;
};

inline double RadToDeg(double r)
{
    return r * 180.0 / M_PI;
}

inline double DegToRad(double d)
{
    return d * M_PI / 180.0;
}

inline float RadToDeg(float r)
{
    return r * 180.0f / (float)M_PI;
}

inline float DegToRad(float d)
{
    return d * (float)M_PI / 180.0f;
}

inline EulerAngles HMDMatRotationToEuler(vr::HmdMatrix34_t& R)
{
    double sy = sqrt(R.m[0][0] * R.m[0][0] + R.m[1][0] * R.m[1][0]);

    EulerAngles angles;
    if (sy > 1e-6)
    {
        angles.X = RadToDeg(atan2(R.m[2][1], R.m[2][2]));
        angles.Y = RadToDeg(atan2(-R.m[2][0], sy));
        angles.Z = RadToDeg(atan2(R.m[1][0], R.m[0][0]));
    }
    else
    {
        angles.X = RadToDeg(atan2(-R.m[1][2], R.m[1][1]));
        angles.Y = RadToDeg(atan2(-R.m[2][0], sy));
        angles.Z = RadToDeg(0.0);
    }

    return angles;
}

inline EulerAngles CVMatRotationToEuler(cv::Mat& R)
{
    double sy = sqrt(R.at<double>(0, 0) * R.at<double>(0, 0) + R.at<double>(1, 0) * R.at<double>(1, 0));

    EulerAngles angles;
    if (sy > 1e-6)
    {
        angles.X = RadToDeg(atan2(R.at<double>(2, 1), R.at<double>(2, 2)));
        angles.Y= RadToDeg(atan2(-R.at<double>(2, 0), sy));
        angles.Z = RadToDeg(atan2(R.at<double>(1, 0), R.at<double>(0, 0)));
    }
    else
    {
        angles.X = RadToDeg(atan2(-R.at<double>(1, 2), R.at<double>(1, 1)));
        angles.Y = RadToDeg(atan2(-R.at<double>(2, 0), sy));
        angles.Z = RadToDeg(0.0);
    }

    return angles;
}

inline cv::Mat EulerToCVMatRotation(EulerAngles angles)
{
    double rx = DegToRad(angles.X);
    double ry = DegToRad(angles.Y);
    double rz = DegToRad(angles.Z);

    cv::Mat X = (cv::Mat_<double>(3, 3) << 1, 0, 0, 0, cos(rx), -sin(rx), 0, sin(rx), cos(rx));
    cv::Mat Y = (cv::Mat_<double>(3, 3) << cos(ry), 0, sin(ry), 0, 1, 0, -sin(ry), 0, cos(ry));
    cv::Mat Z = (cv::Mat_<double>(3, 3) << cos(rz), -sin(rz), 0, sin(rz), cos(rz), 0, 0, 0, 1);

    return Z * Y * X;
}

inline XrMatrix4x4f ToXRMatrix4x4(vr::HmdMatrix44_t& input)
{
    XrMatrix4x4f output =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], input.m[3][0],
        input.m[0][1], input.m[1][1], input.m[2][1], input.m[3][1],
        input.m[0][2], input.m[1][2], input.m[2][2], input.m[3][2],
        input.m[0][3], input.m[1][3], input.m[2][3], input.m[3][3]
    };
    return output;
}

inline XrMatrix4x4f ToXRMatrix4x4(vr::HmdMatrix34_t& input)
{
    XrMatrix4x4f output =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], 0,
        input.m[0][1], input.m[1][1], input.m[2][1], 0,
        input.m[0][2], input.m[1][2], input.m[2][2], 0,
        input.m[0][3], input.m[1][3], input.m[2][3], 1
    };
    return output;
}

inline XrMatrix4x4f ToXRMatrix4x4Inverted(vr::HmdMatrix44_t& input)
{
    XrMatrix4x4f temp =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], input.m[3][0],
        input.m[0][1], input.m[1][1], input.m[2][1], input.m[3][1],
        input.m[0][2], input.m[1][2], input.m[2][2], input.m[3][2],
        input.m[0][3], input.m[1][3], input.m[2][3], input.m[3][3]
    };
    XrMatrix4x4f output;
    XrMatrix4x4f_Invert(&output, &temp);
    return output;
}


inline XrMatrix4x4f ToXRMatrix4x4Inverted(vr::HmdMatrix34_t& input)
{
    XrMatrix4x4f temp =
    {
        input.m[0][0], input.m[1][0], input.m[2][0], 0,
        input.m[0][1], input.m[1][1], input.m[2][1], 0,
        input.m[0][2], input.m[1][2], input.m[2][2], 0,
        input.m[0][3], input.m[1][3], input.m[2][3], 1
    };
    XrMatrix4x4f output;
    XrMatrix4x4f_Invert(&output, &temp);
    return output;
}


inline XrMatrix4x4f CVMatToXrMatrix(cv::Mat& inMatrix)
{
    XrMatrix4x4f outMatrix;
    XrMatrix4x4f_CreateIdentity(&outMatrix);
    for (int y = 0; y < inMatrix.rows; y++)
    {
        for (int x = 0; x < inMatrix.cols; x++)
        {
            outMatrix.m[x + y * 4] = (float)inMatrix.at<double>(y, x);
        }
    }
    return outMatrix;
}