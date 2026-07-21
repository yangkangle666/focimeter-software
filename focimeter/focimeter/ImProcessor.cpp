// ImProcessor.cpp
#include "ImProcessor.h"
#include <iostream>
#include <algorithm>
#include <limits>

ImProcessor::ImProcessor(bool showIntermediate, const string& prefix)
    : showIntermediateResults(showIntermediate), windowPrefix(prefix) {
}

void ImProcessor::setRoiParameters(float width_ratio, float height_ratio) {
    widthRatio = width_ratio;
    heightRatio = height_ratio;
}

ImdoRes ImProcessor::processImageFile(const string& imagePath) {
    Mat image = imread(imagePath);
    if (image.empty()) {
        cerr << "无法加载图片: " << imagePath << endl;
        return ImdoRes();
    }
    return processImageMat(image);
}

ImdoRes ImProcessor::processImageMat(const Mat& srcImage) {
    // 1. ROI提取
    Mat roiImage = extractRoi(srcImage, widthRatio, heightRatio);

    // 2. 灰度化
    Mat grayImage;
    if (roiImage.channels() == 3)
        cvtColor(roiImage, grayImage, COLOR_BGR2GRAY);
    else
        grayImage = roiImage.clone();

    if (showIntermediateResults) {
        namedWindow(windowPrefix + " - 灰度图", WINDOW_NORMAL);
        imshow(windowPrefix + " - 灰度图", grayImage);
    }

    // 3. 中值滤波
    Mat filteredImage;
    medianBlur(grayImage, filteredImage, 3);

    // 4. 顶帽运算
    Mat tophatImage;
    morphologyEx(filteredImage, tophatImage, MORPH_TOPHAT,
        getStructuringElement(MORPH_RECT, Size(30, 30)));

    if (showIntermediateResults) {
        namedWindow(windowPrefix + " - 顶帽运算", WINDOW_NORMAL);
        imshow(windowPrefix + " - 顶帽运算", tophatImage);
    }

    // 5. 图像二值化
    Mat binaryImage = adaptiveOtsuByRegion(tophatImage, 0.4f, 0.7f, 2);

    if (showIntermediateResults) {
        namedWindow(windowPrefix + " - 二值化", WINDOW_NORMAL);
        imshow(windowPrefix + " - 二值化", binaryImage);
    }

    // 6. 识别五个最大的连通域
    return labelConnectedDomains(binaryImage, tophatImage);
}

Mat ImProcessor::extractRoi(const Mat& src, float width_ratio, float height_ratio) {
    if (src.empty()) {
        cerr << "警告: extractRoi接收到空图像" << endl;
        return Mat();
    }

    int roi_width = static_cast<int>(src.cols * width_ratio);
    int roi_height = static_cast<int>(src.rows * height_ratio);
    int center_x = src.cols / 2;
    int center_y = src.rows / 2;

    int x = max(0, center_x - roi_width / 2);
    int y = max(0, center_y - roi_height / 2);
    roi_width = min(roi_width, src.cols - x);
    roi_height = min(roi_height, src.rows - y);

    return src(Rect(x, y, roi_width, roi_height));
}

// 基于区域划分的Otsu二值化
Mat ImProcessor::adaptiveOtsuByRegion(const Mat& srcGray, float a, float b, int maxDepth) {
    // 参数说明：
    // a: 背景区域阈值系数（越小，越多的区域被视为背景）
    // b: 显著区域阈值系数（越大，越少的区域继续划分）
    // maxDepth: 最小划分区域尺寸，控制划分次数
    
    // 计算对比度图
    Mat contrastMap = computeContrastMap(srcGray);

    // 获取整幅图像的最大对比度
    float globalMaxContrast = getMaxContrast(contrastMap, Rect(0, 0, srcGray.cols, srcGray.rows));

    // 创建输出二值图像（初始为全黑）
    Mat dstBinary = Mat::zeros(srcGray.size(), CV_8U);

    // 开始四叉树划分
    quadtreePartition(srcGray, contrastMap, dstBinary,
        Rect(0, 0, srcGray.cols, srcGray.rows),
        globalMaxContrast, a, b, 0, maxDepth);

    return dstBinary;
}

