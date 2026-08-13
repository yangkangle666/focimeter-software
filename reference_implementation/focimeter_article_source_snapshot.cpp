#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QFileDialog>
#include <QDebug>
#include<vector>
#include <QImage>
#include <vector>
#include <QStyledItemDelegate>
#include <QDateTime>
#include<cmath>
#include <QFile>
#include <QMessageBox>
using namespace std;
int sliderPos = 70;
Mat imageEllispe;
#define INPUT_VIDEO_W   (1280)
#define INPUT_VIDEO_H   (1024)
//全局回调函数
void processImage(int, void *)
{
    vector<vector<Point> > contours;
    Mat bimage = imageEllispe >= sliderPos;

    findContours(bimage, contours, RETR_LIST, CHAIN_APPROX_NONE);

    Mat cimage = Mat::zeros(bimage.size(), CV_8UC3);

    for(size_t i = 0; i < contours.size(); i++)
    {
        size_t count = contours[i].size();
        if( count < 6 )
            continue;

        Mat pointsf;
        Mat(contours[i]).convertTo(pointsf, CV_32F);
        RotatedRect box = fitEllipse(pointsf);

        if( MAX(box.size.width, box.size.height) > MIN(box.size.width, box.size.height)*30 )
            continue;
        drawContours(cimage, contours, (int)i, Scalar::all(255), 1, 8);

        ellipse(cimage, box, Scalar(0,0,255), 1, LINE_AA);
        ellipse(cimage, box.center, box.size*0.5f, box.angle, 0, 360, Scalar(0,255,255), 1, LINE_AA);
        Point2f vtx[4];
        box.points(vtx);
        for( int j = 0; j < 4; j++ )
            line(cimage, vtx[j], vtx[(j+1)%4], Scalar(0,255,0), 1, LINE_AA);
    }
    imshow("result", cimage);
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    center_X = 0;
    center_Y = 0;
    horizontal_value = 0;
    thresholdState = THRESHOLD_TYPE_AUTO;
    vertical_value = 0;
    QStyledItemDelegate *delegate = new QStyledItemDelegate(this);
    ui->comboBox->setItemDelegate(delegate);
    ui->comboBox->setStyleSheet("QComboBox QAbstractItemView::item {min-height: 60px;}");
    autoCorrectBoot();//自动矫正
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::imageTwoValue()
{
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        //求最大值
        int maxThreshold = 0;
        int thresholdNumber = 0;
        for (int h = 0;h < INPUT_VIDEO_H;h++)
        {
            for (int w = 0;w < INPUT_VIDEO_W;w++){
                thresholdNumber = image.data[h*w + w];
                if(thresholdNumber> maxThreshold)
                {
                    maxThreshold = thresholdNumber;
                }
            }
        }
        qDebug()<<"求最大值:"<<maxThreshold;
        if(     thresholdState == THRESHOLD_TYPE_AUTO)
        {
            cv::adaptiveThreshold(image, twoValueImage,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);

        }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
        {
            int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
            threshold(image, twoValueImage, twoValueThres , 255 ,0);
        }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
        {
            threshold(image, twoValueImage, maxThreshold/2 , 255 ,0);
        }else
        {
            int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
            threshold(image, twoValueImage, maxThreshold*p/100 , 255 ,0);
        }
    }
    QImage resultImage = cvMat2QImage(twoValueImage);
    QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
    ui->original_label->setPixmap(QPixmap::fromImage(newImg));
}

bool MainWindow::fiveLightArea()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            ui->max_lineEdit->setText (QString::number (maxThreshold));
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number < 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            //五点拟合
            vector<Point> areaPoint;
            areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
            areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
            areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
            areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
            areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
            Rect resultRect = cv::boundingRect(areaPoint);//求区域
            int radius = (resultRect.width/2 + resultRect.height/2)/2;
            int disNumber = 0;
            int garyNumber = 0;
            long long totalNumber = 0;
            for (int h  = 0; h < INPUT_VIDEO_H; h++)
            {
                for (int w  = 0; w < INPUT_VIDEO_W; w++)
                {
                    disNumber = sqrt(pow(abs(w - (resultRect.x + resultRect.width/2)),2 )+ pow(abs(h - (resultRect.y + resultRect.height/2)),2));
                    if(disNumber < radius){
                        garyNumber++;
                        totalNumber+= image.data[1280*h+w];
                        image.data[1280*h+w] = 100;
                    }
                }
            }


            ui->five_light_radios_lineEdit->setText (QString::number (radius));
            ui->five_light_Area_lineEdit->setText (QString::number (garyNumber));
            ui->five_light_total_lineEdit->setText (QString::number (totalNumber));
            //显示到屏幕
            if(1)
            {
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-20, // 半径
                           100, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-20, // 半径
                           100, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-20, // 半径
                           100, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           100, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                           recordPointE.height/2-10, // 半径
                           100, // 颜色（这里用黑色）
                           2); // 厚

                //

                cv::circle(image, // 目标图像
                           cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                           (resultRect.width/2+resultRect.height/2)/2, // 半径
                           100, // 颜色（这里用黑色）
                           2); // 厚
                ui->center_X_value_lineEdit->setText (QString::number (recordPointC.x));
                ui->center_Y_value_lineEdit->setText (QString::number (recordPointC.y));
                ui->radius_value_lineEdit->setText (QString::number ((resultRect.width/2+resultRect.height/2)/2));;
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.width/2, // 半径
                           100, // 颜色（这里用黑色）
                           2); // 厚

            }
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
    return  0;
}

bool MainWindow::fourLightAverige()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        //求最大值
        int maxThreshold = 0;
        int thresholdNumber = 0;
        for (int h = 0;h < INPUT_VIDEO_H;h++)
        {
            for (int w = 0;w < INPUT_VIDEO_W;w++){
                thresholdNumber = image.data[h*w + w];
                if(thresholdNumber> maxThreshold)
                {
                    maxThreshold = thresholdNumber;
                }
            }
        }
        qDebug()<<"求最大值:"<<maxThreshold;
        //
        cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
        //do image filter
        cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));

        //Binarization process of the cropped image
        if(     thresholdState == THRESHOLD_TYPE_AUTO)
        {
            cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
        }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
        {
            int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
            threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
        }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
        {
            threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
        }else
        {
            int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
            threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
        }
        //start to find split rect after Binarization
        cv::Mat imgTemp = binariedImg.clone();
        //查找轮廓
        cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
        qDebug() << "contours.size() = " << contours.size();
        int max_w = 0;
        Rect recordMaxPoint;//记录最大圆心位置
        for (int i = 0; i < contours.size(); i++)
        {
            vector<Point> edgePoint = contours.at(i);
            Rect rect = cv::boundingRect(edgePoint);//求区域
            if(rect.width > max_w)
            {
                max_w = rect.width;//求最大面积
                recordMaxPoint = rect;
            }
        }
        qDebug()<<"max_w:"<< max_w;
        Rect recordPointA;//记录四个点位置
        Rect recordPointB;//记录四个点位置
        Rect recordPointC;//记录四个点位置
        Rect recordPointD;//记录四个点位置
        int number = 0;//记录个数
        for (int i = 0; i < contours.size(); i++) {
            vector<Point> edgePoint = contours.at(i);
            Rect rect = cv::boundingRect(edgePoint);//求区域
            if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆

                int distance_x = abs(((rect.x + rect.width/2) - (recordMaxPoint.x + recordMaxPoint.width/2))) ;
                int distance_y = abs(((rect.y + rect.height/2) - (recordMaxPoint.y + recordMaxPoint.height/2)));
                if(distance_x < max_w*1.5 && distance_x > 10)
                {
                    if( distance_y < max_w*1.5 && distance_y > 10)
                    {
                        if(number == 0)
                        {
                            number++;
                            recordPointA = rect;
                        }
                        else if(number == 1)
                        {
                            number++;
                            recordPointB = rect;
                        }
                        else if(number == 2)
                        {
                            number++;
                            recordPointC = rect;
                        }
                        else if(number == 3)
                        {
                            number++;
                            recordPointD = rect;
                        }
                    }
                }
            }
        }
        if(number != 5 )
        {
            QImage resultImage = cvMat2QImage(binariedImg);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
            QMessageBox msgBox;
            msgBox.setText(tr("Lookup center circle failure!"));
            msgBox.exec();
            return 0;
        }
        cv::circle(image, // 目标图像
                   cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                   recordPointA.height/2-10, // 半径
                   100, // 颜色（这里用黑色）
                   2); // 厚
        cv::circle(image, // 目标图像
                   cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                   recordPointB.height/2-10, // 半径
                   100, // 颜色（这里用黑色）
                   2); // 厚
        cv::circle(image, // 目标图像
                   cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                   recordPointC.height/2-10, // 半径
                   100, // 颜色（这里用黑色）
                   2); // 厚
        cv::circle(image, // 目标图像
                   cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                   recordPointD.height/2-10, // 半径
                   100, // 颜色（这里用黑色）
                   2); // 厚
        cv::circle(image, // 目标图像
                   cv::Point(recordMaxPoint.x + recordMaxPoint.width/2,recordMaxPoint.y + recordMaxPoint.height/2), // 中心点坐标
                   recordMaxPoint.height/2-10, // 半径
                   100, // 颜色（这里用黑色）
                   2); // 厚

        ui->big_circular_lineEdit->setText (QString::number (max_w/2));
        ui->small_circular_lineEdit->setText (QString::number ((recordPointA.width/2 + recordPointB.width/2 + recordPointC.width/2+ recordPointD.width/2)/4));
        //////////////////////////////////////求区域///////////////////////////////////////////////
        long long totalNumber = 0;
        vector<Point> areaPoint;
        areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
        areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
        areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
        areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
        Rect resultRect = cv::boundingRect(areaPoint);//求区域
        cv::circle(image, // 目标图像
                   cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                   resultRect.width*0.7, // 半径
                   10, // 颜色（这里用黑色）
                   2); // 厚
        for(int w = resultRect.x; w< resultRect.x +  resultRect.width; w++ )
        {
            for(int h = resultRect.y; h< resultRect.y +  resultRect.height; h++ )
            {
                totalNumber += image.data[h*w + w];
            }
        }

        ui->pixel_value_label->setText (QString::number (resultRect.height * resultRect.width));
        ui->gray_value_label->setText (QString::number (totalNumber / (resultRect.height * resultRect.width)));
        ui->total_number_label->setText (QString::number (totalNumber));
        ui->distance_label->setText (QString::number (resultRect.height + resultRect.width/2));

        //显示到屏幕
        QImage resultImage = cvMat2QImage(image);
        QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
        ui->original_label->setPixmap(QPixmap::fromImage(newImg));
    }
    return 0;
}

