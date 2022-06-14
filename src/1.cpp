#include "CameraViewfinder.h"
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core/types.hpp>
#include <QGraphicsBlurEffect>
#include <QPainter>

using namespace std;
using namespace cv;

#ifdef Q_OS_WIN
#include <cstring>
#endif

CameraViewfinder::CameraViewfinder(QObject *parent) : QAbstractVideoSurface(parent)
{
    if(face_cascade_db.load("faces.xml")) {
        cout << "haarcascade load";
    } else {
        cout << "Error. Haarcascade don't load";
    }
}

CameraViewfinder::~CameraViewfinder()
{
}

QList<QVideoFrame::PixelFormat>
    CameraViewfinder::supportedPixelFormats(QAbstractVideoBuffer::HandleType) const
{
    // Необходимо использовать RGB32 для Windows, т.к. всё иное отвергается с ошибкой
    // "Video surfave needs to support RGB32 pixel format. Failed to configure preview format."

    // Необходимо использовать YUYV для MacOS, потому что он по-другому не может

#if defined(Q_OS_WIN)
    return QList<QVideoFrame::PixelFormat>() << QVideoFrame::Format_RGB32;
#elif defined(Q_OS_MACOS)
    return QList<QVideoFrame::PixelFormat>() << QVideoFrame::Format_YUYV;
#else
    return QList<QVideoFrame::PixelFormat>() << QVideoFrame::Format_YUV420P;
#endif
}

bool CameraViewfinder::present(const QVideoFrame &frame)
{
    if (!frame.isValid() || frame.pixelFormat() == QVideoFrame::Format_Invalid)
        return false;

    QVideoFrame copiedFrame(frame);
    const auto resolution = frame.size();
    _converter.setResolution(resolution);

    copiedFrame.map(QAbstractVideoBuffer::ReadOnly);

#if defined(Q_OS_WIN)
    // ВНИМАНИЕ: Windows нагло нас обманывает, на самом деле это BGR32,
    // да ещё и зеркальный по высоте, хотя мы вообще просили YUYV
    const auto frameSize = copiedFrame.mappedBytes();


    // Конвертация из QVideoFrame в cv::Mat
    Mat frameYUV=Mat(copiedFrame.height(), copiedFrame.width(), CV_8UC4, (void*)copiedFrame.bits());
    Mat mask = Mat::zeros(frameYUV.size(), frameYUV.type()); // all 0
    Mat circleFaceMask = mask.clone();
    Mat flipFrame;
    rotate(frameYUV, flipFrame, ROTATE_180);
    Mat grayFrame;
    cvtColor(flipFrame, grayFrame, COLOR_BGR2GRAY);
    vector<Rect> faces;
    face_cascade_db.detectMultiScale(grayFrame, faces, 1.1, 19);
    cout << "Find " << faces.size() << " faces" << endl;
    Mat rectangleFace = flipFrame.clone();
    if(faces.size() != 0) {
        for (int i = 0; i < faces.size(); i++) {
            // Вывод высоты, ширины и координат в консоль
            cout << faces[i].height << " " << faces[i].width << " " << faces[i].x << " " << faces[i].y << endl;
            circle(circleFaceMask, Point(faces[i].x + faces[i].width / 2, faces[i].y + faces[i].height / 2), faces[i].width / 2, Scalar(255, 255, 255), -1);
        }
        Mat faceFrame;
        Mat blurFrame;
        blur(flipFrame, blurFrame, {30, 30});
        bitwise_and(blurFrame, circleFaceMask, faceFrame);
        bitwise_not(circleFaceMask, circleFaceMask);
        bitwise_and(flipFrame, circleFaceMask, flipFrame);
        bitwise_or(flipFrame, faceFrame, flipFrame);
    }

    rotate(flipFrame, frameYUV, ROTATE_180);

    if (_bgr32Frame.capacity() < frameSize)
        _bgr32Frame.reserve(frameSize);

    _bgr32Frame.resize(frameSize);
    std::memcpy(_bgr32Frame.data(), frameYUV.ptr(), frameSize);

    // при смене разрешения "на горячую" ещё могут приходить фреймы со старым разрешением 1-3 шт.,
    // их нужно проигнорировать
    if (_bgr32Frame.size() != (resolution.width() * resolution.height() * 4))
        return true;

    _converter.rgbVerticalFlip(_bgr32Frame, _flippedBgr32Frame);
    _converter.rgb32ToYuv420(_flippedBgr32Frame, _yuv420Frame, FrameConverter::RgbOrder::bgr);
#elif defined(Q_OS_MACOS)
    _yuv422Frame =
        QByteArray(reinterpret_cast<char *>(copiedFrame.bits()), copiedFrame.mappedBytes());
    _converter.yuv422ToYuv420(_yuv422Frame, _yuv420Frame);
#else
    _yuv420Frame =
        QByteArray(reinterpret_cast<char *>(copiedFrame.bits()), copiedFrame.mappedBytes());
#endif

    copiedFrame.unmap();

    emit frameYuvReady(_yuv420Frame);
    return true;
}