// 连通域分析
ImdoRes ImProcessor::analyzeConnectedDomains(const Mat& binaryImg) {
    // 寻找所有轮廓
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(binaryImg, contours, hierarchy, RETR_CCOMP, CHAIN_APPROX_SIMPLE);

    // 存储选中的轮廓
    vector<vector<Point>> selectedContours;

    // 存储最外层轮廓的索引和面积
    vector<pair<int, double>> outerContourAreas;

    // 只考虑最外层轮廓（hierarchy[i][3] == -1 表示没有父轮廓）
    for (int i = 0; i < contours.size(); i++) {
        if (hierarchy[i][3] == -1) { // 最外层轮廓
            double area = contourArea(contours[i]);
            outerContourAreas.push_back(make_pair(i, area));
        }
    }

    // 按面积从大到小排序
    sort(outerContourAreas.begin(), outerContourAreas.end(),
        [](const pair<int, double>& a, const pair<int, double>& b) {
            return a.second > b.second;
        });

    // 只保留前5个最大的最外层连通域
    int numToKeep = min(5, (int)outerContourAreas.size());

    // 创建标记图像
    Mat labels = Mat::zeros(binaryImg.size(), CV_32SC1);

    // 用不同的标签值标记每个选中的轮廓
    for (int i = 0; i < numToKeep; i++) {
        int contourIndex = outerContourAreas[i].first;

        // 将轮廓添加到输出参数中
        selectedContours.push_back(contours[contourIndex]);

        // 在标签图像中绘制轮廓（使用不同的标签值，从1开始）
        drawContours(labels, contours, contourIndex, Scalar(i + 1), FILLED);
    }

    return { labels, selectedContours };
}

// 计算质心
void ImProcessor::calculateCentroids(ImdoRes& domainResult, const Mat& tophatImg) {
    Mat labels = domainResult.labels;
    vector<vector<Point>>& selectedContours = domainResult.selectedContours;

    int numContours = selectedContours.size();
    domainResult.centroids.resize(numContours);

    if (numContours == 0) {
        return;  // 如果没有找到轮廓，返回空向量
    }

    // 矩(moments)计算质心
    for (int i = 0; i < numContours; i++) {
        // 创建当前连通域的掩码
        Mat mask = Mat::zeros(labels.size(), CV_8UC1);
        drawContours(mask, selectedContours, i, Scalar(255), FILLED);

        // 使用掩码提取当前区域的图像
        Mat regionImg;
        tophatImg.copyTo(regionImg, mask);

        // 使用矩计算加权质心
        Moments m = moments(regionImg, false);  // false: 使用像素值作为权重

        if (m.m00 > 0) {
            float centroid_x = static_cast<float>(m.m10 / m.m00);
            float centroid_y = static_cast<float>(m.m01 / m.m00);
            domainResult.centroids[i] = Point2f(centroid_x, centroid_y);
        }
        else {
            // 如果加权矩为零，使用轮廓质心作为备选
            Moments contourMoment = moments(selectedContours[i], false);
            if (contourMoment.m00 > 0) {
                float centroid_x = static_cast<float>(contourMoment.m10 / contourMoment.m00);
                float centroid_y = static_cast<float>(contourMoment.m01 / contourMoment.m00);
                domainResult.centroids[i] = Point2f(centroid_x, centroid_y);
            }
            else {
                domainResult.centroids[i] = Point2f(-1, -1);  // 无效值
            }
        }
    }
}

