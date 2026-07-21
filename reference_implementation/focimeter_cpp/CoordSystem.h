// CoordSystem.h
#pragma once

#include "ImProcessor.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>

using namespace cv;
using namespace std;

// 坐标系结构体
struct CoordinateSys {
    Point2f origin;           // 原点（标签0的质心）
    Point2f x_axis_point;     // X轴正方向点（标签4的质心）
    Point2f y_axis_point;     // Y轴正方向点（标签1的质心）
    float x_basis[2];         // X轴单位向量 [cosθ, sinθ]
    float y_basis[2];         // Y轴单位向量 [cos(θ+90°), sin(θ+90°)]
    float rotation_angle;     // 坐标系旋转角度（弧度）
    bool valid;               // 坐标系是否有效

    // 默认构造函数
    CoordinateSys() : valid(false), rotation_angle(0.0f) {}
};

// 标定结果结构体
struct CalibrationResult {
    CoordinateSys coordSys;
    vector<Point2f> calibrationCentroids;
    bool calibrated;

    CalibrationResult() : calibrated(false) {}
};

// 坐标转换类
class CoordTransformer {
private:
    CoordinateSys coordSys;
    bool initialized = false;

public:
    CoordTransformer() : initialized(false) {}

    // 从坐标系初始化转换器
    bool Init(const CoordinateSys& coordSys);

    // 检查是否已初始化
    bool IsValid() const { return initialized; }

    // 图像坐标 -> 世界坐标（标定坐标系）
    Point2f ImageToWorld(const Point2f& imagePoint) const;

    // 世界坐标 -> 图像坐标
    Point2f WorldToImage(const Point2f& worldPoint) const;

    // 批量转换
    vector<Point2f> TransformPoints(const vector<Point2f>& imagePoints) const;

    // 获取底层坐标系
    const CoordinateSys& GetCoordinateSystem() const { return coordSys; }
};

// 创建坐标系
CoordinateSys Create_CSystem(const ImdoRes& signResult);