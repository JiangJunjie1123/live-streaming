#include "danmaku_overlay.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>

const QColor DanmakuOverlay::s_colors[] = {
    QColor(255, 255, 255),
    QColor(255, 100, 100),
    QColor(100, 255, 100),
    QColor(100, 180, 255),
    QColor(255, 255, 100),
    QColor(255, 180, 100),
    QColor(200, 150, 255),
    QColor(100, 255, 255),
};
const int DanmakuOverlay::s_colorCount = sizeof(s_colors) / sizeof(s_colors[0]);

DanmakuOverlay::DanmakuOverlay(QObject* parent)
    : QObject(parent)
{
    m_rowOccupied.resize(20);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &DanmakuOverlay::repaintNeeded);
    m_timer->start(16);
}

void DanmakuOverlay::addDanmaku(const QString& username, const QString& text)
{
    QString fullText = username + ": " + text;
    QFont font("Microsoft YaHei", 14);
    QFontMetrics fm(font);
    double textWidth = fm.horizontalAdvance(fullText);

    // use last known width from render(), or a sensible default
    int screenW = m_rowOccupied.size() > 0 ? 800 : 800;
    int row = allocateRow(screenW, textWidth);
    if (row < 0) return;

    DanmakuItem item;
    item.text = fullText;
    item.x = screenW;
    item.y = row * m_rowHeight;
    item.color = s_colors[qHash(username) % s_colorCount];

    m_items.append(item);
    m_rowOccupied[row] = screenW + textWidth;
}

int DanmakuOverlay::allocateRow(double screenWidth, double textWidth)
{
    for (int i = 0; i < m_rowOccupied.size(); i++) {
        if (m_rowOccupied[i] <= screenWidth - textWidth - m_minGap) {
            return i;
        }
    }
    return -1;
}

void DanmakuOverlay::render(QPainter* painter, int width, int height)
{
    // update row count based on current widget height
    int maxRows = (height / m_rowHeight) + 1;
    if (maxRows < 1) maxRows = 1;
    if (maxRows != m_rowOccupied.size()) {
        m_rowOccupied.resize(maxRows);
        for (int i = 0; i < m_rowOccupied.size(); i++)
            m_rowOccupied[i] = 0;
    }

    if (m_items.isEmpty()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont font("Microsoft YaHei", 14, QFont::Bold);
    QFontMetrics fm(font);
    painter->setFont(font);

    double dt = 0.016;
    QList<DanmakuItem> remaining;

    for (int i = 0; i < m_rowOccupied.size(); i++)
        m_rowOccupied[i] = 0;

    for (auto& item : m_items) {
        item.x -= m_speed * dt;

        QPainterPath path;
        path.addText(item.x, item.y + m_rowHeight - fm.descent(), font, item.text);

        QPen outlinePen(QColor(0, 0, 0, 180), 3);
        painter->setPen(outlinePen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);

        painter->setPen(Qt::NoPen);
        painter->setBrush(item.color);
        painter->drawPath(path);

        double w = fm.horizontalAdvance(item.text);
        if (item.x + w > 0) {
            remaining.append(item);
            int rowIdx = item.y / m_rowHeight;
            if (rowIdx >= 0 && rowIdx < m_rowOccupied.size()) {
                double rightEdge = item.x + w;
                if (rightEdge > m_rowOccupied[rowIdx])
                    m_rowOccupied[rowIdx] = rightEdge;
            }
        }
    }

    m_items = remaining;
    painter->restore();
}