// 标记连通域
ImdoRes ImProcessor::labelConnectedDomains(const Mat& binaryImg, const Mat& tophatImg) {
    // 调用 Imdomain 函数获取标签图像和选中的轮廓
    ImdoRes result = analyzeConnectedDomains(binaryImg);

    // 计算质心并存储在result中
    calculateCentroids(result, tophatImg);

    // 如果选中的轮廓数量不为5，直接返回
    if (result.selectedContours.size() != 5) {
        return result;
    }

    // 重新创建标签图像
    result.labels = Mat::zeros(binaryImg.size(), CV_32SC1);

    // 找到中心连通域（中间位置）
    // 方法：找到x和y坐标都接近平均值的连通域
    float avgX = 0, avgY = 0;
    int validCentroidCount = 0;
    for (const auto& centroid : result.centroids) {
        // 检查质心是否有效（Imcentroids中返回的无效值为(-1, -1)）
        if (centroid.x >= 0 && centroid.y >= 0) {
            avgX += centroid.x;
            avgY += centroid.y;
            validCentroidCount++;
        }
    }

    // 如果有少于5个有效质心，直接返回
    if (validCentroidCount < 5) {
        return result;
    }

    avgX /= 5;
    avgY /= 5;

    // 找到距离平均值最近的连通域作为中心
    int centerIdx = 0;
    float minDist = numeric_limits<float>::max();
    for (int i = 0; i < 5; i++) {
        // 跳过无效质心
        if (result.centroids[i].x < 0 || result.centroids[i].y < 0) continue;

        float dist = sqrt(pow(result.centroids[i].x - avgX, 2) +
            pow(result.centroids[i].y - avgY, 2));
        if (dist < minDist) {
            minDist = dist;
            centerIdx = i;
        }
    }

    Point2f centerPoint = result.centroids[centerIdx];

    // 临时存储轮廓，因为可能需要重新排序
    vector<vector<Point>> tempContours = result.selectedContours;

    // 重新清空selectedContours，以便按顺序填充
    result.selectedContours.clear();
    result.selectedContours.resize(5);

    // 中心标记为0
    drawContours(result.labels, tempContours, centerIdx, Scalar(0), FILLED);
    result.selectedContours[0] = tempContours[centerIdx];

    // 同时需要重新排序质心向量以匹配新的轮廓顺序
    vector<Point2f> tempCentroids = result.centroids;
    result.centroids[0] = tempCentroids[centerIdx];

    // 计算其他点相对于中心的极坐标并分类
    int assignedCount = 1; // 已经分配了中心轮廓

    for (int i = 0; i < 5; i++) {
        if (i == centerIdx) continue;

        // 检查质心是否有效
        if (tempCentroids[i].x < 0 || tempCentroids[i].y < 0) {
            // 无效质心，跳过
            continue;
        }

        Point2f vec = tempCentroids[i] - centerPoint;
        float angle = atan2(vec.y, vec.x) * 180.0f / CV_PI;

        // 根据角度分类到4个方向
        float adjustedAngle = fmod(angle + 360.0f, 360.0f);

        // 确定方向索引
        int directionIdx = -1;
        if (adjustedAngle >= 45 && adjustedAngle < 135) {
            // 下方（角度270度左右）
            directionIdx = 3;
        }
        else if (adjustedAngle >= 135 && adjustedAngle < 225) {
            // 左方（角度180度左右）
            directionIdx = 2;
        }
        else if (adjustedAngle >= 225 && adjustedAngle < 315) {
            // 上方（在图像坐标系中，y轴向下，所以上方是角度90度左右）
            directionIdx = 1;
        }
        else {
            // 右方（角度0度或360度左右）
            directionIdx = 4;
        }

        // 确保方向索引在有效范围内且未被占用
        if (directionIdx >= 1 && directionIdx <= 4 &&
            result.selectedContours[directionIdx].empty()) {
            // 标记对应的标签值
            drawContours(result.labels, tempContours, i, Scalar(directionIdx), FILLED);
            result.selectedContours[directionIdx] = tempContours[i];
            result.centroids[directionIdx] = tempCentroids[i];
            assignedCount++;
        }
    }

    // 如果有未分配的轮廓（可能因为质心无效或角度分类冲突），将它们分配到剩余位置
    if (assignedCount < 5) {
        for (int i = 0; i < 5; i++) {
            if (i == centerIdx) continue;

            // 检查当前轮廓是否已被分配
            bool alreadyAssigned = false;
            for (int j = 0; j < 5; j++) {
                if (j != 0 && !result.selectedContours[j].empty() &&
                    result.selectedContours[j].size() == tempContours[i].size() &&
                    norm(result.selectedContours[j][0] - tempContours[i][0]) < 1e-6) {
                    alreadyAssigned = true;
                    break;
                }
            }

            if (alreadyAssigned) continue;

            // 找到第一个空的位置
            for (int j = 1; j < 5; j++) {
                if (result.selectedContours[j].empty()) {
                    drawContours(result.labels, tempContours, i, Scalar(j), FILLED);
                    result.selectedContours[j] = tempContours[i];
                    result.centroids[j] = tempCentroids[i];
                    assignedCount++;
                    break;
                }
            }

            if (assignedCount >= 5) break;
        }
    }

    return result;
}

