#ifndef COORD_PREPROCESS_H
#define COORD_PREPROCESS_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <stdexcept>
using namespace cv;
using namespace std;

// 镜片类型枚举
enum LensType {
    SPHERICAL,      // 球镜
    CYLINDRICAL,    // 柱镜
    UNKNOWN         // 无法确定
};

// 存储坐标信息的结构体
struct StoredCoordinates {
    Point2f calib_world1;  // 标定图片标记1的世界坐标
    Point2f calib_world4;  // 标定图片标记4的世界坐标
    Point2f measure_world1;  // 测量图片标记1的世界坐标
    Point2f measure_world4;  // 测量图片标记4的世界坐标
};

// 柱镜度计算结果结构体
struct ClensResult {
    double Fs;
    double Fc;
    double theta;  // 角度制
};

// 判断镜片类型
LensType classifyLensType(const Point2f& worldPoint1, const Point2f& worldPoint4,
    float pixelThreshold);

// 获取镜片类型字符串
string getLensTypeString(LensType type);

// 球镜度
float Slens(const Point2f& calibworld1, const Point2f& meaworld1);

// 柱镜度
ClensResult Clens(const Point2f& calibworld1,  const Point2f& meaworld1, const Point2f& meaworld4);

// 阿贝数
float Abbenum(float S_F, float S_d, float S_C);

#endif