QImage MainWindow::cvMat2QImage(const cv::Mat& mat)
{
    if(mat.type() == CV_8UC1)
    {
        QImage image(mat.cols, mat.rows, QImage::Format_Indexed8);
        image.setColorCount(256);
        for(int i = 0; i < 256; i++)
        {
            image.setColor(i, qRgb(i, i, i));
        }
        uchar *pSrc = mat.data;
        for(int row = 0; row < mat.rows; row ++)
        {
            uchar *pDest = image.scanLine(row);
            memcpy(pDest, pSrc, mat.cols);
            pSrc += mat.step;
        }
        return image;
    }
    else if(mat.type() == CV_8UC3)
    {
        const uchar *pSrc = (const uchar*)mat.data;
        QImage image(pSrc, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return image.rgbSwapped();
    }
    else if(mat.type() == CV_8UC4)
    {
        const uchar *pSrc = (const uchar*)mat.data;
        QImage image(pSrc, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32);
        return image.copy();
    }
    else
    {
        return QImage();
    }
}

bool MainWindow::findAllContours()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image
            //求最大值
            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"求最大值:"<<maxThreshold;
            //Binarization process of the cropped image
            if(     thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);

            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }

            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                //qDebug() << QString("x = %1,y = %2,width = %3, height = %4").arg(rect.x).arg(rect.y).arg(rect.width).arg(rect.height);//打印位置高宽
                //画圆
                if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                    cv::circle(image, // 目标图像
                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                               (rect.height/2 + rect.width/2)/2, // 半径
                               50, // 颜色（这里用黑色）
                               2); // 厚
                }
            }
        }
    }
    //    namedWindow("result", 1);
    //    imshow("result", image);
    QImage resultImage = cvMat2QImage(image);
    QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
    ui->original_label->setPixmap(QPixmap::fromImage(newImg));
    return 0;
}

bool MainWindow::addImageAllNumber()
{
    long long total_number = 0;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            for (int h  = 0; h < INPUT_VIDEO_H; h++)
            {
                for (int w  = 0; w < INPUT_VIDEO_W; w++)
                {
                    total_number+=image.data[h*w + w];
                }
            }
        }
    }
    ui->all_image_average_lineEdit->setText (QString::number (total_number/(1024*1280)));
    ui->lineEdit->setText (QString::number (total_number));
    QImage resultImage = cvMat2QImage(image);
    QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
    ui->original_label->setPixmap(QPixmap::fromImage(newImg));
    return 0;
}

bool MainWindow::aretotalNumber()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //求最大值
            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"求最大值:"<<maxThreshold;
            //Binarization process of the cropped image
            if(     thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);

            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            int rec_number = 0;
            long tatalnumber = 0;
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                //qDebug() << QString("x = %1,y = %2,width = %3, height = %4").arg(rect.x).arg(rect.y).arg(rect.width).arg(rect.height);
                //画圆
                if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                    cv::circle(image, // 目标图像
                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                               (rect.height/2 + rect.width/2)/2, // 半径
                               50, // 颜色（这里用黑色）
                               2); // 厚
                    for(int  h = rect.y;h < rect.y + rect.height;h++)
                    {
                        for(int  w = rect.x;w < rect.x + rect.width;w++)
                        {
                            tatalnumber+=image.data[h*w + w];
                            rec_number++;
                        }
                    }
                }
            }
            ui->light_are_lineEdit->setText (QString::number (tatalnumber));
            ui->light_are_average_lineEdit->setText (QString::number (tatalnumber/rec_number));
        }
    }
    //        namedWindow("result", 1);
    //        imshow("result", image);
    QImage resultImage = cvMat2QImage(image);
    QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
    ui->original_label->setPixmap(QPixmap::fromImage(newImg));
    return 0;
}

bool MainWindow::total255Number()
{
    long long total_number = 0;
    int num = 0;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            for (int h  = 0; h < INPUT_VIDEO_H; h++)
            {
                for (int w  = 0; w < INPUT_VIDEO_W; w++)
                {
                    num = image.data[h*w + w];
                    if(num == 255)
                    {
                        total_number+=image.data[h*w + w];
                    }
                }
            }
        }
    }
    ui->total_255_number_lineEdit->setText (QString::number (total_number));
    QImage resultImage = cvMat2QImage(image);
    QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
    ui->original_label->setPixmap(QPixmap::fromImage(newImg));
    return 0;
}

bool MainWindow::lightAreAverige()
{
    int normalNumber = 0;
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //求最大值
            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"求最大值:"<<maxThreshold;
            //Binarization process of the cropped image
            if(     thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);

            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            long totalNumber = 0;
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                //qDebug() << QString("x = %1,y = %2,width = %3, height = %4").arg(rect.x).arg(rect.y).arg(rect.width).arg(rect.height);//打印位置高宽
                //画圆
                if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                    normalNumber++;
                    cv::circle(image, // 目标图像
                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                               (rect.height/2 + rect.width/2)/2, // 半径
                               50, // 颜色（这里用黑色）
                               2); // 厚
                    for(int  h = rect.y;h < rect.y + rect.height;h++)
                    {
                        for(int  w = rect.x;w < rect.x + rect.width;w++)
                        {
                            totalNumber+=image.data[h*w + w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
            }
            ui->light_are_averige_value_lineEdit->setText (QString::number (totalNumber/normalNumber));
        }
    }
    //    namedWindow("result", 1);
    //    imshow("result", image);
    QImage resultImage = cvMat2QImage(image);
    QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
    ui->original_label->setPixmap(QPixmap::fromImage(newImg));
    return 0;
}