Mat ImProcessor::computeContrastMap(const Mat& gray) {
    Mat contrastMap(gray.size(), CV_32F, Scalar(0));
    for (int y = 0; y < gray.rows; ++y) {
        for (int x = 0; x < gray.cols; ++x) {
            int dx = (x == 0) ? 0 : abs(gray.at<uchar>(y, x) - gray.at<uchar>(y, x - 1));
            int dy = (y == 0) ? 0 : abs(gray.at<uchar>(y, x) - gray.at<uchar>(y - 1, x));
            contrastMap.at<float>(y, x) = max(dx, dy);
        }
    }
    return contrastMap;
}

float ImProcessor::getMaxContrast(const Mat& contrastMap, const Rect& region) {
    float maxVal = 0;
    for (int y = region.y; y < region.y + region.height; ++y) {
        for (int x = region.x; x < region.x + region.width; ++x) {
            if (contrastMap.at<float>(y, x) > maxVal) {
                maxVal = contrastMap.at<float>(y, x);
            }
        }
    }
    return maxVal;
}

// 四叉树划分函数
void ImProcessor::quadtreePartition(const Mat& srcGray, const Mat& contrastMap, Mat& dstBinary,
    const Rect& region, float parentMaxContrast,
    float a, float b, int depth, int maxDepth) {

    // 获取当前区域的最大对比度
    float regionMaxContrast = getMaxContrast(contrastMap, region);

    // 如果深度达到最大深度或区域太小，则停止划分，使用大津法二值化
    if (depth >= maxDepth || region.width <= 4 || region.height <= 4) {
        Mat subRegion = srcGray(region);
        Mat binarySub;
        threshold(subRegion, binarySub, 0, 255, THRESH_BINARY | THRESH_OTSU);
        binarySub.copyTo(dstBinary(region));
        return;
    }

    // 第一次划分：判断是否为背景区域
    if (regionMaxContrast <= a * parentMaxContrast) {
        // 背景区域，直接设为黑色（0）
        rectangle(dstBinary, region, Scalar(0), FILLED);
    }
    else {
        // 非背景区域，继续划分
        int subWidth = region.width / 2;
        int subHeight = region.height / 2;
        vector<Rect> subRegions = {
            Rect(region.x, region.y, subWidth, subHeight),
            Rect(region.x + subWidth, region.y, region.width - subWidth, subHeight),
            Rect(region.x, region.y + subHeight, subWidth, region.height - subHeight),
            Rect(region.x + subWidth, region.y + subHeight, region.width - subWidth, region.height - subHeight)
        };

        for (const auto& subRegion : subRegions) {
            float subMaxContrast = getMaxContrast(contrastMap, subRegion);
            // 第二次划分：根据对比度大小决定是否继续划分或使用不同增强方式
            if (subMaxContrast <= a * regionMaxContrast) {
                // 背景子区域，设为黑色（0）
                rectangle(dstBinary, subRegion, Scalar(0), FILLED);
            }
            else if (subMaxContrast >= b * regionMaxContrast) {
                // 对比度非常显著区域，直接使用大津法
                Mat subGray = srcGray(subRegion);
                Mat binarySub;
                threshold(subGray, binarySub, 0, 255, THRESH_BINARY | THRESH_OTSU);
                binarySub.copyTo(dstBinary(subRegion));
            }
            else {
                // 对比度较显著区域，继续递归划分
                quadtreePartition(srcGray, contrastMap, dstBinary, subRegion,
                    regionMaxContrast, a, b, depth + 1, maxDepth);
            }
        }
    }
}