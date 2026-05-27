#ifndef DANMAKU_OVERLAY_H
#define DANMAKU_OVERLAY_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <QVector>
#include <QColor>
#include <QString>

class QPainter;

class DanmakuOverlay : public QObject
{
    Q_OBJECT
public:
    explicit DanmakuOverlay(QObject* parent = nullptr);

    void addDanmaku(const QString& username, const QString& text);
    void render(QPainter* painter, int width, int height);

signals:
    void repaintNeeded();

private:
    struct DanmakuItem {
        QString text;
        double x;
        int y;
        QColor color;
    };

    int allocateRow(double screenWidth, double textWidth);

    QList<DanmakuItem> m_items;
    QTimer* m_timer;
    int m_speed = 150;
    int m_rowHeight = 28;
    double m_minGap = 40;
    QVector<double> m_rowOccupied;
    int m_lastWidth = 0;

    static const QColor s_colors[];
    static const int s_colorCount;
};

#endif // DANMAKU_OVERLAY_H
