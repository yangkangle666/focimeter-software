// CoordSystem.cpp
#include "CoordSystem.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>

using namespace cv;
using namespace std;

// 创建坐标系
CoordinateSys Create_CSystem(const ImdoRes& signResult) {
    const vector<vector<Point>>& selectedContours = signResult.selectedContours;
    const vector<Point2f>& centroids = signResult.centroids;

    CoordinateSys coordSys;
    coordSys.valid = false;

    // 检查是否有5个轮廓和质心
    if (selectedContours.size() != 5 || centroids.size() != 5) {
        cout << "错误：没有找到5个连通域或质心，找到 "
            << selectedContours.size() << " 个轮廓，"
            << centroids.size() << " 个质心" << endl;
        return coordSys; // 返回无效的坐标系
    }

    // 检查质心是否有效（防止 (-1, -1) 的无效值）
    for (int i = 0; i < 5; i++) {
        if (centroids[i].x < 0 || centroids[i].y < 0) {
            cout << "警告：质心 " << i << " 无效" << endl;
            return coordSys;
        }
    }

    // 设置原点
    coordSys.origin = centroids[0];
    coordSys.y_axis_point = centroids[1];  // Y轴正方向点
    coordSys.x_axis_point = centroids[4];  // X轴正方向点

    // Y轴：0->1方向
    Point2f y_axis_vec = centroids[1] - centroids[0];
    float y_length = norm(y_axis_vec);

    if (y_length < FLT_EPSILON) {
        cout << "错误：Y轴长度过小" << endl;
        return coordSys;  // 返回无效的坐标系
    }

    // 计算Y轴单位向量
    coordSys.y_basis[0] = y_axis_vec.x / y_length; // Y轴X分量
    coordSys.y_basis[1] = y_axis_vec.y / y_length; // Y轴Y分量

    // X轴：与Y轴垂直（顺时针旋转90度）
    coordSys.x_basis[0] = -coordSys.y_basis[1]; // X轴X分量
    coordSys.x_basis[1] = coordSys.y_basis[0]; // X轴Y分量

    // 验证X轴方向：计算点4在新坐标系中的X坐标应为正
    Point2f vec_to_4 = centroids[4] - centroids[0];
    float x_coord = vec_to_4.x * coordSys.x_basis[0] + vec_to_4.y * coordSys.x_basis[1];

    // 如果X坐标为负，则需要反转X轴方向
    if (x_coord < 0) {
        coordSys.x_basis[0] = -coordSys.x_basis[0];
        coordSys.x_basis[1] = -coordSys.x_basis[1];
        // 重新计算X坐标
        x_coord = vec_to_4.x * coordSys.x_basis[0] + vec_to_4.y * coordSys.x_basis[1];
    }
    // 计算点4在新坐标系中的Y坐标（应为接近0）
    float y_coord = vec_to_4.x * coordSys.y_basis[0] + vec_to_4.y * coordSys.y_basis[1];

    // 计算坐标系旋转角度（弧度）
    // 注意：图像坐标系中Y轴向下，所以旋转角度需要根据实际需求调整
    coordSys.rotation_angle = atan2(coordSys.y_basis[1], coordSys.y_basis[0]);

    // 标记坐标系为有效
    coordSys.valid = true;

    return coordSys;
}

// 初始化转换器（使用已有坐标系）
bool CoordTransformer::Init(const CoordinateSys& coordSys) 
{
    this->coordSys = coordSys;
    initialized = coordSys.valid;
    return initialized;
}

// 检查是否已初始化
bool CoordTransformer::IsValid() const {
    return initialized;
}

// 图像坐标 -> 世界坐标（标定坐标系）
Point2f CoordTransformer::ImageToWorld(const Point2f& imagePoint) const {
    if (!initialized) {
        throw runtime_error("坐标转换器未初始化");
    }

    Point2f vec = imagePoint - coordSys.origin;
    float x = vec.x * coordSys.x_basis[0] + vec.y * coordSys.x_basis[1];
    float y = vec.x * coordSys.y_basis[0] + vec.y * coordSys.y_basis[1];

    return Point2f(x, y);
}

// 世界坐标 -> 图像坐标
Point2f CoordTransformer::WorldToImage(const Point2f& worldPoint) const {
    if (!initialized) {
        throw runtime_error("坐标转换器未初始化");
    }

    float img_x = coordSys.origin.x +
        worldPoint.x * coordSys.x_basis[0] +
        worldPoint.y * coordSys.y_basis[0];
    float img_y = coordSys.origin.y +
        worldPoint.x * coordSys.x_basis[1] +
        worldPoint.y * coordSys.y_basis[1];

    return Point2f(img_x, img_y);
}

// 批量转换图像坐标 -> 世界坐标
vector<Point2f> CoordTransformer::TransformPoints(const vector<Point2f>& imagePoints) const {
    if (!initialized) {
        throw runtime_error("坐标转换器未初始化");
    }

    vector<Point2f> worldPoints;
    worldPoints.reserve(imagePoints.size());

    for (const auto& pt : imagePoints) {
        worldPoints.push_back(ImageToWorld(pt));
    }

    return worldPoints;
}
