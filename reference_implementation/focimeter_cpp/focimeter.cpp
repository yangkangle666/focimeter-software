#include "foci.h"
#include "ImProcessor.h"
#include "CoordSystem.h"
#include <opencv2/opencv.hpp>
#include <iostream>

using namespace cv;
using namespace std;

int main()
{
    // ========== 配置参数 ==========
    string calibImagePath = "D:\\calibration.png";    // 标定图片路径
    string measureImagePath = "D:\\measurement.png";  // 测量图片路径

    bool recalibrate = true;  // 是否重新标定（false则加载已有标定）
    bool showImages = true;   // 是否显示处理过程图像

    // ========== 创建图像处理器实例 ==========
    ImProcessor calibProcessor(showImages, "标定阶段");
    ImProcessor measureProcessor(showImages, "测量阶段");

    // ========== 1. 标定阶段（使用第一张图片） ==========
    CalibrationResult calibResult;

    if (recalibrate) {
        cout << "=== 开始标定（使用第一张图片）===" << endl;

        // 处理标定图片
        ImdoRes calibSignResult = calibProcessor.processImageFile(calibImagePath);

        if (calibSignResult.selectedContours.size() != 5) {
            cerr << "标定失败：未找到5个标记点" << endl;
            return -1;
        }

        // 建立坐标系
        CoordinateSys coordSys = Create_CSystem(calibSignResult);
        if (!coordSys.valid) {
            cerr << "错误：坐标系建立失败" << endl;
            return -1;
        }

        // 保存标定结果
        calibResult.coordSys = coordSys;
        calibResult.calibrationCentroids = calibSignResult.centroids;
        calibResult.calibrated = true;

        cout << "\n标定完成！" << endl;
        cout << "原点位置: (" << coordSys.origin.x << ", "
            << coordSys.origin.y << ")" << endl;
        cout << "坐标系旋转角度: " << coordSys.rotation_angle * 180 / CV_PI
            << " 度" << endl;
    }

    // ========== 2. 初始化坐标转换器 ==========
    CoordTransformer transformer;
    transformer.Init(calibResult.coordSys);

    if (!transformer.IsValid()) {
        cerr << "错误：坐标转换器初始化失败" << endl;
        return -1;
    }

    // ========== 3. 测量阶段（使用第二张图片） ==========
    cout << "\n=== 开始测量（使用第二张图片）===" << endl;

    // 使用 ImProcessor 处理测量图片
    ImdoRes measureSignResult = measureProcessor.processImageFile(measureImagePath);

    if (measureSignResult.selectedContours.size() != 5) {
        cerr << "测量失败：未找到5个标记点" << endl;
        return -1;
    }

    // ========== 4. 在标定坐标系下进行坐标转换 ==========
    cout << "\n=== 坐标转换 ===" << endl;

    // 标定图片转换
    Point2f calib_world1 = transformer.ImageToWorld(calibResult.calibrationCentroids[1]);
    Point2f calib_world4 = transformer.ImageToWorld(calibResult.calibrationCentroids[4]);

    // 测量图片转换
    Point2f measure_world1 = transformer.ImageToWorld(measureSignResult.centroids[1]);
    Point2f measure_world4 = transformer.ImageToWorld(measureSignResult.centroids[4]);

    // ========== 5. 存储坐标信息 ==========
    cout << "\n=== 存储坐标信息 ===" << endl;
    StoredCoordinates storedCoords;

    // 标定图片中标记1和标记4的世界坐标
    storedCoords.calib_world1 = calib_world1;
    storedCoords.calib_world4 = calib_world4;

    // 测量图片中标记1和标记4的世界坐标
    storedCoords.measure_world1 = measure_world1;
    storedCoords.measure_world4 = measure_world4;


    // 在控制台显示存储的坐标
    cout << "\n标定图片坐标:" << endl;
    cout << "标记1 (Y轴正方向): (" << storedCoords.calib_world1.x << ", "
        << storedCoords.calib_world1.y << ")" << endl;
    cout << "标记4 (X轴正方向): (" << storedCoords.calib_world4.x << ", "
        << storedCoords.calib_world4.y << ")" << endl;

    cout << "\n测量图片坐标:" << endl;
    cout << "标记1: (" << storedCoords.measure_world1.x << ", "
        << storedCoords.measure_world1.y << ")" << endl;
    cout << "标记4: (" << storedCoords.measure_world4.x << ", "
        << storedCoords.measure_world4.y << ")" << endl;

    // ========== 6. 判断镜片类型 ==========
    float pixelThreshold = 1.0f;
    LensType lensType = classifyLensType(
        storedCoords.measure_world1,
        storedCoords.measure_world4,
        pixelThreshold);

    // 输出判断细节
    cout << "标记1在Y轴垂直方向偏移(X坐标): "
        << storedCoords.measure_world1.x << endl;
    cout << "标记4在X轴垂直方向偏移(Y坐标): "
        << storedCoords.measure_world4.y << endl;
    cout << "判断阈值: " << pixelThreshold << " 像素" << endl;
    cout << "镜片类型: " << getLensTypeString(lensType) << endl;

    // 判断依据说明
    if (lensType == CYLINDRICAL) {
        cout << "判断依据: 标记1的X坐标绝对值 >= " << pixelThreshold
            << " 且标记4的Y坐标绝对值 >= " << pixelThreshold << endl;
        // 调用Clens函数计算柱镜参数
        try {
            ClensResult clensResult = Clens(
                storedCoords.calib_world1,
                storedCoords.measure_world1,
                storedCoords.measure_world4);

            cout << "\n=== 柱镜参数计算结果 ===" << endl;
            cout << "球光度 Fs: " << clensResult.Fs << endl;
            cout << "柱光度 Fc: " << clensResult.Fc << endl;
            cout << "轴位角度 θ: " << clensResult.theta << " 度" << endl;
        }
        catch (const runtime_error& e) {
            cerr << "柱镜计算错误: " << e.what() << endl;
        }
    }
    else if (lensType == SPHERICAL) {
        cout << "判断依据: 标记1的X坐标绝对值 < " << pixelThreshold
            << " 且标记4的Y坐标绝对值 < " << pixelThreshold << endl;
        // 如果是球镜，计算球镜度
        float Fs = Slens(storedCoords.calib_world1, storedCoords.measure_world1);
        cout << "球镜度 Fs: " << Fs << endl;
    }
    else {
        cout << "判断依据: 无法确定镜片类型" << endl;
    }


    waitKey(0);
    return 0;
}