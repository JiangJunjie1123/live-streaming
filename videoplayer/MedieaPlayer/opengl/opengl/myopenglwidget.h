#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>

class DanmakuOverlay;

class MyOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    MyOpenGLWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent), m_danmaku(nullptr) {}

    void slot_setImage(QImage img);
    void setDanmakuOverlay(DanmakuOverlay* overlay) { m_danmaku = overlay; }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    QSize getImageScaledSize(QSize size);

private:
    QImage m_image;
    GLuint m_texture;
    DanmakuOverlay* m_danmaku;
};
