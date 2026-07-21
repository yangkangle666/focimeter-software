// foci.cpp
#include "foci.h"
#include <cmath>
#include <algorithm>
#include <iostream>

// 常量定义 - 明确单位
const float pixelSize = 4e-6f;      // 4微米转换为米: 4e-6 m

// 判断镜片类型
LensType classifyLensType(const Point2f& worldPoint1, const Point2f& worldPoint4,
    float pixelThreshold) {

    float thresholdX = pixelThreshold;
    float thresholdY = pixelThreshold;

    // 判断条件
    bool isCylindrical = (abs(worldPoint1.x) >= thresholdX) &&
        (abs(worldPoint4.y) >= thresholdY);

    bool isSpherical = (abs(worldPoint1.x) < thresholdX) &&
        (abs(worldPoint4.y) < thresholdY);

    if (isCylindrical) {
        return CYLINDRICAL;
    }
    else if (isSpherical) {
        return SPHERICAL;
    }
    else {
        return UNKNOWN;
    }
}

// 获取镜片类型字符串
string getLensTypeString(LensType type) {
    switch (type) {
    case SPHERICAL: return "球镜";
    case CYLINDRICAL: return "柱镜";
    case UNKNOWN: return "未知类型";
    default: return "未知";
    }
}

float Slens(const Point2f& calibworld1, const Point2f& meaworld1)
{    
    const float d = 0.03f;             // 3厘米转换为米: 0.03 m
    float S = (calibworld1.y* pixelSize) - (pixelSize/ 2.0f);
    float TA = ((calibworld1.y - meaworld1.y) * pixelSize) - (pixelSize / 2.0f);

    // 球镜度（单位：屈光度，1/m）
    float Fs = 0.0f;
    // 避免除以零的错误
    if (abs(S) > 1e-9f) {
        Fs = TA / (d * S);
        cout << "球光度 Fs 计算完成: Fs = " << Fs << " D (屈光度)" << endl;
    }
    else {
        cout << "警告: S值为零或接近零，无法计算球光度" <<endl;
    }
    return Fs;
}

ClensResult Clens(const Point2f& calibworld1, const Point2f& meaworld1, const Point2f& meaworld4)
{
    // 提取参数
    double h = (calibworld1.y * pixelSize) - (pixelSize / 2.0f);
    double x1 = (meaworld1.x * pixelSize) - (pixelSize / 2.0f);
    double y1 = (meaworld1.y * pixelSize) - (pixelSize / 2.0f);
    double x2 = (meaworld4.x * pixelSize) - (pixelSize / 2.0f);
    double u = 0.025f;

    double Fs;
    double Fc;
    double theta;

    // 计算公共部分
    double sqrt_term = sqrt(4 * x1 * x1 + (y1 - x2) * (y1 - x2));

    // 根据 x1 的正负确定符号
    double sign_Fs, sign_Fc_theta;
    if (x1 > 0) {
        sign_Fs = -1.0;
        sign_Fc_theta = 1.0;
    }
    else {
        sign_Fs = 1.0;
        sign_Fc_theta = -1.0;
    }

    // 计算 Fs
    Fs = (2 * h - y1 - x2 + sign_Fs * sqrt_term) / (2 * u * h);

    // 计算 Fc
    Fc = (sign_Fc_theta * sqrt_term) / (u * h);

    // 计算 theta（弧度）
    double denominator = sign_Fc_theta * sqrt_term;
    if (denominator == 0) {
        throw runtime_error("分母为0，无法计算 theta");
    }
    double asin_arg = 2 * x1 / denominator;
    // 确保参数在 arcsin 定义域内
    if (asin_arg < -1.0 || asin_arg > 1.0) {
        throw runtime_error("asin 参数超出范围 [-1, 1]");
    }
    theta = 0.5 * asin(asin_arg);

    // 转弧度为度
    theta = theta * 180.0 / CV_PI;

    // 返回结果结构体
    return { Fs, Fc, theta };
}

// 阿贝数计算函数
float Abbenum(float S_F, float S_d, float S_C) {
    // 计算分母：F光和C光的球镜度差
    float denominator = S_F - S_C;

    // 检查分母是否接近零，避免除以零错误
    if (abs(denominator) < 1e-6f) {
        throw runtime_error("分母（S_F - S_C）为零或接近零，无法计算阿贝数");
    }

    // 计算阿贝数：V = S_d / (S_F - S_C)
    float V = S_d / denominator;

    cout << "阿贝数计算完成: V = " << V << endl;
    return V;
}