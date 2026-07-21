// ImProcessor.h
#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

using namespace cv;
using namespace std;

// 连通域分析结果
struct ImdoRes {
    Mat labels;
    vector<vector<Point>> selectedContours;
    vector<Point2f> centroids;
};

class ImProcessor
{
private:
    bool showIntermediateResults;
    string windowPrefix;

    // ROI参数
    float widthRatio = 0.9f;
    float heightRatio = 0.9f;

public:
    ImProcessor(bool showIntermediate = false, const string& prefix = "");

    // 设置ROI参数
    void setRoiParameters(float width_ratio, float height_ratio);

    // 处理图像文件
    ImdoRes processImageFile(const string& imagePath);

    // 处理Mat图像
    ImdoRes processImageMat(const Mat& srcImage);

    // 静态方法：ROI区域提取
    static Mat extractRoi(const Mat& src, float width_ratio, float height_ratio);

    // 静态方法：基于区域划分的Otsu二值化
    static Mat adaptiveOtsuByRegion(const Mat& srcGray, float a, float b, int maxDepth);

    // 静态方法：连通域分析
    static ImdoRes analyzeConnectedDomains(const Mat& binaryImg);

    // 静态方法：计算质心
    static void calculateCentroids(ImdoRes& domainResult, const Mat& tophatImg);

    // 静态方法：标记连通域
    static ImdoRes labelConnectedDomains(const Mat& binaryImg, const Mat& tophatImg);

private:
    // 私有辅助方法
    static Mat computeContrastMap(const Mat& gray);
    static float getMaxContrast(const Mat& contrastMap, const Rect& region);
    static void quadtreePartition(const Mat& srcGray, const Mat& contrastMap, Mat& dstBinary,
        const Rect& region, float parentMaxContrast,
        float a, float b, int depth, int maxDepth);
};

