#ifndef SOFTDESKTOPTEXTRENDERER_H
#define SOFTDESKTOPTEXTRENDERER_H

#include <QColor>
#include <QFont>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class QImage;
class QFontMetrics;
class QPainter;

class SoftDesktopTextRenderer
{
public:
    struct Options
    {
        QColor textColor = QColor(255, 255, 255);
        QColor glowColor = QColor(0, 0, 0, 210);
        QColor innerShadowColor = QColor(0, 0, 0, 155);
        int largeGlowRadius = 6;
        int tightGlowRadius = 2;
        QPoint glowOffset = QPoint(0, 0);
        QPoint innerShadowOffset = QPoint(0, 0);
        int lineSpacingAdjust = -2;
        Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignVCenter;
        Qt::TextElideMode elideMode = Qt::ElideRight;
    };

    static void drawText(QPainter* painter,
                         const QRect& rect,
                         const QString& text,
                         const QFont& font,
                         const Options& options = Options());

    static void drawText(QPainter* painter,
                         const QRect& rect,
                         const QStringList& lines,
                         const QFont& font,
                         const Options& options = Options());

private:
    static QImage buildTextMask(const QSize& logicalSize,
                                qreal devicePixelRatio,
                                const QRect& localTextRect,
                                const QStringList& lines,
                                const QFont& font,
                                const Options& options);
    static QImage blurAlphaImage(const QImage& source, const QColor& color, int radius);
    static void blurHorizontal(const QImage& source, QImage& target, int radius);
    static void blurVertical(const QImage& source, QImage& target, int radius);
    static QVector<QRect> buildLineRects(const QRect& rect,
                                         const QFontMetrics& metrics,
                                         int lineCount,
                                         int lineSpacingAdjust,
                                         Qt::Alignment alignment);
    static QStringList elideLines(const QStringList& lines,
                                  const QFontMetrics& metrics,
                                  int width,
                                  Qt::TextElideMode mode);
};

#endif