bool MainWindow::findCenterCircular()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            cv::circle(image, // 目标图像
                       cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                       recordPointA.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                       recordPointB.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                       recordPointC.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                       recordPointD.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                       recordPointE.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            vector<Point> areaPoint;
            areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
            areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
            areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
            areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
            areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
            Rect resultRect = cv::boundingRect(areaPoint);//求区域
            cv::circle(image, // 目标图像
                       cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                       (resultRect.width/2+resultRect.height/2)/2, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            ui->center_X_value_lineEdit->setText (QString::number (recordPointC.x));
            ui->center_Y_value_lineEdit->setText (QString::number (recordPointC.y));
            ui->radius_value_lineEdit->setText (QString::number ((resultRect.width/2+resultRect.height/2)/2));;
            cv::circle(image, // 目标图像
                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                       recordPointC.width/2, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            long totalNumber = 0;
            for(int w = recordPointC.x; w< recordPointC.x +  recordPointC.width; w++ )
            {
                for(int h = recordPointC.y; h< recordPointC.y +  recordPointC.height; h++ )
                {
                    totalNumber += image.data[h*w + w];
                }
            }
            ui->center_circular_radius_value_lineEdit->setText (QString::number ((recordPointC.width+recordPointC.height)/4));
            ui->center_circular_measure_value_lineEdit->setText (QString::number (recordPointC.width*recordPointC.height));
            ui->center_circular_grau_value_lineEdit->setText (QString::number (totalNumber));
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(binariedImg);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::fiveCircularGray()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            ui->max_lineEdit->setText (QString::number (maxThreshold));
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number < 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();

                return 0;
            }

            cv::circle(image, // 目标图像
                       cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                       recordPointA.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                       recordPointB.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                       recordPointC.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                       recordPointD.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                       recordPointE.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            //            vector<Point> areaPoint;
            //            areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
            //            areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
            //            areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
            //            areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
            //            areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
            //            Rect resultRect = cv::boundingRect(areaPoint);//求区域
            //            cv::circle(image, // 目标图像
            //                       cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
            //                       (resultRect.width/2+resultRect.height/2)/2, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //            ui->center_X_value_lineEdit->setText (QString::number (recordPointC.x));
            //            ui->center_Y_value_lineEdit->setText (QString::number (recordPointC.y));
            //            ui->radius_value_lineEdit->setText (QString::number ((resultRect.width/2+resultRect.height/2)/2));;
            //            cv::circle(image, // 目标图像
            //                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
            //                       recordPointC.width/2, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //
            //灰度总和
            long totalNumber = 0;
            int disNumber = 0;
            int garyNumber = 0;
            //
            int centerNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointA.x + recordPointA.width/2)),2 )+ pow(abs(513 - (recordPointA.y + recordPointA.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointA.width/2 + recordPointA.height/2)/2;
                for(int w = recordPointA.x; w< recordPointA.x +  recordPointA.width; w++ )
                {
                    for(int h = recordPointA.y; h< recordPointA.y +  recordPointA.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointA.x + recordPointA.width/2)),2 )+ pow(abs(h - (recordPointA.y + recordPointA.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_A->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(513 - (recordPointB.y + recordPointB.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointB.width/2 + recordPointB.height/2)/2;
                for(int w = recordPointB.x; w< recordPointB.x +  recordPointB.width; w++ )
                {
                    for(int h = recordPointB.y; h< recordPointB.y +  recordPointB.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(h - (recordPointB.y + recordPointB.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_B->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(513 - (recordPointC.y + recordPointC.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointC.width/2 + recordPointC.height/2)/2;
                for(int w = recordPointC.x; w< recordPointC.x +  recordPointC.width; w++ )
                {
                    for(int h = recordPointC.y; h< recordPointC.y +  recordPointC.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(h - (recordPointC.y + recordPointC.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_C->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointD.x + recordPointD.width/2)),2 )+ pow(abs(513 - (recordPointD.y + recordPointD.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointD.width/2 + recordPointD.height/2)/2;
                for(int w = recordPointD.x; w< recordPointD.x +  recordPointD.width; w++ )
                {
                    for(int h = recordPointD.y; h< recordPointD.y +  recordPointD.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointD.x + recordPointD.width/2)),2 )+ pow(abs(h - (recordPointD.y + recordPointD.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_D->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointE.x + recordPointE.width/2)),2 )+ pow(abs(513 - (recordPointE.y + recordPointE.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointE.width/2 + recordPointE.height/2)/2;
                for(int w = recordPointE.x; w< recordPointE.x +  recordPointE.width; w++ )
                {
                    for(int h = recordPointE.y; h< recordPointE.y +  recordPointE.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointE.x + recordPointE.width/2)),2 )+ pow(abs(h - (recordPointE.y + recordPointE.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_E->setText (QString::number (totalNumber));
            }
            //
            ui->center_circular_measure_value_lineEdit->setText (QString::number (garyNumber));
            ui->center_circular_grau_value_lineEdit->setText (QString::number (totalNumber));
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::fixedCenter()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int totalNumber = 0;
            for (int h = 508 - 50;h < 508 + 50;h++)
            {
                for (int w = 634 - 50;w < 634 + 50;w++){
                    totalNumber += image.data[h*w + w];
                    image.data[h*1280 + w] = 100;
                }
            }
            ui->fixed_center_lineEdit->setText (QString::number (totalNumber));
            //显示
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::centerCircularGray()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            ui->max_lineEdit->setText (QString::number (maxThreshold));
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number < 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();

                return 0;
            }

            //            cv::circle(image, // 目标图像
            //                       cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
            //                       recordPointA.height/2-10, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //            cv::circle(image, // 目标图像
            //                       cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
            //                       recordPointB.height/2-10, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //            cv::circle(image, // 目标图像
            //                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
            //                       recordPointC.height/2-10, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //            cv::circle(image, // 目标图像
            //                       cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
            //                       recordPointD.height/2-10, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //            cv::circle(image, // 目标图像
            //                       cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
            //                       recordPointE.height/2-10, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //            vector<Point> areaPoint;
            //            areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
            //            areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
            //            areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
            //            areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
            //            areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
            //            Rect resultRect = cv::boundingRect(areaPoint);//求区域
            //            cv::circle(image, // 目标图像
            //                       cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
            //                       (resultRect.width/2+resultRect.height/2)/2, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //            ui->center_X_value_lineEdit->setText (QString::number (recordPointC.x));
            //            ui->center_Y_value_lineEdit->setText (QString::number (recordPointC.y));
            //            ui->radius_value_lineEdit->setText (QString::number ((resultRect.width/2+resultRect.height/2)/2));;
            //            cv::circle(image, // 目标图像
            //                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
            //                       recordPointC.width/2, // 半径
            //                       10, // 颜色（这里用黑色）
            //                       2); // 厚
            //
            //灰度总和
            long totalNumber = 0;
            int disNumber = 0;
            int garyNumber = 0;
            //
            int centerNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointA.x + recordPointA.width/2)),2 )+ pow(abs(513 - (recordPointA.y + recordPointA.height/2)),2));
            if(centerNumber < 20)
            {
                //灰度总和
                int radius = (recordPointA.width/2 + recordPointA.height/2)/2;
                for(int w = recordPointA.x; w< recordPointA.x +  recordPointA.width; w++ )
                {
                    for(int h = recordPointA.y; h< recordPointA.y +  recordPointA.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointA.x + recordPointA.width/2)),2 )+ pow(abs(h - (recordPointA.y + recordPointA.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_A->setText (QString::number (totalNumber));
                ui->center_circular_radius_value_lineEdit->setText (QString::number ((recordPointA.width+recordPointA.height)/4));
            }
            centerNumber = sqrt(pow(abs(647 - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(513 - (recordPointB.y + recordPointB.height/2)),2));
            if(centerNumber < 20)
            {
                //灰度总和
                int radius = (recordPointB.width/2 + recordPointB.height/2)/2;
                for(int w = recordPointB.x; w< recordPointB.x +  recordPointB.width; w++ )
                {
                    for(int h = recordPointB.y; h< recordPointB.y +  recordPointB.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(h - (recordPointB.y + recordPointB.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_B->setText (QString::number (totalNumber));
                ui->center_circular_radius_value_lineEdit->setText (QString::number ((recordPointB.width+recordPointB.height)/4));
            }
            centerNumber = sqrt(pow(abs(647 - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(513 - (recordPointC.y + recordPointC.height/2)),2));
            if(centerNumber < 20)
            {
                //灰度总和
                int radius = (recordPointC.width/2 + recordPointC.height/2)/2;
                for(int w = recordPointC.x; w< recordPointC.x +  recordPointC.width; w++ )
                {
                    for(int h = recordPointC.y; h< recordPointC.y +  recordPointC.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(h - (recordPointC.y + recordPointC.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_C->setText (QString::number (totalNumber));
                ui->center_circular_radius_value_lineEdit->setText (QString::number ((recordPointC.width+recordPointC.height)/4));
            }

            centerNumber = sqrt(pow(abs(647 - (recordPointD.x + recordPointD.width/2)),2 )+ pow(abs(513 - (recordPointD.y + recordPointD.height/2)),2));
            if(centerNumber < 20)
            {
                //灰度总和
                int radius = (recordPointD.width/2 + recordPointD.height/2)/2;
                for(int w = recordPointD.x; w< recordPointD.x +  recordPointD.width; w++ )
                {
                    for(int h = recordPointD.y; h< recordPointD.y +  recordPointD.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointD.x + recordPointD.width/2)),2 )+ pow(abs(h - (recordPointD.y + recordPointD.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_D->setText (QString::number (totalNumber));
                ui->center_circular_radius_value_lineEdit->setText (QString::number ((recordPointD.width+recordPointD.height)/4));
            }
            centerNumber = sqrt(pow(abs(647 - (recordPointE.x + recordPointE.width/2)),2 )+ pow(abs(513 - (recordPointE.y + recordPointE.height/2)),2));
            if(centerNumber < 20)
            {
                //灰度总和
                int radius = (recordPointE.width/2 + recordPointE.height/2)/2;
                for(int w = recordPointE.x; w< recordPointE.x +  recordPointE.width; w++ )
                {
                    for(int h = recordPointE.y; h< recordPointE.y +  recordPointE.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointE.x + recordPointE.width/2)),2 )+ pow(abs(h - (recordPointE.y + recordPointE.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_E->setText (QString::number (totalNumber));
                ui->center_circular_radius_value_lineEdit->setText (QString::number ((recordPointE.width+recordPointE.height)/4));
            }
            //
            ui->center_circular_measure_value_lineEdit->setText (QString::number (garyNumber));
            ui->center_circular_grau_value_lineEdit->setText (QString::number (totalNumber));
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::fiveLightGray()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            ui->max_lineEdit->setText (QString::number (maxThreshold));
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number < 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }

            cv::circle(image, // 目标图像
                       cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                       recordPointA.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            qDebug()<<"recordPointA_x:"<<recordPointA.x + recordPointA.width/2;
            qDebug()<<"recordPointA_y:"<<recordPointA.y + recordPointA.height/2;
            qDebug()<<"recordPointA_W:"<<recordPointA.width;
            qDebug()<<"recordPointA_H:"<<recordPointA.height;
            cv::circle(image, // 目标图像
                       cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                       recordPointB.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            qDebug()<<"recordPointB_x:"<<recordPointB.x + recordPointB.width/2;
            qDebug()<<"recordPointB_y:"<<recordPointB.y + recordPointB.height/2;
            qDebug()<<"recordPointB_W:"<<recordPointB.width;
            qDebug()<<"recordPointB_H:"<<recordPointB.height;
            cv::circle(image, // 目标图像
                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                       recordPointC.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            qDebug()<<"recordPointC_x:"<<recordPointC.x + recordPointC.width/2;
            qDebug()<<"recordPointC_y:"<<recordPointC.y + recordPointC.height/2;
            qDebug()<<"recordPointC_W:"<<recordPointC.width;
            qDebug()<<"recordPointC_H:"<<recordPointC.height;
            cv::circle(image, // 目标图像
                       cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                       recordPointD.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            qDebug()<<"recordPointD_x:"<<recordPointD.x + recordPointD.width/2;
            qDebug()<<"recordPointD_y:"<<recordPointD.y + recordPointD.height/2;
            qDebug()<<"recordPointD_W:"<<recordPointD.width;
            qDebug()<<"recordPointD_H:"<<recordPointD.height;
            cv::circle(image, // 目标图像
                       cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                       recordPointE.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            qDebug()<<"recordPointE_x:"<<recordPointE.x + recordPointE.width/2;
            qDebug()<<"recordPointE_y:"<<recordPointE.y + recordPointE.height/2;
            qDebug()<<"recordPointE_W:"<<recordPointE.width;
            qDebug()<<"recordPointE_H:"<<recordPointE.height;

            //
            //灰度总和
            long totalNumber = 0;
            int disNumber = 0;
            int garyNumber = 0;
            //
            int centerNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointA.x + recordPointA.width/2)),2 )+ pow(abs(513 - (recordPointA.y + recordPointA.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointA.width/2 + recordPointA.height/2)/2;
                for(int w = recordPointA.x; w< recordPointA.x +  recordPointA.width; w++ )
                {
                    for(int h = recordPointA.y; h< recordPointA.y +  recordPointA.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointA.x + recordPointA.width/2)),2 )+ pow(abs(h - (recordPointA.y + recordPointA.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_A->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(513 - (recordPointB.y + recordPointB.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointB.width/2 + recordPointB.height/2)/2;
                for(int w = recordPointB.x; w< recordPointB.x +  recordPointB.width; w++ )
                {
                    for(int h = recordPointB.y; h< recordPointB.y +  recordPointB.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(h - (recordPointB.y + recordPointB.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_B->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(513 - (recordPointC.y + recordPointC.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointC.width/2 + recordPointC.height/2)/2;
                for(int w = recordPointC.x; w< recordPointC.x +  recordPointC.width; w++ )
                {
                    for(int h = recordPointC.y; h< recordPointC.y +  recordPointC.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(h - (recordPointC.y + recordPointC.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_C->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointD.x + recordPointD.width/2)),2 )+ pow(abs(513 - (recordPointD.y + recordPointD.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointD.width/2 + recordPointD.height/2)/2;
                for(int w = recordPointD.x; w< recordPointD.x +  recordPointD.width; w++ )
                {
                    for(int h = recordPointD.y; h< recordPointD.y +  recordPointD.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointD.x + recordPointD.width/2)),2 )+ pow(abs(h - (recordPointD.y + recordPointD.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_D->setText (QString::number (totalNumber));
            }
            totalNumber = 0;
            centerNumber = sqrt(pow(abs(647 - (recordPointE.x + recordPointE.width/2)),2 )+ pow(abs(513 - (recordPointE.y + recordPointE.height/2)),2));
            if(1)
            {
                //灰度总和
                int radius = (recordPointE.width/2 + recordPointE.height/2)/2;
                for(int w = recordPointE.x; w< recordPointE.x +  recordPointE.width; w++ )
                {
                    for(int h = recordPointE.y; h< recordPointE.y +  recordPointE.height; h++ )
                    {
                        disNumber = sqrt(pow(abs(w - (recordPointE.x + recordPointE.width/2)),2 )+ pow(abs(h - (recordPointE.y + recordPointE.height/2)),2));
                        if(disNumber < radius){
                            garyNumber++;
                            totalNumber+= image.data[1280*h+w];
                            image.data[1280*h+w] = 100;
                        }
                    }
                }
                ui->lineEdit_E->setText (QString::number (totalNumber));
            }
            //
            ui->center_circular_measure_value_lineEdit->setText (QString::number (garyNumber));
            ui->center_circular_grau_value_lineEdit->setText (QString::number (totalNumber));
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::centerCircularGrayTotal()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;

            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }

            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            cv::circle(image, // 目标图像
                       cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                       recordPointA.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                       recordPointB.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                       recordPointC.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                       recordPointD.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            cv::circle(image, // 目标图像
                       cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                       recordPointE.height/2-10, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            vector<Point> areaPoint;
            areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
            areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
            areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
            areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
            areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
            Rect resultRect = cv::boundingRect(areaPoint);//求区域
            cv::circle(image, // 目标图像
                       cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                       (resultRect.width/2+resultRect.height/2)/2, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            ui->center_X_value_lineEdit->setText (QString::number (recordPointC.x));
            ui->center_Y_value_lineEdit->setText (QString::number (recordPointC.y));
            ui->radius_value_lineEdit->setText (QString::number ((resultRect.width/2+resultRect.height/2)/2));;
            cv::circle(image, // 目标图像
                       cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                       recordPointC.width/2, // 半径
                       10, // 颜色（这里用黑色）
                       2); // 厚
            long totalNumber = 0;
            for(int w = recordPointC.x; w< recordPointC.x +  recordPointC.width; w++ )
            {
                for(int h = recordPointC.y; h< recordPointC.y +  recordPointC.height; h++ )
                {
                    totalNumber += image.data[h*w + w];
                }
            }
            ui->center_circular_measure_value_lineEdit->setText (QString::number (recordPointC.width*recordPointC.height));
            ui->center_circular_grau_value_lineEdit->setText (QString::number (totalNumber));
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::lockImagePixel()
{
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg *.bmp");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            namedWindow("result", WINDOW_NORMAL);
            imshow("result", image);
        }
    }
    return 0;
}

static quint8 v4l2ConstantFrame[INPUT_VIDEO_W*INPUT_VIDEO_H*1];
static quint8 v4l2ConstantFrame1[INPUT_VIDEO_W*INPUT_VIDEO_H*1];

bool MainWindow::lockRawLmagePixel()
{
    memset(v4l2ConstantFrame,0,sizeof(v4l2ConstantFrame));
    //if path is empty. set it
    fileName.clear ();
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.raw");

    if (fileName.isEmpty())
        qDebug() << "imagePath = " << fileName;
    QFile raw(fileName);
    if  (!raw.open(QIODevice::ReadWrite)) {
        qDebug() << "file open error";
    }
    qDebug() << "file open success";
    raw.read((char*)v4l2ConstantFrame,1024*1280);
    memcpy(v4l2ConstantFrame1,v4l2ConstantFrame,1024*1280);
    cv::Mat mat_B(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1);
    memcpy(mat_B.data, v4l2ConstantFrame, 1024*1280);
    if(!mat_B.empty ()){
        namedWindow("result+B", WINDOW_NORMAL);
        imshow("result+B", mat_B);
    }else
    {
        qDebug()<<"error:";
    }
    return 0;
}

int bmp_write(unsigned char *image, int xsize, int ysize, char *filename)
{
    unsigned char header[54] = {
        0x42, 0x4d, 0, 0, 0, 0, 0, 0, 0, 0,
        54, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 32, 0,//(或者24)
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0
    };

    long file_size = (long)xsize * (long)ysize * 4 + 54;//(或者3)
    header[2] = (unsigned char)(file_size &0x000000ff);
    header[3] = (file_size >> 8) & 0x000000ff;
    header[4] = (file_size >> 16) & 0x000000ff;
    header[5] = (file_size >> 24) & 0x000000ff;
    long width = xsize;
    header[18] = width & 0x000000ff;
    header[19] = (width >> 8) &0x000000ff;
    header[20] = (width >> 16) &0x000000ff;
    header[21] = (width >> 24) &0x000000ff;
    long height = ysize;
    header[22] = height &0x000000ff;
    header[23] = (height >> 8) &0x000000ff;
    header[24] = (height >> 16) &0x000000ff;
    header[25] = (height >> 24) &0x000000ff;
    /////////////////////////////////////////////////////////////////




    /////////////////////////////////////////////////////////////////
    char fname_bmp[128];
    sprintf(fname_bmp, "%s.bmp", filename);
    qDebug() << "bmp success"<<fname_bmp;
    FILE *fp;
    if (!(fp = fopen(fname_bmp, "wb")))
        return -1;
    fwrite(header, sizeof(unsigned char), 54, fp);
    fwrite(image, sizeof(unsigned char), (size_t)(long)xsize * ysize * 4, fp);//(或者3)
    qDebug() << "bmp success";
    fclose(fp);
    return 0;
}

int MainWindow::raw2rgb(void)
{
    memset(v4l2ConstantFrame,0,sizeof(v4l2ConstantFrame));
    //if path is empty. set it
    fileName.clear ();
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.raw");

    if (fileName.isEmpty())
        qDebug() << "imagePath = " << fileName;
    QFile raw(fileName);
    if  (!raw.open(QIODevice::ReadWrite)) {
        qDebug() << "file open error";
    }
    qDebug() << "file open success";
    raw.read((char*)v4l2ConstantFrame,1024*1280);
    memcpy(v4l2ConstantFrame1,v4l2ConstantFrame,1024*1280);
    bmp_write((unsigned char*)v4l2ConstantFrame,1280,1024,"C:\\Users\\Administrator\\Desktop\\test");
    //////////////////////////////////
    int ret = 0, width = 1280, height = 1024;

    IplImage *pBayerData = cvCreateImage(cvSize(width,height), 16, 1);
    IplImage *pRgbDataInt16 = cvCreateImage(cvSize(width,height),16,3);
    IplImage *pRgbDataInt8 = cvCreateImage(cvSize(width,height),8,3);
    memcpy(pBayerData->imageData, (char *)v4l2ConstantFrame, width*height*sizeof(unsigned short));
    cvCvtColor(pBayerData, pRgbDataInt16, CV_BayerRG2BGR);

    /*将16bit数据转换为8bit*/
    cvConvertScale(pRgbDataInt16, pRgbDataInt8, 0.015625, 0);

    cvNamedWindow("rgb", 1);
    cvShowImage("rgb", pRgbDataInt8);
    cvWaitKey(0);

    cvDestroyWindow("rgb");
    cvReleaseImage(&pBayerData);
    cvReleaseImage(&pRgbDataInt8);
    cvReleaseImage(&pRgbDataInt16);


    return 0;
}

bool MainWindow::lensMeasureFunOne()
{
    QDateTime time = QDateTime::currentDateTime();
    QString str;
    str = time.toString(" yyyy-MM-dd \n   hh:mm:ss");
    qDebug()<<str;
    double angle = 0;
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image
            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            if(number == 4){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                int n_horizontal = sqrt(pow(abs(recordPointA.x - recordPointC.x),2) + pow(abs(recordPointA.y - recordPointC.y),2));//水平
                int n_vertical = n_horizontal;//垂直
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           n_horizontal/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                ui->horizontal_lineEdit->setText (QString::number (n_horizontal));//水平拉伸
                ui->vertical_lineEdit->setText (QString::number (n_vertical));//垂直拉伸
                //12.55比例球镜//计算球镜值
                int basicNumber = (horizontal_value + vertical_value)/2;
                int measureNumber = (n_horizontal + n_vertical)/2;
                float resultFsNumber  = ( basicNumber - measureNumber) / 12.55;
                ui->fs_lineEdit->setText (QString::number (resultFsNumber));
                float x = (center_X - (recordPointB.x + recordPointB.width/2))/640;
                float y =  (center_Y - (recordPointB.y + recordPointB.height/2))/512;
                ui->center_point_x_lineEdit->setText (QString::number (x));//x坐标
                ui->center_point_y_lineEdit->setText (QString::number (y));//y坐标
                ui->measure_X_value_lineEdit->setText (QString::number (recordPointB.x + recordPointB.width/2));//x坐标
                ui->measure_Y_value_lineEdit->setText (QString::number (recordPointB.y + recordPointB.height/2));//y坐标
                //棱镜30.34
                //计算棱镜30.34
                float basicPrism = sqrt(pow(abs(recordPointB.x + recordPointB.width/2 - center_X),2 )+ pow(abs(recordPointB.y + recordPointB.height/2 - center_Y),2));
                ui->fp_lineEdit->setText (QString::number (basicPrism/30.34));
            }
            if(number == 5){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                           recordPointE.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                vector<Point> areaPoint;
                areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
                areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
                areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
                areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
                areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
                Rect resultRect = cv::boundingRect(areaPoint);//求区域
                cv::circle(image, // 目标图像
                           cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                           (resultRect.width/2+resultRect.height/2)/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.width/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                int n_horizontal = sqrt(pow(abs(recordPointB.x - recordPointD.x),2) + pow(abs(recordPointB.y - recordPointD.y),2));//水平
                int n_vertical = sqrt(pow(abs(recordPointA.x - recordPointE.x),2 )+ pow(abs(recordPointA.y - recordPointE.y),2));//垂直
                ui->horizontal_lineEdit->setText (QString::number (n_horizontal));//水平拉伸
                ui->vertical_lineEdit->setText (QString::number (n_vertical));//垂直拉伸
                //12.55比例球镜//计算球镜值
                int basicNumber = (horizontal_value + vertical_value)/2;
                int measureNumber = (n_horizontal + n_vertical)/2;
                float resultFsNumber  = ( basicNumber - measureNumber) / 0.86*10;
                ui->fs_lineEdit->setText (QString::number (resultFsNumber));
                ui->measure_X_value_lineEdit->setText (QString::number (resultRect.x + resultRect.width/2));//x坐标
                ui->measure_Y_value_lineEdit->setText (QString::number (resultRect.y + resultRect.height/2));//x坐标
                float x = (center_X - (resultRect.x + resultRect.width/2))/640;
                float y =  (center_Y - (resultRect.y + resultRect.height/2))/512;
                ui->center_point_x_lineEdit->setText (QString::number (x));//x坐标
                ui->center_point_y_lineEdit->setText (QString::number (y));//y坐标
                //轴位角
                double axis_x =   (resultRect.x + resultRect.width/2) - center_X;
                double axis_y =   (resultRect.y + resultRect.height/2) - center_Y;
                if(axis_y  < 0)
                {
                    if(axis_x > 0)
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415;
                    }
                    else
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 90;
                    }
                }
                else
                {
                    if(axis_x > 0)
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 270;
                    }
                    else
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 180;
                    }
                }
                ui->fp_jiao_lineEdit->setText (QString::number (angle));
                //计算棱镜30.34
                float basicPrism = sqrt(pow(abs(resultRect.x + resultRect.width/2 - center_X),2 )+ pow(abs(resultRect.y + resultRect.height/2 - center_Y),2));
                ui->fp_lineEdit->setText (QString::number (basicPrism/30.34));
            }
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    QDateTime times = QDateTime::currentDateTime();
    QString strs;
    strs = times.toString(" yyyy-MM-dd \n   hh:mm:ss");
    qDebug()<<strs;
    return 0;
}

bool MainWindow::lensMeasureFunTwo()
{
    double angle = 0;
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image
            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            if(number == 4){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                int n_horizontal = sqrt(pow(abs(recordPointA.x - recordPointC.x),2) + pow(abs(recordPointA.y - recordPointC.y),2));//水平
                int n_vertical = n_horizontal;//垂直
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           n_horizontal/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                ui->horizontal_lineEdit->setText (QString::number (n_horizontal));//水平拉伸
                ui->vertical_lineEdit->setText (QString::number (n_vertical));//垂直拉伸
                //12.55比例球镜//计算球镜值
                int basicNumber = (horizontal_value + vertical_value)/2;
                int measureNumber = (n_horizontal + n_vertical)/2;
                float resultFsNumber  = ( basicNumber - measureNumber) / 0.86*10;
                ui->fs_lineEdit->setText (QString::number (resultFsNumber));
                float x = (center_X - (recordPointB.x + recordPointB.width/2))/640;
                float y =  (center_Y - (recordPointB.y + recordPointB.height/2))/512;
                ui->center_point_x_lineEdit->setText (QString::number (x));//x坐标
                ui->center_point_y_lineEdit->setText (QString::number (y));//y坐标
                ui->measure_X_value_lineEdit->setText (QString::number (recordPointB.x + recordPointB.width/2));//x坐标
                ui->measure_Y_value_lineEdit->setText (QString::number (recordPointB.y + recordPointB.height/2));//y坐标
                //棱镜30.34
                //计算棱镜30.34
                float basicPrism = sqrt(pow(abs(recordPointB.x + recordPointB.width/2 - center_X),2 )+ pow(abs(recordPointB.y + recordPointB.height/2 - center_Y),2));
                ui->fp_lineEdit->setText (QString::number (basicPrism/30.34));
            }
            if(number == 5){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                           recordPointE.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                vector<Point> areaPoint;
                areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
                areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
                areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
                areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
                areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
                Rect resultRect = cv::boundingRect(areaPoint);//求区域
                cv::circle(image, // 目标图像
                           cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                           (resultRect.width/2+resultRect.height/2)/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.width/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                int n_horizontal = sqrt(pow(abs(recordPointB.x - recordPointD.x),2) + pow(abs(recordPointB.y - recordPointD.y),2));//水平
                int n_vertical = sqrt(pow(abs(recordPointA.x - recordPointE.x),2 )+ pow(abs(recordPointA.y - recordPointE.y),2));//垂直
                ui->horizontal_lineEdit->setText (QString::number (n_horizontal));//水平拉伸
                ui->vertical_lineEdit->setText (QString::number (n_vertical));//垂直拉伸
                //12.55比例球镜//计算球镜值
                int basicNumber = (horizontal_value + vertical_value)/2;
                int measureNumber = (n_horizontal + n_vertical)/2;
                float resultFsNumber  = ( basicNumber - measureNumber) / 12.55;
                ui->fs_lineEdit->setText (QString::number (resultFsNumber));
                ui->measure_X_value_lineEdit->setText (QString::number (resultRect.x + resultRect.width/2));//x坐标
                ui->measure_Y_value_lineEdit->setText (QString::number (resultRect.y + resultRect.height/2));//x坐标
                float x = (center_X - (resultRect.x + resultRect.width/2))/640;
                float y =  (center_Y - (resultRect.y + resultRect.height/2))/512;
                ui->center_point_x_lineEdit->setText (QString::number (x));//x坐标
                ui->center_point_y_lineEdit->setText (QString::number (y));//y坐标
                //轴位角
                double axis_x =   (resultRect.x + resultRect.width/2) - center_X;
                double axis_y =   (resultRect.y + resultRect.height/2) - center_Y;
                if(axis_y  < 0)
                {
                    if(axis_x > 0)
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415;
                    }
                    else
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 90;
                    }
                }
                else
                {
                    if(axis_x > 0)
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 270;
                    }
                    else
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 180;
                    }
                }
                ui->fp_jiao_lineEdit->setText (QString::number (angle));
                //计算棱镜30.34
                float basicPrism = sqrt(pow(abs(resultRect.x + resultRect.width/2 - center_X),2 )+ pow(abs(resultRect.y + resultRect.height/2 - center_Y),2));
                ui->fp_lineEdit->setText (QString::number (basicPrism/30.34));
            }
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

void MainWindow::deleteAllLineEdit()
{
    ui->all_image_average_lineEdit->setText ("0");
    ui->lineEdit->setText ("0");
    ui->light_are_lineEdit->setText ("0");
    ui->light_are_average_lineEdit->setText ("0");
    ui->total_255_number_lineEdit->setText ("0");
    ui->total_number_label->setText ("0");
    ui->pixel_value_label->setText ("0");
    ui->gray_value_label->setText ("0");
    ui->distance_label->setText ("0");
    ui->light_are_averige_value_lineEdit->setText ("0");
    ui->center_circular_grau_value_lineEdit->setText ("0");
    ui->center_circular_measure_value_lineEdit->setText ("0");
    ui->radius_value_lineEdit->setText ("0");
}

bool MainWindow::autoCorrectBoot()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    //fileName = QFileDialog::getOpenFileName(NULL,"open image","F://testImageGray//testImage//","*.jpg");
    fileName = QApplication::applicationDirPath ();
    fileName.append ("/resource/0.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        qDebug()<<image.empty ();
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image
            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }

            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<5){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            /////////////////////////////////////////////////////////////矫正透过率基准/////////////////////////////
            long totalNumber = 0;
            int normalNumber = 0;
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                    normalNumber++;
                    cv::circle(image, // 目标图像
                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                               (rect.height/2 + rect.width/2)/2, // 半径
                               50, // 颜色（这里用黑色）
                               2); // 厚
                    for(int  h = rect.y;h < rect.y + rect.height;h++)
                    {
                        for(int  w = rect.x;w < rect.x + rect.width;w++)
                        {
                            totalNumber+=image.data[h*w + w];
                        }
                    }
                }
            }
            recordCorrectTransmiss = totalNumber/normalNumber;
            ui->light_are_averige_value_lineEdit->setText (QString::number (recordCorrectTransmiss));


            if(number == 5){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                           recordPointE.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                vector<Point> areaPoint;
                areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
                areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
                areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
                areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
                areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
                Rect resultRect = cv::boundingRect(areaPoint);//求区域
                cv::circle(image, // 目标图像
                           cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                           (resultRect.width/2+resultRect.height/2)/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                ui->correct_four_circle_radius_lineEdit->setText (QString::number ( (resultRect.width/2+resultRect.height/2)/2));
                ui->correct_X_value_lineEdit->setText (QString::number (resultRect.x + resultRect.width/2));
                ui->correct_Y_value_lineEdit->setText (QString::number (resultRect.y + resultRect.height/2));
                center_X = resultRect.x + resultRect.width/2;
                center_Y = resultRect.y + resultRect.height/2;
                int horizontal = sqrt(pow(abs(recordPointB.x - recordPointD.x),2) + pow(abs(recordPointB.y - recordPointD.y),2));//水平
                int vertical = sqrt(pow(abs(recordPointA.x - recordPointE.x),2 )+ pow(abs(recordPointA.y - recordPointE.y),2));//垂直
                ui->correct_horizontal_lineEdit->setText (QString::number (horizontal));
                ui->correct_vertical_lineEdit->setText (QString::number (vertical));
                horizontal_value = horizontal;
                vertical_value  = vertical;
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.width/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
            }
            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::allCirGrayAverige()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //求最大值
            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"求最大值:"<<maxThreshold;
            //Binarization process of the cropped image
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            long totalGrayNumber = 0;
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                //qDebug() << QString("x = %1,y = %2,width = %3, height = %4").arg(rect.x).arg(rect.y).arg(rect.width).arg(rect.height);//打印位置高宽
                //画圆
                if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                    //                    cv::circle(image, // 目标图像
                    //                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                    //                               (rect.height/2 + rect.width/2)/2, // 半径
                    //                               50, // 颜色（这里用黑色）
                    //                               2); // 厚
                    //                    for(int h = 0;h < rect.height;h++)
                    //                    {
                    //                        for(int w = 0;w < rect.width;w++)
                    //                        {
                    //                            int disNumber = sqrt(pow(abs((rect.x+w) - (rect.x + rect.width/2)),2 )+ pow(abs((rect.y+h) - (rect.y + rect.height/2)),2));
                    //                            if(disNumber < (rect.height + rect.width) /2){
                    //                                totalGrayNumber+= image.data[1280*(rect.y+h)+(rect.x+w)];
                    //                                image.data[1280*(rect.y+h)+(rect.x+w)] = 100;
                    //                            }
                    //                        }
                    //                    }

                    int garyNumber = 0;
                    int radius = (rect.width/2 + rect.height/2)/2;
                    for(int h = rect.y + rect.height/2 - radius;h < rect.y + rect.height/2 + radius;h++)
                    {
                        for(int w = rect.x + rect.width/2 - radius;w < rect.x + rect.width/2 + radius;w++)
                        {
                            int disNumber = sqrt(pow(abs(w - (rect.x + rect.width/2)),2 )+ pow(abs(h - (rect.y + rect.height/2)),2));
                            if(disNumber < radius){
                                garyNumber++;
                                totalGrayNumber+= image.data[1280*h+w];
                                image.data[1280*h+w] = 100;
                            }
                        }
                    }
                    ui->all_cir_averige_lineEdit->setText (QString::number (totalGrayNumber/garyNumber));
                }
            }
        }
    }
    //    namedWindow("result", 1);
    //    imshow("result", image);
    QImage resultImage = cvMat2QImage(image);
    QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
    ui->original_label->setPixmap(QPixmap::fromImage(newImg));
    return 0;
}

void MainWindow::ellipseFitting()
{
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    imageEllispe = imread(fileName.toStdString (), 0);
    if( imageEllispe.empty() )
    {
        qDebug() << "Couldn't open image " << fileName << "\nUsage: fitellipse <image_name>\n";
        return ;
    }
    namedWindow("result",0.5);
    // Create toolbars. HighGUI use.
    createTrackbar( "threshold", "result", &sliderPos, 255, &processImage );
}

bool MainWindow::correctOpenBoot()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;

            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }

            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            int max_w = 0;
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            int number = 0;//记录个数
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<5){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            /////////////////////////////////////////////////////////////矫正透过率基准/////////////////////////////
            long totalNumber = 0;
            int normalNumber = 0;
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                    normalNumber++;
                    cv::circle(image, // 目标图像
                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                               (rect.height/2 + rect.width/2)/2, // 半径
                               50, // 颜色（这里用黑色）
                               2); // 厚
                    for(int  h = rect.y;h < rect.y + rect.height;h++)
                    {
                        for(int  w = rect.x;w < rect.x + rect.width;w++)
                        {
                            totalNumber+=image.data[h*w + w];
                        }
                    }
                }
            }
            recordCorrectTransmiss = totalNumber/normalNumber;
            ui->light_are_averige_value_lineEdit->setText (QString::number (recordCorrectTransmiss));

            if(number == 5){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                           recordPointE.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                vector<Point> areaPoint;
                areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
                areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
                areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
                areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
                areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
                Rect resultRect = cv::boundingRect(areaPoint);//求区域
                cv::circle(image, // 目标图像
                           cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                           (resultRect.width/2+resultRect.height/2)/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                ui->correct_four_circle_radius_lineEdit->setText (QString::number ( (resultRect.width/2+resultRect.height/2)/2));
                ui->correct_X_value_lineEdit->setText (QString::number (resultRect.x + resultRect.width/2));
                ui->correct_Y_value_lineEdit->setText (QString::number (resultRect.y + resultRect.height/2));
                center_X = resultRect.x + resultRect.width/2;
                center_Y = resultRect.y + resultRect.height/2;
                int horizontal = sqrt(pow(abs(recordPointB.x - recordPointD.x),2) + pow(abs(recordPointB.y - recordPointD.y),2));//水平
                int vertical = sqrt(pow(abs(recordPointA.x - recordPointE.x),2 )+ pow(abs(recordPointA.y - recordPointE.y),2));//垂直
                ui->correct_horizontal_lineEdit->setText (QString::number (horizontal));
                ui->correct_vertical_lineEdit->setText (QString::number (vertical));
                horizontal_value = horizontal;
                vertical_value  = vertical;
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.width/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
            }

            //显示到屏幕
            //            namedWindow("result", 1);
            //            imshow("result", image);
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::columnLensMeasureOne()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    vector<Point> ellipseVector;//椭圆点集
    int disCenterOfCircle = 0;//圆心距离
    int  fourFlogEllispeDistance = 0;//四大flog点半径
    int max_w = 0;//最大灰度
    int number = 0;//记录个数
    int distance_x = 0;
    int distance_y = 0;
    int distance_result = 0;//到边点距离
    int distance_Length = 0;//边长
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    QString fileDir = QApplication::applicationDirPath ();
    fileName = QFileDialog::getOpenFileName(NULL,"open image",fileDir,"*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //克隆图像
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<5){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            /////////////////////////////////////////椭圆拟合/////////////////////////////////////////////////
            distance_Length = sqrt(pow(abs((recordPointA.x + recordPointA.width/2) - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(recordPointA.y + recordPointA.height/2 - (recordPointB.y + recordPointB.height/2)),2));
            if(number == 5){
                for (int i = 0; i < contours.size(); i++) {
                    vector<Point> edgePoint = contours.at(i);
                    Rect rect = cv::boundingRect(edgePoint);//求区域
                    if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                        //到中心距离
                        disCenterOfCircle = 0;
                        disCenterOfCircle = sqrt(pow(abs((rect.x + rect.width/2) - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(rect.y + rect.height/2 - (recordPointC.y + recordPointC.height/2)),2));
                        //recordPointA
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointA.x + recordPointA.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointA.y + recordPointA.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointA.y + recordPointA.height/2) - (recordPointC.y + recordPointC.height/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance/2)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance)
                            {
                                if(abs(distance_result - distance_Length/4) < 15){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointA";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointB
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointB.x + recordPointB.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointB.y + recordPointB.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointB.x + recordPointB.width/2) - (recordPointC.x + recordPointC.width/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance/2)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance)
                            {
                                if(abs(distance_result - distance_Length/4) < 15){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointB";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointD
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointD.x + recordPointD.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointD.y + recordPointD.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointD.x + recordPointD.width/2) - (recordPointC.x + recordPointC.width/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance/2)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance)
                            {
                                if(abs(distance_result - distance_Length/4) < 15){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointD";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointE
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointE.x + recordPointE.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointE.y + recordPointE.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointE.y + recordPointE.height/2) - (recordPointC.y + recordPointC.height/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance/2)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance)
                            {
                                if(abs(distance_result - distance_Length/4) < 15){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointE";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                    }
                }
            }
            qDebug()<<"ellipseVector.size:"<<ellipseVector.size();
            if(ellipseVector.size () >= 6)
            {
                Mat pointsf;
                Mat(ellipseVector).convertTo(pointsf, CV_32F);
                RotatedRect box = fitEllipse(pointsf);
                ellipse(image, box, Scalar(100,80,255), 1, LINE_AA);
                ellipse(image, box.center, box.size*0.5f, box.angle, 0, 360, Scalar(255,255,0), 1, LINE_AA);
                Point2f vtx[4];
                box.points(vtx);
                //画边框

                for( int j = 0; j < 4; j++ ){
                    line(image, vtx[j], vtx[(j+1)%4], Scalar(255,255,0), 1, LINE_AA);
                }
                int lineLengthA = sqrt(pow(abs(vtx[0].x - vtx[1].x),2 )+ pow(abs(vtx[0].y - vtx[1].y),2 ));
                int lineLengthB = sqrt(pow(abs(vtx[1].x - vtx[2].x),2 )+ pow(abs(vtx[1].y - vtx[2].y),2 ));
                //                int lineLengthC = sqrt(pow(abs(vtx[2].x - vtx[3].x),2 )+ pow(abs(vtx[2].y - vtx[3].y),2 ));
                //                int lineLengthD = sqrt(pow(abs(vtx[0].x - vtx[3].x),2 )+ pow(abs(vtx[0].y - vtx[3].y),2 ));
                //计算棱镜度
                if(lineLengthA > lineLengthB)
                {
                    float fc_number = (lineLengthA - lineLengthB)/1.7*0.25;
                    ui->fc_lineEdit->setText (QString::number (fc_number));
                    ui->leng_length_lineEdit->setText (QString::number (lineLengthA));
                    ui->leng_short_lineEdit->setText (QString::number (lineLengthB));
                }else
                {
                    float fc_number = (lineLengthB - lineLengthA)/1.7*0.25;
                    ui->fc_lineEdit->setText (QString::number (fc_number));
                    ui->leng_length_lineEdit->setText (QString::number (lineLengthB));
                    ui->leng_short_lineEdit->setText (QString::number (lineLengthA));
                }
                //计算棱镜角度
                if(lineLengthA > lineLengthB)
                {
                    int axis_x = abs(vtx[0].x - vtx[1].x);
                    int axis_y = abs(vtx[0].y - vtx[1].y);
                    double angle_atanValue = 0;
                    if(axis_x != 0){
                        angle_atanValue = atan(axis_y/axis_x);
                    }
                    float angle = 0;
                    angle = angle_atanValue*180/3.1415;
                    ui->fc_jiao_lineEdit->setText (QString::number (180 - angle));
                }else
                {
                    int axis_x = abs(vtx[1].x - vtx[2].x);
                    int axis_y = abs(vtx[1].y - vtx[2].y);
                    double angle_atanValue = 0;
                    if(axis_x != 0){
                        angle_atanValue = atan(axis_y/axis_x);
                    }
                    float angle = 0;
                    angle = angle_atanValue*180/3.1415;
                    ui->fc_jiao_lineEdit->setText (QString::number (angle));
                }
            }
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::columnLensMeasureTwo()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    vector<Point> ellipseVector;//椭圆点集
    int disCenterOfCircle = 0;//圆心距离
    int  fourFlogEllispeDistance = 0;//四大flog点半径
    int max_w = 0;//最大灰度
    int number = 0;//记录个数
    int distance_x = 0;
    int distance_y = 0;
    int distance_result = 0;//到边点距离
    int distance_Length = 0;//边长
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    fileName = QFileDialog::getOpenFileName(NULL,"open image","F://testImageGray//testImage//","*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //克隆图像
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<5){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            /////////////////////////////////////////椭圆拟合/////////////////////////////////////////////////
            //计算边长
            distance_Length = sqrt(pow(abs((recordPointA.x + recordPointA.width/2) - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(recordPointA.y + recordPointA.height/2 - (recordPointB.y + recordPointB.height/2)),2));
            if(number == 5){
                for (int i = 0; i < contours.size(); i++) {
                    vector<Point> edgePoint = contours.at(i);
                    Rect rect = cv::boundingRect(edgePoint);//求区域
                    if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                        /////////////////////////////////////////////////////////分别计算到四个边点的距离//////////////////////////////////////////////////
                        //每个点到中心距离
                        disCenterOfCircle = 0;
                        disCenterOfCircle = sqrt(pow(abs((rect.x + rect.width/2) - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(rect.y + rect.height/2 - (recordPointC.y + recordPointC.height/2)),2));
                        //recordPointA
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointA.x + recordPointA.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointA.y + recordPointA.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointA.y + recordPointA.height/2) - (recordPointC.y + recordPointC.height/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance)//判断到圆心的距离大于四个点的距离一半
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance+distance_Length/4)//判断到圆心的距离小于四个点的距离
                            {
                                if(abs(distance_result - distance_Length/4) < 15){//距边点的值与边长的4分之一小于15个像素
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointA";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointB
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointB.x + recordPointB.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointB.y + recordPointB.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointB.x + recordPointB.width/2) - (recordPointC.x + recordPointC.width/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance+distance_Length/4)
                            {
                                if(abs(distance_result - distance_Length/4) < 15){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointB";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointD
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointD.x + recordPointD.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointD.y + recordPointD.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointD.x + recordPointD.width/2) - (recordPointC.x + recordPointC.width/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance + distance_Length/4)
                            {
                                if(abs(distance_result - distance_Length/4) < 15){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointD";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointE
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointE.x + recordPointE.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointE.y + recordPointE.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointE.y + recordPointE.height/2) - (recordPointC.y + recordPointC.height/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance + distance_Length/4)
                            {
                                if(abs(distance_result - distance_Length/4) < 15){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointE";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                    }
                }
            }
            qDebug()<<"ellipseVector.size:"<<ellipseVector.size();
            if(ellipseVector.size () >= 6)
            {
                Mat pointsf;
                Mat(ellipseVector).convertTo(pointsf, CV_32F);
                RotatedRect box = fitEllipse(pointsf);
                ellipse(image, box, Scalar(100,80,255), 1, LINE_AA);
                ellipse(image, box.center, box.size*0.5f, box.angle, 0, 360, Scalar(255,255,0), 1, LINE_AA);
                Point2f vtx[4];
                box.points(vtx);
                //画边框

                for( int j = 0; j < 4; j++ ){
                    line(image, vtx[j], vtx[(j+1)%4], Scalar(255,255,0), 1, LINE_AA);
                }
                int lineLengthA = sqrt(pow(abs(vtx[0].x - vtx[1].x),2 )+ pow(abs(vtx[0].y - vtx[1].y),2 ));
                int lineLengthB = sqrt(pow(abs(vtx[1].x - vtx[2].x),2 )+ pow(abs(vtx[1].y - vtx[2].y),2 ));
                //                int lineLengthC = sqrt(pow(abs(vtx[2].x - vtx[3].x),2 )+ pow(abs(vtx[2].y - vtx[3].y),2 ));
                //                int lineLengthD = sqrt(pow(abs(vtx[0].x - vtx[3].x),2 )+ pow(abs(vtx[0].y - vtx[3].y),2 ));
                //计算棱镜度
                if(lineLengthA > lineLengthB)
                {
                    float fc_number = (lineLengthA - lineLengthB)/1.1*0.1;
                    ui->fc_lineEdit->setText (QString::number (fc_number));
                    ui->leng_length_lineEdit->setText (QString::number (lineLengthA));
                    ui->leng_short_lineEdit->setText (QString::number (lineLengthB));
                }else
                {
                    float fc_number = (lineLengthB - lineLengthA)/1.1*0.1;
                    ui->fc_lineEdit->setText (QString::number (fc_number));
                    ui->leng_length_lineEdit->setText (QString::number (lineLengthB));
                    ui->leng_short_lineEdit->setText (QString::number (lineLengthA));
                }
                //计算棱镜角度
                if(lineLengthA > lineLengthB)
                {
                    int axis_x = abs(vtx[0].x - vtx[1].x);
                    int axis_y = abs(vtx[0].y - vtx[1].y);
                    double angle_atanValue = 0;
                    if(axis_x != 0){
                        angle_atanValue = atan(axis_y/axis_x);
                    }
                    float angle = 0;
                    angle = angle_atanValue*180/3.1415;
                    ui->fc_jiao_lineEdit->setText (QString::number (180 - angle));
                }else
                {
                    int axis_x = abs(vtx[1].x - vtx[2].x);
                    int axis_y = abs(vtx[1].y - vtx[2].y);
                    double angle_atanValue = 0;
                    float angle = 0;
                    if(axis_x != 0){
                        angle_atanValue = atan(axis_y/axis_x);
                    }
                    angle = angle_atanValue*180/3.1415;
                    ui->fc_jiao_lineEdit->setText (QString::number (angle));
                }
            }
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::columnLensMeasureThird()
{
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    vector<Point> ellipseVector;//椭圆点集
    int disCenterOfCircle = 0;//圆心距离
    int  fourFlogEllispeDistance = 0;//四大flog点半径
    int max_w = 0;//最大灰度
    int number = 0;//记录个数
    int distance_x = 0;
    int distance_y = 0;
    int distance_result = 0;//到边点距离
    int distance_Length = 0;//边长
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    fileName = QFileDialog::getOpenFileName(NULL,"open image","F://testImageGray//testImage//","*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image

            int maxThreshold = 0;
            int thresholdNumber = 0;
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //克隆图像
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();
            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<5){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            /////////////////////////////////////////椭圆拟合/////////////////////////////////////////////////
            //计算边长
            distance_Length = sqrt(pow(abs((recordPointA.x + recordPointA.width/2) - (recordPointB.x + recordPointB.width/2)),2 )+ pow(abs(recordPointA.y + recordPointA.height/2 - (recordPointB.y + recordPointB.height/2)),2));
            if(number == 5){
                for (int i = 0; i < contours.size(); i++) {
                    vector<Point> edgePoint = contours.at(i);
                    Rect rect = cv::boundingRect(edgePoint);//求区域
                    if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                        /////////////////////////////////////////////////////////分别计算到四个边点的距离//////////////////////////////////////////////////
                        //每个点到中心距离
                        disCenterOfCircle = 0;
                        disCenterOfCircle = sqrt(pow(abs((rect.x + rect.width/2) - (recordPointC.x + recordPointC.width/2)),2 )+ pow(abs(rect.y + rect.height/2 - (recordPointC.y + recordPointC.height/2)),2));
                        //recordPointA
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointA.x + recordPointA.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointA.y + recordPointA.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointA.y + recordPointA.height/2) - (recordPointC.y + recordPointC.height/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance&&abs(distance_result - fourFlogEllispeDistance/2 ) < 20)//判断到圆心的距离大于四个点的距离一半
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance+distance_Length/4)//判断到圆心的距离小于四个点的距离
                            {
                                if(distance_result > distance_Length/4&&distance_result < distance_Length/2){//距边点的值与边长的4分之一小于15个像素
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointA";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointB
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointB.x + recordPointB.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointB.y + recordPointB.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointB.x + recordPointB.width/2) - (recordPointC.x + recordPointC.width/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance&&abs(distance_result - fourFlogEllispeDistance/2 ) <20)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance+distance_Length/4)
                            {
                                if(distance_result > distance_Length/4&&distance_result < distance_Length/2){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointB";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointD
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointD.x + recordPointD.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointD.y + recordPointD.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointD.x + recordPointD.width/2) - (recordPointC.x + recordPointC.width/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance&&abs(distance_result - fourFlogEllispeDistance/2 ) < 20)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance + distance_Length/4)
                            {
                                if(distance_result > distance_Length/4&&distance_result < distance_Length/2){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointD";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                        //recordPointE
                        distance_x = abs(((rect.x + rect.width/2) - (recordPointE.x + recordPointE.width/2))) ;
                        distance_y = abs(((rect.y + rect.height/2) - (recordPointE.y + recordPointE.height/2)));
                        //距离四边大圆距离
                        distance_result = sqrt(pow(distance_x,2 )+ pow(distance_y,2));
                        //四点半径
                        fourFlogEllispeDistance = 0;
                        fourFlogEllispeDistance = abs((recordPointE.y + recordPointE.height/2) - (recordPointC.y + recordPointC.height/2));
                        if(disCenterOfCircle > fourFlogEllispeDistance&&abs(distance_result - fourFlogEllispeDistance/2 ) < 20)
                        {
                            if(disCenterOfCircle < fourFlogEllispeDistance + distance_Length/4)
                            {
                                if(distance_result > distance_Length/4&&distance_result < distance_Length/2){
                                    cv::circle(image, // 目标图像
                                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                                               rect.height/2- 5, // 半径
                                               10, // 颜色（这里用黑色）
                                               2); // 厚
                                    qDebug()<<"recordPointE";
                                    Point npoint;
                                    npoint.x = rect.x + rect.width/2;
                                    npoint.y = rect.y + rect.height/2;
                                    ellipseVector.push_back (npoint);
                                }
                            }
                        }
                    }
                }
            }
            qDebug()<<"ellipseVector.size:"<<ellipseVector.size();
            if(ellipseVector.size () >= 6)
            {
                Mat pointsf;
                Mat(ellipseVector).convertTo(pointsf, CV_32F);
                RotatedRect box = fitEllipse(pointsf);
                ellipse(image, box, Scalar(100,80,255), 1, LINE_AA);
                ellipse(image, box.center, box.size*0.5f, box.angle, 0, 360, Scalar(255,255,0), 1, LINE_AA);
                Point2f vtx[4];
                box.points(vtx);
                //画边框

                for( int j = 0; j < 4; j++ ){
                    line(image, vtx[j], vtx[(j+1)%4], Scalar(255,255,0), 1, LINE_AA);
                }
                int lineLengthA = sqrt(pow(abs(vtx[0].x - vtx[1].x),2 )+ pow(abs(vtx[0].y - vtx[1].y),2 ));
                int lineLengthB = sqrt(pow(abs(vtx[1].x - vtx[2].x),2 )+ pow(abs(vtx[1].y - vtx[2].y),2 ));
                //                int lineLengthC = sqrt(pow(abs(vtx[2].x - vtx[3].x),2 )+ pow(abs(vtx[2].y - vtx[3].y),2 ));
                //                int lineLengthD = sqrt(pow(abs(vtx[0].x - vtx[3].x),2 )+ pow(abs(vtx[0].y - vtx[3].y),2 ));
                //计算棱镜度
                if(lineLengthA > lineLengthB)
                {
                    float fc_number = (lineLengthA - lineLengthB)/0.95*0.1;
                    ui->fc_lineEdit->setText (QString::number (fc_number));
                    ui->leng_length_lineEdit->setText (QString::number (lineLengthA));
                    ui->leng_short_lineEdit->setText (QString::number (lineLengthB));
                }else
                {
                    float fc_number = (lineLengthB - lineLengthA)/0.95*0.1;
                    ui->fc_lineEdit->setText (QString::number (fc_number));
                    ui->leng_length_lineEdit->setText (QString::number (lineLengthB));
                    ui->leng_short_lineEdit->setText (QString::number (lineLengthA));
                }
                //计算棱镜角度
                if(lineLengthA > lineLengthB)
                {
                    qDebug()<<"herd8";
                    int axis_x = abs(vtx[0].x - vtx[1].x);
                    int axis_y = abs(vtx[0].y - vtx[1].y);
                    double angle_atanValue = 0;
                    float angle = 0;
                    if(axis_x != 0){
                        angle_atanValue = atan(axis_y/axis_x);
                        angle = angle_atanValue*180/3.1415;
                    }
                    ui->fc_jiao_lineEdit->setText (QString::number (180 - angle));
                }else
                {
                    int axis_x = abs(vtx[1].x - vtx[2].x);
                    int axis_y = abs(vtx[1].y - vtx[2].y);
                    double angle_atanValue = 0;
                    float angle = 0;
                    if(axis_x != 0){
                        angle_atanValue = atan(axis_y/axis_x);
                        angle = angle_atanValue*180/3.1415;
                    }
                    ui->fc_jiao_lineEdit->setText (QString::number (angle));
                }
            }
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

bool MainWindow::transmissivityTesting()
{
    int max_w = 0;//最大宽度
    long totalNumber = 0;//亮班灰度统计
    int number = 0;//记录四个大圆
    float basicPrism  = 0;//棱镜基准值
    int n_horizontal = 0;//水平
    int basicNumber  = 0;//基准
    int maxThreshold = 0;//二值化最大值
    int thresholdNumber = 0;//二值化取值
    int n_vertical = 0;//垂直
    int measureNumber = 0;//测量值
    int normalNumber = 0;//正常亮班个数
    float resultFsNumber = 0;
    cv::Mat filteredImg;
    cv::Mat binariedImg;
    double angle = 0;
    //    cv::Mat destImg;
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    fileName = QFileDialog::getOpenFileName(NULL,"open image","F://testImageGray//testImage//","*.jpg");
    if(fileName.size () > 5){
        image = imread(fileName.toStdString (), 0);
        if(!image.empty ()){
            cv::Mat oriImg(INPUT_VIDEO_H,INPUT_VIDEO_W,CV_8UC1,image.data);
            //do image filter
            cv::blur(oriImg,filteredImg,Size(7,7),Point(-1,-1));
            //Binarization process of the cropped image
            for (int h = 0;h < INPUT_VIDEO_H;h++)
            {
                for (int w = 0;w < INPUT_VIDEO_W;w++){
                    thresholdNumber = image.data[h*w + w];
                    if(thresholdNumber> maxThreshold)
                    {
                        maxThreshold = thresholdNumber;
                    }
                }
            }
            qDebug()<<"zui dazhi:"<<maxThreshold;
            if( thresholdState == THRESHOLD_TYPE_AUTO)
            {
                cv::adaptiveThreshold(filteredImg, binariedImg,255.0, CV_ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY, 27, -25);
            }else if(  thresholdState == THRESHOLD_TYPE_CUSTOM)
            {
                int twoValueThres = ui->threshold_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, twoValueThres , 255 ,0);
            }else if(thresholdState == THRESHOLD_TYPE_MAX_Divide)
            {
                threshold(filteredImg, binariedImg, maxThreshold/2 , 255 ,0);
            }else
            {
                int p = ui->threshold_Percentage_lineEdit->text ().toInt ();
                threshold(filteredImg, binariedImg, maxThreshold*p/100 , 255 ,0);
            }
            //start to find split rect after Binarization
            cv::Mat imgTemp = binariedImg.clone();
            //查找轮廓
            cv::findContours(imgTemp, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE, Point(0,0));
            qDebug() << "contours.size() = " << contours.size();

            Rect recordMaxPoint;//记录最大圆心位置
            for (int i = 0; i < contours.size(); i++)
            {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w)
                {
                    max_w = rect.width;//求最大面积
                    recordMaxPoint = rect;
                }
            }
            ///////////////////////////灰度统计///////////////////////////

            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(rect.width > max_w/2&&rect.height > max_w/2 ){//过滤错误小圆
                    normalNumber++;
                    cv::circle(image, // 目标图像
                               cv::Point(rect.x + rect.width/2,rect.y + rect.height/2), // 中心点坐标
                               (rect.height/2 + rect.width/2)/2, // 半径
                               50, // 颜色（这里用黑色）
                               2); // 厚
                    for(int  h = rect.y;h < rect.y + rect.height;h++)
                    {
                        for(int  w = rect.x;w < rect.x + rect.width;w++)
                        {
                            totalNumber+=image.data[h*w + w];
                        }
                    }
                }
            }
            qDebug()<<"max_w:"<< max_w;
            Rect recordPointA;//记录四个点位置
            Rect recordPointB;//记录四个点位置
            Rect recordPointC;//记录四个点位置
            Rect recordPointD;//记录四个点位置
            Rect recordPointE;//记录四个点位置
            for (int i = 0; i < contours.size(); i++) {
                vector<Point> edgePoint = contours.at(i);
                Rect rect = cv::boundingRect(edgePoint);//求区域
                if(abs(rect.width - max_w)<10){//过滤错误小圆
                    number++;
                    if(number == 1)
                    {
                        recordPointA = rect;
                    }else if(number == 2)
                    {
                        recordPointB = rect;
                    }else if(number == 3)
                    {
                        recordPointC = rect;
                    }else if(number == 4)
                    {
                        recordPointD = rect;
                    }
                    else if(number == 5)
                    {
                        recordPointE = rect;
                    }
                }
            }
            if(number != 5 )
            {
                QImage resultImage = cvMat2QImage(binariedImg);
                QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
                ui->original_label->setPixmap(QPixmap::fromImage(newImg));
                QMessageBox msgBox;
                msgBox.setText("Lookup center circle failure");
                msgBox.exec();
                return 0;
            }
            if(number == 4){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                n_horizontal = sqrt(pow(abs(recordPointA.x - recordPointC.x),2) + pow(abs(recordPointA.y - recordPointC.y),2));//水平
                n_vertical = n_horizontal;//垂直
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           n_horizontal/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                ui->horizontal_lineEdit->setText (QString::number (n_horizontal));//水平拉伸
                ui->vertical_lineEdit->setText (QString::number (n_vertical));//垂直拉伸
                //12.55比例球镜//计算球镜值
                basicNumber = (horizontal_value + vertical_value)/2;
                measureNumber = (n_horizontal + n_vertical)/2;
                resultFsNumber  = ( basicNumber - measureNumber) / 12.55;
                ui->fs_lineEdit->setText (QString::number (resultFsNumber));
                float x = (center_X - (recordPointB.x + recordPointB.width/2))/640;
                float y =  (center_Y - (recordPointB.y + recordPointB.height/2))/512;
                ui->center_point_x_lineEdit->setText (QString::number (x));//x坐标
                ui->center_point_y_lineEdit->setText (QString::number (y));//y坐标
                ui->measure_X_value_lineEdit->setText (QString::number (recordPointB.x + recordPointB.width/2));//x坐标
                ui->measure_Y_value_lineEdit->setText (QString::number (recordPointB.y + recordPointB.height/2));//y坐标
                //棱镜30.34个基准像素
                //计算棱镜30.34
                basicPrism = sqrt(pow(abs(recordPointB.x + recordPointB.width/2 - center_X),2 )+ pow(abs(recordPointB.y + recordPointB.height/2 - center_Y),2));
                ui->fp_lineEdit->setText (QString::number (basicPrism/30.34));
            }
            if(number == 5){
                cv::circle(image, // 目标图像
                           cv::Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2), // 中心点坐标
                           recordPointA.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2), // 中心点坐标
                           recordPointB.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2), // 中心点坐标
                           recordPointD.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2), // 中心点坐标
                           recordPointE.height/2-10, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                vector<Point> areaPoint;
                areaPoint.push_back (Point(recordPointA.x + recordPointA.width/2,recordPointA.y + recordPointA.height/2));
                areaPoint.push_back (Point(recordPointB.x + recordPointB.width/2,recordPointB.y + recordPointB.height/2));
                areaPoint.push_back (Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2));
                areaPoint.push_back (Point(recordPointD.x + recordPointD.width/2,recordPointD.y + recordPointD.height/2));
                areaPoint.push_back (Point(recordPointE.x + recordPointE.width/2,recordPointE.y + recordPointE.height/2));
                Rect resultRect = cv::boundingRect(areaPoint);//求区域
                cv::circle(image, // 目标图像
                           cv::Point(resultRect.x + resultRect.width/2,resultRect.y + resultRect.height/2), // 中心点坐标
                           (resultRect.width/2+resultRect.height/2)/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                cv::circle(image, // 目标图像
                           cv::Point(recordPointC.x + recordPointC.width/2,recordPointC.y + recordPointC.height/2), // 中心点坐标
                           recordPointC.width/2, // 半径
                           10, // 颜色（这里用黑色）
                           2); // 厚
                n_horizontal = sqrt(pow(abs(recordPointB.x - recordPointD.x),2) + pow(abs(recordPointB.y - recordPointD.y),2));//水平
                n_vertical = sqrt(pow(abs(recordPointA.x - recordPointE.x),2 )+ pow(abs(recordPointA.y - recordPointE.y),2));//垂直
                ui->horizontal_lineEdit->setText (QString::number (n_horizontal));//水平拉伸
                ui->vertical_lineEdit->setText (QString::number (n_vertical));//垂直拉伸
                //12.55比例球镜//计算球镜值
                basicNumber = (horizontal_value + vertical_value)/2;
                measureNumber = (n_horizontal + n_vertical)/2;
                resultFsNumber  = ( basicNumber - measureNumber) / 12.55;
                ui->fs_lineEdit->setText (QString::number (resultFsNumber));
                ui->measure_X_value_lineEdit->setText (QString::number (resultRect.x + resultRect.width/2));//x坐标
                ui->measure_Y_value_lineEdit->setText (QString::number (resultRect.y + resultRect.height/2));//x坐标
                float x = (center_X - (resultRect.x + resultRect.width/2))/640;
                float y =  (center_Y - (resultRect.y + resultRect.height/2))/512;
                ui->center_point_x_lineEdit->setText (QString::number (x));//x坐标
                ui->center_point_y_lineEdit->setText (QString::number (y));//y坐标

                double axis_x =   (resultRect.x + resultRect.width/2) - center_X;
                double axis_y =   (resultRect.y + resultRect.height/2) - center_Y;
                if(axis_y  < 0)
                {
                    if(axis_x > 0)
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415;
                    }
                    else
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 90;
                    }
                }
                else
                {
                    if(axis_x > 0)
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 270;
                    }
                    else
                    {
                        double angle_atanValue = 0;
                        angle_atanValue = atan(axis_y/axis_x);
                        angle=angle_atanValue*180/3.1415 + 180;
                    }
                }
                ui->fp_jiao_lineEdit->setText (QString::number (angle));
                //计算棱镜30.34
                basicPrism = sqrt(pow(abs(resultRect.x + resultRect.width/2 - center_X),2 )+ pow(abs(resultRect.y + resultRect.height/2 - center_Y),2));
                ui->fp_lineEdit->setText (QString::number (basicPrism/30.34));
            }
            /////////////////////////////写入显示////////////////////////////////////////////
            float measureTranNumber = 0;
            measureTranNumber = totalNumber/normalNumber;
            ui->tran_gray_averige_lineEdit->setText (QString::number (measureTranNumber));
            ui->tran_light_number_lineEdit->setText (QString::number (normalNumber));
            ui->tran_number_lineEdit->setText (QString::number (measureTranNumber/recordCorrectTransmiss + basicPrism / 30.34 * 3.5 / 100));
            QImage resultImage = cvMat2QImage(image);
            QImage newImg  =  resultImage.scaled(ui->original_label->width(), ui->original_label->height(),Qt::KeepAspectRatioByExpanding);
            ui->original_label->setPixmap(QPixmap::fromImage(newImg));
        }
    }
    return 0;
}

void MainWindow::on_open_image_pushbutton_clicked()
{
    deleteAllLineEdit();
    imageTwoValue();
}

void MainWindow::on_find_circular_pushButton_clicked()
{
    deleteAllLineEdit();
    findAllContours();
}

void MainWindow::on_add_number_pushButton_clicked()
{
    deleteAllLineEdit();
    addImageAllNumber();
}

void MainWindow::on_are_total_number_pushuButton_clicked()
{
    deleteAllLineEdit();
    aretotalNumber();
}

void MainWindow::on_are_total_pushButton_clicked()
{
    deleteAllLineEdit();
    fourLightAverige();
}

void MainWindow::on_total_255_number_pushButton_clicked()
{
    deleteAllLineEdit();
    total255Number();
}

void MainWindow::on_light_are_averige_pushButton_clicked()
{
    deleteAllLineEdit();
    lightAreAverige();
}

void MainWindow::on_find_center_circular_pushButton_clicked()
{
    deleteAllLineEdit();
    findCenterCircular();
}

void MainWindow::on_lock_image_pushButton_clicked()
{
    lockImagePixel();
}

void MainWindow::on_correct_pushButton_clicked()
{
    correctOpenBoot();
}

void MainWindow::on_transmissivity_pushButton_clicked()
{
    transmissivityTesting();
}

void MainWindow::on_ellipse_fitting_pushButton_clicked()
{
    ellipseFitting();
}

void MainWindow::on_colunm_measure_third_pushButton_clicked()
{
    columnLensMeasureThird();//柱镜测量
}

void MainWindow::on_colunm_measure_one_pushButton_clicked()
{
    columnLensMeasureOne();//柱镜测量
}

void MainWindow::on_colunm_measure_two_pushButton_clicked()
{
    columnLensMeasureTwo();//柱镜测量
}

void MainWindow::on_len_measure_one_pushButton_clicked()
{
    lensMeasureFunOne();
}

void MainWindow::on_len_measure_two_pushButton_clicked()
{
    lensMeasureFunTwo();
}

void MainWindow::on_big_circular_gray_pushButton_clicked()
{
    centerCircularGrayTotal();
}

void MainWindow::on_all_cir_averige_phsubutton_clicked()
{
    allCirGrayAverige();
}

void MainWindow::on_comboBox_activated(const QString &arg1)
{
    if(arg1 == "Custom threshold")
    {
        thresholdState = THRESHOLD_TYPE_CUSTOM;
    }else if(arg1  == "Auto threshold" )
    {
        thresholdState = THRESHOLD_TYPE_AUTO;
    }else if( arg1 == "GrayMax/2 threshold")
    {
        thresholdState = THRESHOLD_TYPE_MAX_Divide;
    }else
    {
        thresholdState = THRESHOLD_TYPE_MAX_Percentage;
    }
    qDebug()<< arg1;
}

void MainWindow::on_center_circular_gray_pushButton_clicked()
{
    centerCircularGray();
}

void MainWindow::on_five_gray_pushButton_clicked()
{
    fiveLightGray();
}

void MainWindow::on_five_circular_gray_pushButton_clicked()
{
    fiveCircularGray();
}

void MainWindow::on_fixed_center_pushButton_clicked()
{
    fixedCenter();
}

void MainWindow::on_lock_raw_image_pushButton_clicked()
{

    lockRawLmagePixel();
}

void MainWindow::on_five_light_area_pushButton_clicked()
{
    fiveLightArea();
}

void MainWindow::on_raw_rgb_pushButton_clicked()
{
    raw2rgb();
}
