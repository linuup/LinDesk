#include "ui/SoftDesktopTextRenderer.h"

#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPaintDevice>
#include <QVector>

void SoftDesktopTextRenderer::drawText(QPainter* painter,
                                       const QRect& rect,
                                       const QString& text,
                                       const QFont& font,
                                       const Options& options)
{
    drawText(painter, rect, QStringList{ text }, font, options);
}

void SoftDesktopTextRenderer::drawText(QPainter* painter,
                                       const QRect& rect,
                                       const QStringList& lines,
                                       const QFont& font,
                                       const Options& options)
{
    if (!painter || rect.isEmpty() || lines.isEmpty()) {
        return;
    }

    QStringList filteredLines;
    filteredLines.reserve(lines.size());
    for (const QString& line : lines) {
        if (!line.isNull()) {
            filteredLines.push_back(line);
        }
    }

    if (filteredLines.isEmpty()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setFont(font);

    const QFontMetrics metrics(font);
    const QStringList elidedLines = elideLines(filteredLines, metrics, rect.width(), options.elideMode);
    const QVector<QRect> lineRects = buildLineRects(rect, metrics, elidedLines.size(), options.lineSpacingAdjust, options.alignment);
    if (lineRects.isEmpty()) {
        painter->restore();
        return;
    }

    const int maxRadius = qMax(options.largeGlowRadius, options.tightGlowRadius);
    const int maxOffset = qMax(qMax(qAbs(options.glowOffset.x()), qAbs(options.glowOffset.y())),
                               qMax(qAbs(options.innerShadowOffset.x()), qAbs(options.innerShadowOffset.y())));
    const qreal devicePixelRatio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const int maskMargin = maxRadius + maxOffset;
    const QRect localTextRect(maskMargin, maskMargin, rect.width(), rect.height());
    const QSize maskLogicalSize(rect.width() + maskMargin * 2, rect.height() + maskMargin * 2);
    const QImage textMask = buildTextMask(maskLogicalSize, devicePixelRatio, localTextRect, elidedLines, font, options);

    if (!textMask.isNull()) {
        if (options.largeGlowRadius > 0) {
            const QImage glowLarge = blurAlphaImage(textMask, options.glowColor, options.largeGlowRadius);
            painter->drawImage(QPointF(rect.left() - maskMargin + options.glowOffset.x(),
                                       rect.top() - maskMargin + options.glowOffset.y()),
                               glowLarge);
        }

        if (options.tightGlowRadius > 0) {
            const QImage glowTight = blurAlphaImage(textMask, options.glowColor, options.tightGlowRadius);
            painter->drawImage(QPointF(rect.left() - maskMargin + options.glowOffset.x(),
                                       rect.top() - maskMargin + options.glowOffset.y()),
                               glowTight);
        }
    }

    painter->setPen(options.innerShadowColor);
    for (int i = 0; i < elidedLines.size() && i < lineRects.size(); ++i) {
        painter->drawText(lineRects[i].translated(options.innerShadowOffset), options.alignment, elidedLines[i]);
    }

    painter->setPen(options.textColor);
    for (int i = 0; i < elidedLines.size() && i < lineRects.size(); ++i) {
        painter->drawText(lineRects[i], options.alignment, elidedLines[i]);
    }

    painter->restore();
}

QImage SoftDesktopTextRenderer::buildTextMask(const QSize& logicalSize,
                                              qreal devicePixelRatio,
                                              const QRect& localTextRect,
                                              const QStringList& lines,
                                              const QFont& font,
                                              const Options& options)
{
    if (logicalSize.isEmpty()) {
        return QImage();
    }

    QImage textMask(QSize(qMax(1, qRound(logicalSize.width() * devicePixelRatio)),
                          qMax(1, qRound(logicalSize.height() * devicePixelRatio))),
                    QImage::Format_ARGB32_Premultiplied);
    textMask.setDevicePixelRatio(devicePixelRatio);
    textMask.fill(Qt::transparent);

    QPainter maskPainter(&textMask);
    maskPainter.setRenderHint(QPainter::Antialiasing);
    maskPainter.setRenderHint(QPainter::TextAntialiasing);
    maskPainter.setFont(font);
    maskPainter.setPen(Qt::white);

    const QFontMetrics metrics(font);
    const QVector<QRect> localLineRects = buildLineRects(localTextRect, metrics, lines.size(), options.lineSpacingAdjust, options.alignment);
    for (int i = 0; i < lines.size() && i < localLineRects.size(); ++i) {
        maskPainter.drawText(localLineRects[i], options.alignment, lines[i]);
    }

    return textMask;
}

QImage SoftDesktopTextRenderer::blurAlphaImage(const QImage& source, const QColor& color, int radius)
{
    if (radius <= 0 || source.isNull()) {
        return source;
    }

    QImage horizontal(source.size(), QImage::Format_ARGB32_Premultiplied);
    horizontal.setDevicePixelRatio(source.devicePixelRatio());
    horizontal.fill(Qt::transparent);

    QImage blurred(source.size(), QImage::Format_ARGB32_Premultiplied);
    blurred.setDevicePixelRatio(source.devicePixelRatio());
    blurred.fill(Qt::transparent);

    blurHorizontal(source, horizontal, radius);
    blurVertical(horizontal, blurred, radius);

    for (int y = 0; y < blurred.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(blurred.scanLine(y));
        for (int x = 0; x < blurred.width(); ++x) {
            const int alpha = qAlpha(row[x]);
            row[x] = qRgba(color.red(), color.green(), color.blue(),
                           qMin(255, alpha * color.alpha() / 255));
        }
    }

    return blurred;
}

void SoftDesktopTextRenderer::blurHorizontal(const QImage& source, QImage& target, int radius)
{
    QVector<int> alphaPrefix(source.width() + 1);

    for (int y = 0; y < source.height(); ++y) {
        alphaPrefix[0] = 0;
        for (int x = 0; x < source.width(); ++x) {
            alphaPrefix[x + 1] = alphaPrefix[x] + qAlpha(source.pixel(x, y));
        }

        QRgb* dst = reinterpret_cast<QRgb*>(target.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const int left = qMax(0, x - radius);
            const int right = qMin(source.width() - 1, x + radius);
            const int sum = alphaPrefix[right + 1] - alphaPrefix[left];
            const int count = right - left + 1;
            const int alpha = count > 0 ? sum / count : 0;
            dst[x] = qRgba(255, 255, 255, alpha);
        }
    }
}

void SoftDesktopTextRenderer::blurVertical(const QImage& source, QImage& target, int radius)
{
    QVector<int> alphaPrefix(source.height() + 1);

    for (int x = 0; x < source.width(); ++x) {
        alphaPrefix[0] = 0;
        for (int y = 0; y < source.height(); ++y) {
            alphaPrefix[y + 1] = alphaPrefix[y] + qAlpha(source.pixel(x, y));
        }

        for (int y = 0; y < source.height(); ++y) {
            const int top = qMax(0, y - radius);
            const int bottom = qMin(source.height() - 1, y + radius);
            const int sum = alphaPrefix[bottom + 1] - alphaPrefix[top];
            const int count = bottom - top + 1;
            const int alpha = count > 0 ? sum / count : 0;
            reinterpret_cast<QRgb*>(target.scanLine(y))[x] = qRgba(255, 255, 255, alpha);
        }
    }
}

QVector<QRect> SoftDesktopTextRenderer::buildLineRects(const QRect& rect,
                                                       const QFontMetrics& metrics,
                                                       int lineCount,
                                                       int lineSpacingAdjust,
                                                       Qt::Alignment alignment)
{
    QVector<QRect> lineRects;
    if (rect.isEmpty() || lineCount <= 0) {
        return lineRects;
    }

    const int lineHeight = metrics.lineSpacing();
    const int visualLineStep = qMax(1, lineHeight + lineSpacingAdjust);
    const int totalHeight = lineHeight + qMax(0, lineCount - 1) * visualLineStep;
    int startY = rect.top();
    if (alignment & Qt::AlignBottom) {
        startY = rect.bottom() - totalHeight + 1;
    } else if (alignment & Qt::AlignVCenter) {
        startY = rect.top() + qMax(0, (rect.height() - totalHeight) / 2);
    }

    lineRects.reserve(lineCount);
    for (int i = 0; i < lineCount; ++i) {
        lineRects.push_back(QRect(rect.left(),
                                  startY + i * visualLineStep,
                                  rect.width(),
                                  lineHeight));
    }

    return lineRects;
}

QStringList SoftDesktopTextRenderer::elideLines(const QStringList& lines,
                                                const QFontMetrics& metrics,
                                                int width,
                                                Qt::TextElideMode mode)
{
    QStringList result;
    result.reserve(lines.size());
    const int safeWidth = qMax(1, width);
    for (const QString& line : lines) {
        result.push_back(metrics.elidedText(line, mode, safeWidth));
    }
    return result;
}
