#ifndef DANMAKU_OVERLAY_H
#define DANMAKU_OVERLAY_H

#include <QWidget>
#include <QTimer>
#include <QList>
#include <QVector>
#include <QColor>
#include <QString>

class DanmakuOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit DanmakuOverlay(QWidget* parent = nullptr);

    void addDanmaku(const QString& username, const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

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
    int m_speed = 150;       // pixel per second
    int m_rowHeight = 28;
    double m_minGap = 40;    // minimum gap between danmaku items on the same row
    QVector<double> m_rowOccupied; // rightmost x occupied per row

    static const QColor s_colors[];
    static const int s_colorCount;
};

#endif // DANMAKU_OVERLAY_H
