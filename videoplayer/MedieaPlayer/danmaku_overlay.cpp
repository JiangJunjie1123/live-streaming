#include "danmaku_overlay.h"
#include <QPainter>
#include <QFontMetrics>

const QColor DanmakuOverlay::s_colors[] = {
    QColor(255, 255, 255), // white
    QColor(255, 100, 100), // red
    QColor(100, 255, 100), // green
    QColor(100, 180, 255), // blue
    QColor(255, 255, 100), // yellow
    QColor(255, 180, 100), // orange
    QColor(200, 150, 255), // purple
    QColor(100, 255, 255), // cyan
};
const int DanmakuOverlay::s_colorCount = sizeof(s_colors) / sizeof(s_colors[0]);

DanmakuOverlay::DanmakuOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, false);

    m_rowOccupied.resize(20); // up to 20 rows, will grow dynamically in resizeEvent

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        update();
    });
    m_timer->start(16); // ~60fps
}

void DanmakuOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    int maxRows = (height() / m_rowHeight) + 1;
    if (maxRows < 1) maxRows = 1;
    m_rowOccupied.resize(maxRows);
    for (int i = 0; i < m_rowOccupied.size(); i++) {
        m_rowOccupied[i] = 0;
    }
}

void DanmakuOverlay::addDanmaku(const QString& username, const QString& text)
{
    QString fullText = username + ": " + text;
    QFont font("Microsoft YaHei", 14);
    QFontMetrics fm(font);
    double textWidth = fm.horizontalAdvance(fullText);

    int row = allocateRow(width(), textWidth);
    if (row < 0) return; // all rows busy, drop

    DanmakuItem item;
    item.text = fullText;
    item.x = width();
    item.y = row * m_rowHeight;
    item.color = s_colors[qHash(username) % s_colorCount];

    m_items.append(item);
    m_rowOccupied[row] = width() + textWidth;
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

void DanmakuOverlay::paintEvent(QPaintEvent*)
{
    if (m_items.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont font("Microsoft YaHei", 14, QFont::Bold);
    QFontMetrics fm(font);
    painter.setFont(font);

    double dt = 0.016; // ~60fps
    QList<DanmakuItem> remaining;

    // reset row occupancy, will recalculate
    for (int i = 0; i < m_rowOccupied.size(); i++) {
        m_rowOccupied[i] = 0;
    }

    for (auto& item : m_items) {
        item.x -= m_speed * dt;

        // draw text with outline for readability over any background
        QPainterPath path;
        path.addText(item.x, item.y + m_rowHeight - fm.descent(), font, item.text);

        // outline
        QPen outlinePen(QColor(0, 0, 0, 180), 3);
        painter.setPen(outlinePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // fill
        painter.setPen(Qt::NoPen);
        painter.setBrush(item.color);
        painter.drawPath(path);

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
}
