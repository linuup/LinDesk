#include "box/LinBox.h"
#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <QPainter>
#include <QShowEvent>
#include <QStyleOption>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QStyleFactory>
#include <QFileInfo>
#include <QClipboard>
#include <QMimeData>
#include <QtMath>
#include <limits>
#include <climits>
#include <vector>
#include "ui/SettingUI.h"
#include "desktop/ListView.h"
#include "desktop/ListItem.h"
#include "box/BoxManager.h"
#include "desktop/RightMenu.h"
#include "services/WinBlurHelper.h"
#include "ui/Fluent/FluentDragTip.h"
#include "monitor/LinDirectoryMonitor.h"
#include "SystemApi/SystemApi.h"
#include <ShellContextMenu.h>
#include <QtWinExtras/QtWin>
#include <QSvgRenderer>

namespace {

QString normalizeMappedFolderPath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path.trimmed());
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QString defaultDesktopPath()
{
    PWSTR path = nullptr;
    QString desktopPath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        desktopPath = QString::fromWCharArray(path);
        CoTaskMemFree(path);
    }
    return QDir::toNativeSeparators(desktopPath);
}

bool isDriveRootPath(const QString& path)
{
    return path.length() == 2 && path[1] == ':';
}

QString mappedFolderTitleIconResourcePath(const QString& path)
{
    if (isDriveRootPath(path)) {
        return QStringLiteral(":/Assets/disk.svg");
    }
    return QStringLiteral(":/Assets/folder-share.svg");
}

bool draggedItemsContainMappedSource(const QVector<ListItem*>& items)
{
    for (ListItem* item : items) {
        if (!item || item->boxId().isEmpty()) {
            continue;
        }
        if (LinBox* sourceBox = BoxManager::instance()->findBoxById(item->boxId())) {
            if (sourceBox->isFolderMapped()) {
                return true;
            }
        }
    }
    return false;
}

constexpr DWORD kClipboardPreferredDropEffectMove = 0x00000002;

QStringList clipboardLocalFilePathsForBox()
{
    QStringList filePaths;
    if (const QClipboard* clipboard = QGuiApplication::clipboard()) {
        if (const QMimeData* mimeData = clipboard->mimeData()) {
            if (mimeData->hasUrls()) {
                for (const QUrl& url : mimeData->urls()) {
                    if (url.isLocalFile()) {
                        filePaths.append(QDir::toNativeSeparators(url.toLocalFile()));
                    }
                }
            }
        }
    }
    return filePaths;
}

bool clipboardContainsCutItemsForBox()
{
    const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (!format || !IsClipboardFormatAvailable(format) || !OpenClipboard(nullptr)) {
        return false;
    }

    bool isCutOperation = false;
    if (const HANDLE clipboardData = GetClipboardData(format)) {
        if (const void* locked = GlobalLock(clipboardData)) {
            const DWORD effect = *static_cast<const DWORD*>(locked);
            isCutOperation = (effect & kClipboardPreferredDropEffectMove) != 0;
            GlobalUnlock(clipboardData);
        }
    }

    CloseClipboard();
    return isCutOperation;
}

std::vector<std::wstring> toNativeWStringPaths(const QStringList& paths)
{
    std::vector<std::wstring> nativePaths;
    nativePaths.reserve(paths.size());
    for (const QString& path : paths) {
        if (!path.isEmpty()) {
            nativePaths.push_back(QDir::toNativeSeparators(path).toStdWString());
        }
    }
    return nativePaths;
}

QVector<qreal> gaussianKernel(int radius)
{
    if (radius <= 0) {
        return {1.0};
    }

    QVector<qreal> kernel(radius * 2 + 1);
    const qreal sigma = qMax<qreal>(1.0, radius / 2.0);
    const qreal twoSigma2 = 2.0 * sigma * sigma;
    qreal sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const qreal value = qExp(-(i * i) / twoSigma2);
        kernel[i + radius] = value;
        sum += value;
    }
    for (qreal& value : kernel) {
        value /= sum;
    }
    return kernel;
}

QImage gaussianBlurImage(const QImage& source, int radius)
{
    if (source.isNull() || radius <= 0) {
        return source;
    }

    const QImage input = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage horizontal(input.size(), QImage::Format_ARGB32_Premultiplied);
    QImage output(input.size(), QImage::Format_ARGB32_Premultiplied);
    const QVector<qreal> kernel = gaussianKernel(radius);

    for (int y = 0; y < input.height(); ++y) {
        for (int x = 0; x < input.width(); ++x) {
            qreal a = 0, r = 0, g = 0, b = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = qBound(0, x + k, input.width() - 1);
                const QRgb px = input.pixel(sx, y);
                const qreal w = kernel[k + radius];
                a += qAlpha(px) * w;
                r += qRed(px) * w;
                g += qGreen(px) * w;
                b += qBlue(px) * w;
            }
            horizontal.setPixel(x, y, qRgba(qRound(r), qRound(g), qRound(b), qRound(a)));
        }
    }

    for (int y = 0; y < input.height(); ++y) {
        for (int x = 0; x < input.width(); ++x) {
            qreal a = 0, r = 0, g = 0, b = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = qBound(0, y + k, input.height() - 1);
                const QRgb px = horizontal.pixel(x, sy);
                const qreal w = kernel[k + radius];
                a += qAlpha(px) * w;
                r += qRed(px) * w;
                g += qGreen(px) * w;
                b += qBlue(px) * w;
            }
            output.setPixel(x, y, qRgba(qRound(r), qRound(g), qRound(b), qRound(a)));
        }
    }

    return output;
}

QColor blendTowardWhite(const QColor& color, qreal amount)
{
    const qreal t = qBound<qreal>(0.0, amount, 1.0);
    return QColor(
        qRound(color.red() + (255 - color.red()) * t),
        qRound(color.green() + (255 - color.green()) * t),
        qRound(color.blue() + (255 - color.blue()) * t));
}

void applyNoiseLayer(QImage& image, int alpha)
{
    if (image.isNull() || alpha <= 0) {
        return;
    }

    alpha = qBound(0, alpha, 255);
    const QSize noiseSampleSize(qMax(8, image.width() / 10), qMax(8, image.height() / 10));
    QImage noise(noiseSampleSize, QImage::Format_ARGB32_Premultiplied);
    noise.fill(Qt::transparent);

    for (int y = 0; y < noise.height(); ++y) {
        QRgb* scanLine = reinterpret_cast<QRgb*>(noise.scanLine(y));
        for (int x = 0; x < noise.width(); ++x) {
            const uint hash = static_cast<uint>((x * 73856093) ^ (y * 19349663) ^ 0x9e3779b9);
            const int shade = 128 + static_cast<int>(hash % 13) - 6;
            scanLine[x] = qRgba(shade, shade, shade, alpha);
        }
    }

    noise = gaussianBlurImage(noise, 1);
    const QImage scaledNoise = noise.scaled(
        image.size(),
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(0.045);
    painter.drawImage(QPoint(0, 0), scaledNoise);
    painter.end();
}

QPixmap buildBlurredGlass(const QPixmap& source, const QColor& tintColor, float tintOpacity)
{
    if (source.isNull()) {
        return QPixmap();
    }

    const QSize originalSize = source.size();
    const QSize sampleSize(qMax(1, originalSize.width() / 3), qMax(1, originalSize.height() / 3));
    QImage sampled = source.toImage().scaled(sampleSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    sampled = gaussianBlurImage(sampled, 5);
    QImage restored = sampled.scaled(originalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPainter overlayPainter(&restored);
    overlayPainter.setRenderHint(QPainter::Antialiasing);

    // Approximate the official Acrylic recipe:
    // backdrop + blur + exclusion-style light pass + tint overlay + subtle noise.
    overlayPainter.setCompositionMode(QPainter::CompositionMode_Exclusion);
    overlayPainter.fillRect(restored.rect(), QColor(245, 245, 245, 10));

    overlayPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QColor softenedTint = blendTowardWhite(tintColor, 0.25);
    const int tintAlpha = qBound(0, qRound(qBound(0.0f, tintOpacity, 1.0f) * 90.0f), 90);
    overlayPainter.fillRect(restored.rect(), QColor(softenedTint.red(), softenedTint.green(), softenedTint.blue(), tintAlpha));
    overlayPainter.end();

    applyNoiseLayer(restored, 18);

    return QPixmap::fromImage(restored);
}

QIcon buildWhiteSvgIcon(const QString& resourcePath, const QSize& iconSize)
{
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid()) {
        return QIcon(resourcePath);
    }

    QPixmap pixmap(iconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRect(QPoint(0, 0), iconSize));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), Qt::white);
    painter.end();

    return QIcon(pixmap);
}

}

LinBox::LinBox(QWidget* parent) : QWidget(parent) {
    // 浣跨敤Qt::SubWindow浣緽ox鎴愪负ListView鐨勭湡姝ｅ瓙绐楀彛锛屼笉浼氳鐩栧叾浠栬蒋浠?
    setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("LinBox { background: transparent; }");
    setMouseTracking(true);
    
    setMinimumSize(m_minBoxWidth, m_minBoxHeight);
    resize(300, 400);
    setAcceptDrops(true);
    m_blurRefreshTimer = new QTimer(this);
    m_blurRefreshTimer->setSingleShot(true);
    m_blurRefreshTimer->setInterval(16);
    connect(m_blurRefreshTimer, &QTimer::timeout, this, &LinBox::updateBlurCache);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    m_mainLayout->setSpacing(0);

    this->m_title = tr("New Box");
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(m_titleHeight);
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setAttribute(Qt::WA_StyledBackground); 
    
    QHBoxLayout* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(10, 0, 5, 0);

    m_titleEdit = new QLineEdit(m_titleBar);
    m_titleEdit->setVisible(false);
    m_titleEdit->setText(m_title);
    m_titleEdit->setAlignment(Qt::AlignCenter);
    m_titleEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_titleEdit->setFixedHeight(22);
    m_titleEdit->setStyleSheet("background: white; color: black; border-radius: 4px; padding: 2px;");

    m_viewSwitchBtn = new QPushButton(m_titleBar);
    m_viewSwitchBtn->setFixedSize(24, 24);
    m_viewSwitchBtn->setIconSize(QSize(16, 16));
    m_viewSwitchBtn->setText(QString());
    m_viewSwitchBtn->setIcon(buildWhiteSvgIcon(":/Assets/list.svg", m_viewSwitchBtn->iconSize()));
    
    QFont iconFont("Segoe Fluent Icons");
    bool fontLoaded = QFontInfo(iconFont).exactMatch();
    if (!fontLoaded) {
        iconFont.setFamily("Segoe MDL2 Assets");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Segoe UI Symbol");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Wingdings");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    iconFont.setPixelSize(14);
    m_viewSwitchBtn->setStyleSheet("QPushButton { background: transparent; color: white; border: none; border-radius: 4px; } "
                                   "QPushButton:hover { background: rgba(255, 255, 255, 40); } "
                                   "QPushButton:pressed { background: rgba(255, 255, 255, 20); }");

    m_collapseBtn = new QPushButton(m_titleBar);
    m_collapseBtn->setFixedSize(24, 24);
    m_collapseBtn->setFont(iconFont);
    // Use text icons as fallback if font doesn't support Fluent Icons
    QString collapseIcon = fontLoaded ? u8"\uE70E" : "v";
    m_collapseBtn->setText(collapseIcon);
    m_collapseBtn->setStyleSheet("QPushButton { background: transparent; color: white; border: none; border-radius: 4px; } "
                                 "QPushButton:hover { background: rgba(255, 255, 255, 40); } ");

    titleLayout->insertWidget(0, m_collapseBtn);
    titleLayout->addStretch();
    titleLayout->addWidget(m_titleEdit, 1);
    titleLayout->addStretch();
    titleLayout->addWidget(m_viewSwitchBtn);

    // Content Area inside Scroll Area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("scrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; } "
                                "QScrollBar:vertical { width: 4px; background: transparent; margin: 0px 0px 0px 0px; } "
                                "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 120); border-radius: 2px; min-height: 20px; } "
                                "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 200); } "
                                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; } "
                                "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; } ");

    m_contentArea = new QWidget(m_scrollArea);
    m_contentArea->setObjectName("contentArea");
    m_contentArea->setAttribute(Qt::WA_StyledBackground);
    m_scrollArea->setWidget(m_contentArea);

    m_mainLayout->addWidget(m_titleBar);
    m_mainLayout->addWidget(m_scrollArea);

    refreshStyles();

    // Signals
    connect(m_viewSwitchBtn, &QPushButton::clicked, this, [this]() {
        if (m_showType == BoxShowType::Icon_ShowType) {
            setShowType(BoxShowType::List_ShowType);
        } else {
            setShowType(BoxShowType::Icon_ShowType);
        }
    });

    connect(m_collapseBtn, &QPushButton::clicked, this, &LinBox::toggleCollapse);

    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this]() {
        commitTitleEditing();
    });

    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_titleEdit->isVisible()) {
            commitTitleEditing();
        }
    });

    m_titleBar->installEventFilter(this);
    m_contentArea->installEventFilter(this);
    raise(); 
}

LinBox::~LinBox() {
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
    }
    // Note: m_items (ListItem*) are not deleted here because they are shared
    // between ListView's m_allItems and Box's m_items. The ListView destructor
    // will handle the actual ListItem deletion through m_allItems.
}

void LinBox::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    raise();
    refreshLayout();
    QTimer::singleShot(0, this, [this]() {
        m_lastLayoutContentWidth = -1;
        refreshLayout();
    });
    if (!m_isCapturingBackground) {
        scheduleBlurRefresh();
    }
}

void LinBox::setShowType(BoxShowType type) {
    if (m_showType == type) return;
    m_showType = type;
    if (m_showType == BoxShowType::Icon_ShowType) {
        m_viewSwitchBtn->setIcon(buildWhiteSvgIcon(":/Assets/list.svg", m_viewSwitchBtn->iconSize()));
    } else {
        m_viewSwitchBtn->setIcon(buildWhiteSvgIcon(":/Assets/grid.svg", m_viewSwitchBtn->iconSize()));
    }
    refreshLayout();
    QTimer::singleShot(0, this, [this]() {
        m_lastLayoutContentWidth = -1;
        refreshLayout();
    });
    emit geometryChanged(this);
}

void LinBox::addListItem(ListItem* item, bool refresh) {
    if (!item || m_items.contains(item)) return;
    m_items.append(item);
    item->setParent(m_contentArea);
    item->setBoxId(m_id); // Ensure item knows it's in this box
    item->show();
    item->raise();
    if (refresh) {
        refreshLayout();
        emit geometryChanged(this);
    }
}

void LinBox::insertListItem(ListItem* item, int index, bool refresh) {
    if (!item) return;
    m_items.removeAll(item);
    const int clampedIndex = qBound(0, index, m_items.size());
    m_items.insert(clampedIndex, item);
    item->setParent(m_contentArea);
    item->setBoxId(m_id);
    item->show();
    item->raise();
    m_lastLayoutItemCount = -1;
    if (refresh) {
        refreshLayout();
        emit geometryChanged(this);
    }
}

void LinBox::removeListItem(ListItem* item, bool refresh) {
    if (!item) return;
    m_items.removeAll(item);
    item->setBoxId(""); // Item is no longer in this box
    // Note: Do NOT change parent here. Caller is responsible for reparenting.
    item->hide();
    if (refresh) {
        refreshLayout();
        emit geometryChanged(this);
    } else {
        m_lastLayoutItemCount = -1;
    }
}

void LinBox::refreshLayout() {
    if (m_isRefreshingLayout || m_items.isEmpty()) return;
    const int itemW = 76;
    const int itemH = 78;
    const int colSpacing = 8;
    const int rowSpacing = 8;
    const int marginLeft = 5; 
    const int marginTop = 5;
    if (m_mainLayout) {
        m_mainLayout->activate();
    }
    if (m_scrollArea) {
        m_scrollArea->updateGeometry();
    }
    const int viewportW = (m_scrollArea && m_scrollArea->viewport()) ? m_scrollArea->viewport()->width() : 0;
    const int fallbackContentW = qMax(1, width() - (m_borderWidth * 2));
    const int contentW = qMax(1, viewportW > 0 ? viewportW : fallbackContentW);
    quint64 visualSignature = 1469598103934665603ULL;
    for (const ListItem* item : m_items) {
        const quint64 textHeight = static_cast<quint64>(qMax(0, item ? item->visibleTextHeight() : 0));
        const quint64 expanded = (item && item->isFullNameExpanded()) ? 0x9e3779b97f4a7c15ULL : 0ULL;
        visualSignature ^= textHeight + expanded + 0x9e3779b97f4a7c15ULL + (visualSignature << 6) + (visualSignature >> 2);
    }
    const bool layoutUnchanged =
        m_lastLayoutContentWidth == contentW &&
        m_lastLayoutItemCount == m_items.size() &&
        m_lastLayoutShowType == m_showType &&
        m_lastLayoutVisualSignature == visualSignature;

    if (layoutUnchanged) {
        return;
    }

    m_isRefreshingLayout = true;

    if (m_showType == BoxShowType::Icon_ShowType) {
        const int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        const int rows = (m_items.size() + cols - 1) / cols;
        QVector<int> rowHeights(rows, itemH);

        for (int i = 0; i < m_items.size(); ++i) {
            const int row = i / cols;
            ListItem* item = m_items[i];
            item->setLayoutMode(ListItem::LayoutMode::IconMode);
            item->setItemSize(itemW, itemH);
            rowHeights[row] = qMax(rowHeights[row], item->size().height());
            item->show();
            item->raise();
        }

        QVector<int> rowTops(rows, marginTop);
        for (int row = 1; row < rows; ++row) {
            rowTops[row] = rowTops[row - 1] + rowHeights[row - 1] + rowSpacing;
        }

        for (int i = 0; i < m_items.size(); ++i) {
            const int row = i / cols;
            const int col = i % cols;
            const int x = marginLeft + col * (itemW + colSpacing);
            const int y = rowTops[row];
            const QPoint targetPos(x, y);
            ListItem* item = m_items[i];
            if (item->pos() != targetPos) {
                item->move(targetPos);
                item->updateLocationInfo(targetPos);
            }
        }
    } else {
        int listItemH = 30; 
        for (int i = 0; i < m_items.size(); ++i) {
            ListItem* item = m_items[i];
            const QFont textFont("Microsoft YaHei UI", 9);
            const QFontMetrics fm(textFont);
            const int iconAreaW = 8 + 20 + 8; // left padding + icon + gap
            const int textW = qMax(24, fm.horizontalAdvance(item->displayName()));
            const int rightPadding = 12;
            const int listItemW = qMin(contentW - 2 * marginLeft, iconAreaW + textW + rightPadding);
            const int x = marginLeft;
            const int y = marginTop + i * (listItemH + 2);
            const QPoint targetPos(x, y);
            item->setLayoutMode(ListItem::LayoutMode::ListMode);
            if (item->size() != QSize(listItemW, listItemH)) {
                item->setItemSize(listItemW, listItemH);
            }
            if (item->pos() != targetPos) {
                item->move(targetPos);
                item->updateLocationInfo(targetPos);
            }
            item->show();
            item->raise();
        }
    }
    m_contentArea->update();
    
    // Update content area height to enable scroll area
    int totalHeight = 0;
    if (m_showType == BoxShowType::Icon_ShowType) {
        const int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        int rowHeightSum = 0;
        for (int rowStart = 0; rowStart < m_items.size(); rowStart += cols) {
            int rowMaxHeight = itemH;
            const int rowEnd = qMin(rowStart + cols, m_items.size());
            for (int i = rowStart; i < rowEnd; ++i) {
                rowMaxHeight = qMax(rowMaxHeight, m_items[i]->size().height());
            }
            rowHeightSum += rowMaxHeight;
        }
        const int rows = (m_items.size() + cols - 1) / cols;
        totalHeight = marginTop + rowHeightSum + qMax(0, rows - 1) * rowSpacing + 10;
    } else {
        totalHeight = marginTop + m_items.size() * (30 + 2) + 10;
    }
    m_contentArea->setMinimumWidth(contentW);
    m_contentArea->setMinimumHeight(totalHeight);
    m_contentArea->resize(contentW, totalHeight);

    m_lastLayoutContentWidth = contentW;
    m_lastLayoutItemCount = m_items.size();
    m_lastLayoutShowType = m_showType;
    m_lastLayoutVisualSignature = visualSignature;
    m_isRefreshingLayout = false;
    
    // Ensure structural layout changes are persisted
    emit geometryChanged(this);
}

void LinBox::moveEvent(QMoveEvent* event) {
    if (QWidget* parent = parentWidget()) {
        const QRect oldRect(event->oldPos(), size());
        const QRect newRect(pos(), size());
        const QRect dirty = oldRect.united(newRect).adjusted(-12, -12, 12, 12);
        parent->update(dirty);
    }

    raise();
    QWidget::moveEvent(event);
    if (!m_isCapturingBackground && m_glassEffect && event->oldPos() != pos()) {
        scheduleBlurRefresh();
    }
}

void LinBox::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshLayout();
    if (!m_isCapturingBackground && m_glassEffect && event->oldSize() != event->size()) {
        scheduleBlurRefresh();
    }
}

void LinBox::setBoxTitle(const QString& title) {
    m_title = sanitizedTitleText(title);
    m_titleEdit->setText(m_title);
    m_titleBar->update();
    emit geometryChanged(this);
}

void LinBox::refreshMappedFolderState()
{
    if (m_mappedFolderPath.isEmpty()) {
        m_mappedFolderState = MappedFolderState::Normal;
        return;
    }

    const QFileInfo info(m_mappedFolderPath);
    if (!info.exists() || !info.isDir()) {
        m_mappedFolderState = MappedFolderState::Missing;
        return;
    }

    if (!info.isReadable()) {
        m_mappedFolderState = MappedFolderState::NoAccess;
        return;
    }

    m_mappedFolderState = MappedFolderState::Normal;
}

QString LinBox::displayTitleText() const
{
    if (!isFolderMapped()) {
        return m_title;
    }
    const QString mappedText = mappedFolderDisplayText();
    if (m_mappedFolderState == MappedFolderState::Missing) {
        return tr("[Missing] %1").arg(mappedText);
    }
    if (m_mappedFolderState == MappedFolderState::NoAccess) {
        return tr("[No Access] %1").arg(mappedText);
    }
    return mappedText;
}

void LinBox::setMappedFolderPath(const QString& path)
{
    m_mappedFolderPath = normalizeMappedFolderPath(path);
    refreshMappedFolderState();
    if (m_titleEdit) {
        m_titleEdit->setText(displayTitleText());
    }
    if (m_titleBar) {
        m_titleBar->update();
    }
    emit geometryChanged(this);
}

void LinBox::clearMappedFolderPath()
{
    m_mappedFolderPath.clear();
    m_mappedFolderMonitor.reset();
    refreshMappedFolderState();
    if (m_titleEdit) {
        m_titleEdit->setText(m_title);
    }
    if (m_titleBar) {
        m_titleBar->update();
    }
    emit geometryChanged(this);
}

void LinBox::setMappedFolderMonitor(LinDirectoryMonitor* monitor)
{
    m_mappedFolderMonitor.reset(monitor);
}

void LinBox::beginTitleEditing() {
    if (isFolderMapped() && isDriveRootPath(m_mappedFolderPath)) {
        return;
    }
    m_titleEdit->setMaxLength(m_maxCustomTitleLength);
    m_titleEdit->setText(isFolderMapped() ? mappedFolderDisplayText() : m_title);
    m_titleEdit->show();
    m_titleEdit->setFocus();
    m_titleEdit->selectAll();
    m_titleBar->update();
}

void LinBox::commitTitleEditing() {
    const QString newTitle = sanitizedTitleText(m_titleEdit->text());
    if (isFolderMapped()) {
        m_title = newTitle;
        m_titleEdit->hide();
        m_titleBar->update();
        emit geometryChanged(this);
        return;
    }
    setBoxTitle(newTitle.isEmpty() ? m_title : newTitle);
    m_titleEdit->hide();
    m_titleBar->update();
}

QString LinBox::mappedFolderDisplayText() const
{
    if (!m_title.trimmed().isEmpty()) {
        return m_title.trimmed();
    }
    return QDir::toNativeSeparators(m_mappedFolderPath);
}

QString LinBox::sanitizedTitleText(const QString& title) const
{
    QString normalized = title;
    normalized.replace('\n', ' ');
    normalized.replace('\r', ' ');
    normalized = normalized.simplified();
    if (normalized.length() > m_maxCustomTitleLength) {
        normalized = normalized.left(m_maxCustomTitleLength);
    }
    return normalized;
}

bool LinBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleBar && event->type() == QEvent::Paint) {
        if (!m_titleEdit->isVisible()) {
            QPainter p(m_titleBar);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::white);
            p.setFont(QFont("Microsoft YaHei UI", 9, QFont::Bold));
            
            // Adjust rect to avoid buttons: collapseBtn(30px) and viewSwitchBtn(right)
            // Left gap for collapse button + padding
            QRect textRect = m_titleBar->rect().adjusted(35, 0, -35, 0);
            
            if (isFolderMapped()) {
                const int iconSize = 20;
                const int iconY = (m_titleBar->height() - iconSize) / 2;
                QFontMetrics fm(p.font());
                const QString title = displayTitleText();
                const int textWidth = fm.horizontalAdvance(title);
                const int totalWidth = iconSize + 6 + textWidth;
                int startX = (m_titleBar->width() - totalWidth) / 2;
                startX = qMax(35, startX);

                bool iconDrawn = false;
                QSvgRenderer renderer(mappedFolderTitleIconResourcePath(m_mappedFolderPath));
                if (renderer.isValid()) {
                    renderer.render(&p, QRect(startX, iconY, iconSize, iconSize));
                    iconDrawn = true;
                } else if (isDriveRootPath(m_mappedFolderPath)) {
                    QSvgRenderer fallbackRenderer(QStringLiteral(":/Assets/folder-share.svg"));
                    if (fallbackRenderer.isValid()) {
                        fallbackRenderer.render(&p, QRect(startX, iconY, iconSize, iconSize));
                        iconDrawn = true;
                    }
                }

                if (iconDrawn) {
                    textRect = m_titleBar->rect().adjusted(startX + iconSize + 6, 0, -35, 0);
                    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);
                } else {
                    QFontMetrics fm(p.font());
                    QString elidedTitle = fm.elidedText(displayTitleText(), Qt::ElideRight, textRect.width());
                    p.drawText(textRect, Qt::AlignCenter, elidedTitle);
                }
            } else {
                QFontMetrics fm(p.font());
                QString elidedTitle = fm.elidedText(displayTitleText(), Qt::ElideRight, textRect.width());
                p.drawText(textRect, Qt::AlignCenter, elidedTitle);
            }
        }
        return false; // Let the widget continue painting its own background/hover if it has style
    }

    if (watched == m_titleBar || watched == m_contentArea) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_titleEdit->isVisible()) {
                const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
                const QPoint titlePos = sourceWidget == m_titleBar
                    ? mouseEvent->pos()
                    : m_titleBar->mapFromGlobal(sourceWidget->mapToGlobal(mouseEvent->pos()));
                if (!m_titleEdit->geometry().contains(titlePos)) {
                    commitTitleEditing();
                }
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                BoxManager::instance()->bringToFront(this);
                const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
                const QPoint mappedPos = watched == this
                    ? mouseEvent->pos()
                    : sourceWidget->mapTo(this, mouseEvent->pos());
                m_resizeDir = getResizeDir(mappedPos);

                if (m_resizeDir != None && m_resizeDir != Move) {
                    m_isResizing = true;
                    m_dragStartPos = mouseEvent->globalPos();
                    m_dragStartGeometry = geometry();
                    m_geometryChangedDuringInteraction = false;
                    return true;
                }

                if (watched == m_contentArea) {
                    QWidget* child = m_contentArea->childAt(mouseEvent->pos());
                    if (child == nullptr && m_resizeDir == None) {
                        beginItemSelection(mouseEvent->pos());
                        return true;
                    }
                }

                if (isDragArea(mappedPos)) {
                    m_isMoving = true;
                    m_dragStartPos = mouseEvent->globalPos();
                    m_dragStartGeometry = geometry();
                    m_geometryChangedDuringInteraction = false;
                    return true;
                }
            } else if (mouseEvent->button() == Qt::RightButton) {
                showCustomContextMenu(mouseEvent->globalPos());
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
            const QPoint mappedPos = watched == this
                ? mouseEvent->pos()
                : sourceWidget->mapTo(this, mouseEvent->pos());

            if (watched == m_contentArea && m_isSelectingItems) {
                updateItemSelection(mouseEvent->pos());
                return true;
            }
            if (!m_isSelectingItems && !m_isResizing && !m_isMoving) {
                updateCursorShape(mappedPos);
            }
        } else if (watched == m_contentArea && event->type() == QEvent::MouseButtonRelease) {
            if (m_isSelectingItems) {
                finishItemSelection();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick && watched == m_titleBar) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            // Only trigger rename if we clicked in the title area, not on buttons
            if (isDragArea(mouseEvent->pos())) {
                beginTitleEditing();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LinBox::mousePressEvent(QMouseEvent* event) {
    if (m_titleEdit->isVisible()) {
        const QPoint titlePos = m_titleBar->mapFrom(this, event->pos());
        if (!m_titleEdit->geometry().contains(titlePos)) {
            commitTitleEditing();
        }
    }
    if (event->button() == Qt::LeftButton) {
        BoxManager::instance()->bringToFront(this);
    }
    setFocus();
    if (event->button() == Qt::LeftButton) {
        m_resizeDir = getResizeDir(event->pos());
        if (isDragArea(event->pos())) {
            m_isMoving = true;
            m_dragStartPos = event->globalPos();
            m_dragStartGeometry = geometry();
            m_geometryChangedDuringInteraction = false;

            event->accept();
            return;
        } else if (m_resizeDir != None) {
            m_isResizing = true;
            m_dragStartPos = event->globalPos();
            m_dragStartGeometry = geometry();
            m_geometryChangedDuringInteraction = false;
        }
    } else if (event->button() == Qt::RightButton) {
        QWidget* child = childAt(event->pos());
        if (m_titleBar->geometry().contains(event->pos()) || !child || child == m_contentArea || child == this) {
            showCustomContextMenu(event->globalPos());
        }
    }
}

void LinBox::mouseMoveEvent(QMouseEvent* event) {
    if (m_isResizing) {
        QPoint delta = event->globalPos() - m_dragStartPos;
        QRect newGeom = m_dragStartGeometry;
        if (m_resizeDir & Left) newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
        if (m_resizeDir & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());
        if (m_resizeDir & Top) newGeom.setTop(m_dragStartGeometry.top() + delta.y());
        if (m_resizeDir & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());
        if (newGeom.width() >= 100 && newGeom.height() >= 100) {
            if (geometry() != newGeom) {
                setGeometry(newGeom);
                m_geometryChangedDuringInteraction = true;
                update();
            }
        }
    } else if (m_isMoving) {
        // 瀹炴椂璺熼殢榧犳爣绉诲姩
        QPoint delta = event->globalPos() - m_dragStartPos;
        QPoint newPos = m_dragStartGeometry.topLeft() + delta;
        
        // 绉诲姩鍒版柊浣嶇疆
        if (pos() != newPos) {
            move(newPos);
            m_geometryChangedDuringInteraction = true;
            update();
        }
        
        // 鍙洿鏂癇ox鏈韩锛岄伩鍏嶅眰绾у彉鍖?
    } else {
        updateCursorShape(event->pos());
    }
}

void LinBox::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isSelectingItems) {
        finishItemSelection();
        event->accept();
        return;
    }
    if ((m_isResizing || m_isMoving) && m_geometryChangedDuringInteraction) {
        refreshStyles();
        emit geometryChanged(this);
    }
    const bool shouldRefreshBlur = m_glassEffect && m_geometryChangedDuringInteraction;
    m_isResizing = false;
    m_isMoving = false;
    m_geometryChangedDuringInteraction = false;
    if (shouldRefreshBlur) {
        scheduleBlurRefresh();
    }
    updateCursorShape(event->pos());
}

void LinBox::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect contentRect = rect().adjusted(m_borderWidth, m_borderWidth, -m_borderWidth, -m_borderWidth);

    if (m_glassEffect && !m_blurCache.isNull()) {
        p.drawPixmap(contentRect.topLeft(), m_blurCache);
        p.fillRect(contentRect, QColor(255, 255, 255, 8));
        p.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(contentRect.adjusted(0, 0, -1, -1));
    } else {
        QStyleOption opt;
        opt.init(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

        int r = m_bgColor.red(), g = m_bgColor.green(), b = m_bgColor.blue();
        int a = (int)(m_bgOpacity * 255);
        if (a < 5) a = 20;
        p.fillRect(contentRect, QColor(r, g, b, a));
        p.setPen(QPen(QColor(255, 255, 255, 50), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(contentRect.adjusted(0, 0, -1, -1));
    }

    const QRect indicatorRect = insertionIndicatorRect();
    if (!indicatorRect.isNull()) {
        p.setPen(Qt::NoPen);
        if (m_showType == BoxShowType::Icon_ShowType && indicatorRect.width() <= 4) {
            const QColor accent(0, 255, 255, 235);
            p.setBrush(accent);
            p.drawRect(indicatorRect);
        } else if (m_showType == BoxShowType::Icon_ShowType) {
            const QColor accent(0, 255, 255, 235);
            p.setBrush(accent);
            p.drawRect(indicatorRect);
        } else {
            p.setBrush(QColor(0, 255, 255, 230));
            p.drawRect(indicatorRect);
        }
    }

}

void LinBox::beginItemSelection(const QPoint& contentPos)
{
    ListView* listView = ListView::instance();
    if (listView) {
        listView->clearCheckedItem();
    }

    if (!m_selectionArea) {
        m_selectionArea = new SelectArea(m_contentArea);
    }

    m_isSelectingItems = true;
    m_selectionStartPos = contentPos;
    m_selectionArea->syncToParent();
    m_selectionArea->setSelectionRect(QRect(contentPos, QSize(1, 1)));
    m_selectionArea->show();
    m_selectionArea->raise();
    updateItemSelection(contentPos);
}

void LinBox::updateItemSelection(const QPoint& contentPos)
{
    if (!m_isSelectingItems) {
        return;
    }

    const QRect selectionRect = QRect(m_selectionStartPos, contentPos).normalized();
    if (m_selectionArea) {
        m_selectionArea->syncToParent();
        m_selectionArea->setSelectionRect(selectionRect);
    }

    QVector<ListItem*> selectedItems;
    selectedItems.reserve(m_items.size());
    for (ListItem* item : m_items) {
        if (!item || !item->isVisible()) {
            continue;
        }
        if (selectionRect.intersects(item->getChooseItemRect())) {
            selectedItems.push_back(item);
        }
    }

    if (ListView* listView = ListView::instance()) {
        listView->setSelectedItems(selectedItems, false);
    }
}

void LinBox::finishItemSelection()
{
    m_isSelectingItems = false;
    if (m_selectionArea) {
        m_selectionArea->setSelectionRect(QRect());
        m_selectionArea->hide();
    }
}

void LinBox::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
}

void LinBox::leaveEvent(QEvent* event) {
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

LinBox::ResizeDir LinBox::getResizeDir(const QPoint& pos) {
    const int x = pos.x();
    const int y = pos.y();
    const int w = width();
    const int h = height();
    const int resizeMargin = qMax(4, m_borderWidth);
    const int cornerMargin = qMax(resizeMargin * 2, 16);

    if (x < 0 || y < 0 || x >= w || y >= h) {
        return None;
    }

    const bool nearLeft = x < resizeMargin;
    const bool nearRight = x >= w - resizeMargin;
    const bool nearTop = y < resizeMargin;
    const bool nearBottom = y >= h - resizeMargin;

    const bool inTopLeftCorner = x < cornerMargin && y < cornerMargin;
    const bool inTopRightCorner = x >= w - cornerMargin && y < cornerMargin;
    const bool inBottomLeftCorner = x < cornerMargin && y >= h - cornerMargin;
    const bool inBottomRightCorner = x >= w - cornerMargin && y >= h - cornerMargin;

    if (nearLeft && nearTop && inTopLeftCorner) return TopLeft;
    if (nearRight && nearTop && inTopRightCorner) return TopRight;
    if (nearLeft && nearBottom && inBottomLeftCorner) return BottomLeft;
    if (nearRight && nearBottom && inBottomRightCorner) return BottomRight;
    if (nearLeft) return Left;
    if (nearRight) return Right;
    if (nearBottom) return Bottom;
    if (nearTop) return Top;

    if (y >= resizeMargin && y <= m_titleHeight && x >= resizeMargin && x < w - resizeMargin) {
        return Move;
    }
    return None;
}

bool LinBox::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        if (m_glassEffect) {
            int x = GET_X_LPARAM(msg->lParam);
            int y = GET_Y_LPARAM(msg->lParam);
            QPoint localPos = mapFromGlobal(QPoint(x, y));
            
            if (rect().contains(localPos)) {
                // 1. Full Title bar area must be solid to capture ALL mouse events (drag + buttons)
                if (m_titleBar->geometry().contains(localPos)) {
                    *result = HTCLIENT;
                    return true;
                }
                
                // 2. Content area (within scroll area): Icons and ScrollBars are solid; empty space is click-through
                if (m_scrollArea->geometry().contains(localPos)) {
                    QPoint scrollPos = m_scrollArea->mapFrom(this, localPos);
                    QWidget* child = m_scrollArea->childAt(scrollPos);
                    
                    // If it's a scrollbar or a child of contentArea, it should be solid
                    if (child && (child == m_scrollArea->verticalScrollBar() || m_contentArea->childAt(m_contentArea->mapFrom(m_scrollArea, scrollPos)))) {
                        *result = HTCLIENT;
                        return true;
                    }
                }
                
                // 3. Fallback for all other internal area (empty content background) -> Click through
                *result = HTTRANSPARENT; 
                return true; 
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

ListItem* LinBox::hitItemAt(const QPoint& boxPos) const
{
    if (!m_contentArea) {
        return nullptr;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    for (int i = m_items.size() - 1; i >= 0; --i) {
        ListItem* item = m_items[i];
        if (item && item->isVisible() && item->geometry().contains(contentPos)) {
            return item;
        }
    }
    return nullptr;
}

ListItem* LinBox::findItemByPath(const QString& filePath) const
{
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(filePath)).toLower();
    for (ListItem* item : m_items) {
        if (!item) {
            continue;
        }
        const QString itemPath = QDir::cleanPath(QDir::fromNativeSeparators(item->filePath())).toLower();
        if (itemPath == normalized) {
            return item;
        }
    }
    return nullptr;
}

int LinBox::insertionIndexForPos(const QPoint& boxPos, const QVector<ListItem*>& draggedItems, bool* alignAfterPrev) const
{
    if (m_items.isEmpty()) {
        if (alignAfterPrev) *alignAfterPrev = false;
        return 0;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    QVector<ListItem*> orderedItems = m_items;
    for (ListItem* draggedItem : draggedItems) {
        orderedItems.removeAll(draggedItem);
    }

    if (orderedItems.isEmpty()) {
        if (alignAfterPrev) *alignAfterPrev = false;
        return 0;
    }

    if (m_showType == BoxShowType::List_ShowType) {
        if (alignAfterPrev) *alignAfterPrev = false;
        for (int i = 0; i < orderedItems.size(); ++i) {
            ListItem* item = orderedItems[i];
            const QRect rect = item->geometry();
            if (contentPos.y() < rect.center().y()) {
                return i;
            }
        }
        return orderedItems.size();
    }

    QVector<QVector<int>> rows;
    for (int i = 0; i < orderedItems.size(); ++i) {
        const QRect rect = orderedItems[i]->getItemRect();
        bool placed = false;
        for (QVector<int>& row : rows) {
            const QRect firstRect = orderedItems[row.first()]->getItemRect();
            if (qAbs(firstRect.top() - rect.top()) <= qMax(6, rect.height() / 3)) {
                row.append(i);
                placed = true;
                break;
            }
        }
        if (!placed) {
            rows.append({i});
        }
    }

    QVector<QRect> rowRects;
    QVector<int> rowCenters;
    rowRects.reserve(rows.size());
    rowCenters.reserve(rows.size());
    for (const QVector<int>& row : rows) {
        QRect rowRect = orderedItems[row.first()]->getItemRect();
        for (int idx : row) {
            rowRect = rowRect.united(orderedItems[idx]->getItemRect());
        }
        rowRects.append(rowRect);
        rowCenters.append(rowRect.center().y());
    }

    int targetRow = 0;
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const int upperBound = (rowIndex == 0)
            ? INT_MIN
            : (rowCenters[rowIndex - 1] + rowCenters[rowIndex]) / 2;
        const int lowerBound = (rowIndex == rows.size() - 1)
            ? INT_MAX
            : (rowCenters[rowIndex] + rowCenters[rowIndex + 1]) / 2;
        if (contentPos.y() >= upperBound && contentPos.y() < lowerBound) {
            targetRow = rowIndex;
            break;
        }
    }

    const QVector<int>& row = rows[targetRow];
    const QRect firstRect = orderedItems[row.first()]->getItemRect();
    const QRect lastRect = orderedItems[row.last()]->getItemRect();

    struct InsertionLane {
        int insertIndex = 0;
        bool afterPrev = false;
        int x = 0;
    };

    QVector<InsertionLane> lanes;
    lanes.reserve(row.size() + 1);
    lanes.append({row.first(), false, firstRect.left()});
    for (int i = 0; i < row.size() - 1; ++i) {
        const QRect currentRect = orderedItems[row[i]]->getItemRect();
        const QRect nextRect = orderedItems[row[i + 1]]->getItemRect();
        lanes.append({row[i] + 1, true, (currentRect.right() + nextRect.left()) / 2});
    }
    const int trailingIndex = (targetRow + 1 < rows.size()) ? rows[targetRow + 1].first() : orderedItems.size();
    lanes.append({trailingIndex, true, lastRect.right() + 1});

    int bestLane = 0;
    int bestDistance = INT_MAX;
    for (int i = 0; i < lanes.size(); ++i) {
        const int distance = qAbs(contentPos.x() - lanes[i].x);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLane = i;
        }
    }

    if (alignAfterPrev) {
        *alignAfterPrev = lanes[bestLane].afterPrev;
    }
    return lanes[bestLane].insertIndex;
}

QRect LinBox::insertionIndicatorRect() const
{
    if (m_reorderInsertIndex < 0 || !m_contentArea) {
        return QRect();
    }

    QVector<ListItem*> orderedItems = m_items;
    for (ListItem* draggedItem : m_reorderDragItems) {
        orderedItems.removeAll(draggedItem);
    }
    auto itemBorderRectInBox = [this](ListItem* item) -> QRect {
        if (!item) {
            return QRect();
        }
        const QRect contentRect = item->getItemRect();
        const QPoint boxTopLeft = m_contentArea->mapTo(this, contentRect.topLeft());
        return QRect(boxTopLeft, contentRect.size());
    };

    if (m_showType == BoxShowType::List_ShowType) {
        if (orderedItems.isEmpty()) {
            return QRect(5, m_titleHeight + 5, qMax(20, width() - 10), 2);
        }

        if (m_reorderInsertIndex >= orderedItems.size()) {
            const QRect rect = itemBorderRectInBox(orderedItems.last());
            return QRect(rect.left(), rect.bottom() + 2, rect.width(), 2);
        }

        const QRect rect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex]);
        return QRect(rect.left(), rect.top() - 2, rect.width(), 2);
    }

    if (orderedItems.isEmpty()) {
        return QRect(m_contentArea->x() + 5, m_contentArea->y() + 5, 3, 78);
    }

    if (m_reorderInsertIndex <= 0) {
        const QRect firstRect = itemBorderRectInBox(orderedItems.first());
        return QRect(firstRect.left() - 3, firstRect.top(), 3, firstRect.height());
    }

    if (m_reorderInsertIndex >= orderedItems.size()) {
        const QRect lastRect = itemBorderRectInBox(orderedItems.last());
        return QRect(lastRect.right() + 2, lastRect.top(), 3, lastRect.height());
    }

    const QRect prevRect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex - 1]);
    const QRect nextRect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex]);
    if (m_reorderIndicatorAfterPrev) {
        return QRect(prevRect.right() + 2, prevRect.top(), 3, prevRect.height());
    }
    return QRect(nextRect.left() - 3, nextRect.top(), 3, nextRect.height());
}

void LinBox::clearReorderPreview()
{
    if (!m_reorderDragItems.isEmpty() || m_reorderInsertIndex >= 0) {
        m_reorderDragItems.clear();
        m_reorderInsertIndex = -1;
        m_reorderIndicatorAfterPrev = false;
        update();
    }
}

void LinBox::showDropTipForItem(ListItem* item, const QPoint& boxPos, bool isCtrl, bool createShortcut)
{
    if (!item) {
        if (m_dragTip) m_dragTip->hide();
        return;
    }

    if (!m_dragTip) {
        m_dragTip = new FluentDragTip();
    }

    const QString clsid = item->getItemData().getCLSID();
    const bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
    const bool isPC = (clsid == APP_MY_COMPUTER_CLS);
    const bool isNetwork = (clsid == APP_NETWORK_CLS);
    const bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
    const bool isFolder = (item->type() == ObjectType::Folder_ObjectType ||
                           item->type() == ObjectType::SpecialFolder ||
                           item->type() == ObjectType::DiskLetter);

    if (isPC || isNetwork || isControlPanel) {
        m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
    } else {
        QString actionKeyword;
        QString highlightColor = "#70a1ff";
        QString htmlText;
        ListItem* draggedItem = nullptr;
        if (ListView* lv = ListView::instance()) {
            draggedItem = lv->draggedItem();
        }
        const bool isDraggedDiskLetter = (draggedItem && draggedItem->type() == ObjectType::DiskLetter);

        if (isRecycleBin) {
            actionKeyword = tr("Move to");
            highlightColor = "#ff4d4d";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, item->baseName());
        } else if (isFolder && createShortcut) {
            actionKeyword = tr("Create shortcut in");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, item->baseName());
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>鍦?/b> <span>%2</span> <b style='color:%1'>閲屽垱寤洪摼鎺?/b>")
                .arg(highlightColor, item->baseName());
        } else if (isFolder) {
            actionKeyword = isCtrl ? tr("Copy to") : tr("Move to");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, item->baseName());
        } else {
            actionKeyword = tr("Open with");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, item->baseName());
        }
        m_dragTip->setText(htmlText);
    }

    QPoint targetPos = boxPos + QPoint(24, 24);
    if (targetPos.x() + m_dragTip->width() > width()) targetPos.setX(boxPos.x() - m_dragTip->width() - 8);
    if (targetPos.y() + m_dragTip->height() > height()) targetPos.setY(boxPos.y() - m_dragTip->height() - 8);
    m_dragTip->showAt(mapToGlobal(targetPos));
}

void LinBox::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    ListView* lv = ListView::instance();
    const bool isCtrl = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool isInternalDrag = (event->source() != nullptr);
    QStringList paths;
    if (lv) {
        paths = lv->resolveDraggedLocalPaths(event->mimeData());
    }
    ListItem* hitItem = hitItemAt(event->pos());
    const QVector<ListItem*> draggedItems = (isInternalDrag && lv)
        ? lv->orderedDraggedItemsForPaths(paths)
        : QVector<ListItem*>();
    const bool preferShortcutForExternal = !isInternalDrag && lv && lv->shouldCreateShortcutForExternalDrop(paths);

    if (hitItem && !draggedItems.contains(hitItem)) {
        clearReorderPreview();
        const QString clsid = hitItem->getItemData().getCLSID();
        const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
        event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForExternal ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
        event->accept();
        showDropTipForItem(hitItem, event->pos(), isCtrl, preferShortcutForExternal);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (!draggedItems.isEmpty()) {
            lv->resetRenameClickState();
        }
        if (mappedFolderState() != MappedFolderState::Normal) {
            if (m_dragTip) m_dragTip->hide();
            event->ignore();
            return;
        }
        event->setDropAction(preferShortcutForExternal ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction));
        event->accept();
        updateDragTip(event->pos(), isCtrl, preferShortcutForExternal);
        return;
    }

    if (!draggedItems.isEmpty()) {
        lv->resetRenameClickState();
        m_reorderDragItems = draggedItems;
        m_reorderInsertIndex = insertionIndexForPos(event->pos(), draggedItems, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) m_dragTip->hide();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        update();
        return;
    }

    const QString clsid = hitItem ? hitItem->getItemData().getCLSID() : QString();
    const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);

    event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForExternal ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
    event->accept();

    if (hitItem) {
        showDropTipForItem(hitItem, event->pos(), isCtrl, preferShortcutForExternal);
    } else if (isInternalDrag) {
        if (m_dragTip) m_dragTip->hide();
    } else {
        updateDragTip(event->pos(), isCtrl, preferShortcutForExternal);
    }
}

void LinBox::dragLeaveEvent(QDragLeaveEvent* event) {
    if (m_dragTip) m_dragTip->hide();
    clearReorderPreview();
}

void LinBox::dragMoveEvent(QDragMoveEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        if (m_dragTip) m_dragTip->hide();
        event->ignore();
        return;
    }

    ListView* lv = ListView::instance();
    const bool isCtrl = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool isInternalDrag = (event->source() != nullptr);
    QStringList paths;
    if (lv) {
        paths = lv->resolveDraggedLocalPaths(event->mimeData());
    }
    ListItem* hitItem = hitItemAt(event->pos());
    const QVector<ListItem*> draggedItems = (isInternalDrag && lv)
        ? lv->orderedDraggedItemsForPaths(paths)
        : QVector<ListItem*>();
    const bool preferShortcutForExternal = !isInternalDrag && lv && lv->shouldCreateShortcutForExternalDrop(paths);

    if (hitItem && !draggedItems.contains(hitItem)) {
        clearReorderPreview();
        const QString clsid = hitItem->getItemData().getCLSID();
        const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
        event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForExternal ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
        event->accept();
        showDropTipForItem(hitItem, event->pos(), isCtrl, preferShortcutForExternal);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (!draggedItems.isEmpty()) {
            lv->resetRenameClickState();
        }
        if (mappedFolderState() != MappedFolderState::Normal) {
            if (m_dragTip) m_dragTip->hide();
            event->ignore();
            return;
        }
        event->setDropAction(preferShortcutForExternal ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction));
        event->accept();
        updateDragTip(event->pos(), isCtrl, preferShortcutForExternal);
        return;
    }

    if (!draggedItems.isEmpty()) {
        lv->resetRenameClickState();
        m_reorderDragItems = draggedItems;
        m_reorderInsertIndex = insertionIndexForPos(event->pos(), draggedItems, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) m_dragTip->hide();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        update();
        return;
    }

    const QString clsid = hitItem ? hitItem->getItemData().getCLSID() : QString();
    const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);

    event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForExternal ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
    event->accept();

    if (hitItem) {
        showDropTipForItem(hitItem, event->pos(), isCtrl, preferShortcutForExternal);
    } else if (isInternalDrag) {
        if (m_dragTip) m_dragTip->hide();
    } else {
        updateDragTip(event->pos(), isCtrl, preferShortcutForExternal);
    }
}

void LinBox::dropEvent(QDropEvent* event) {
    if (m_dragTip) m_dragTip->hide();
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) {
        clearReorderPreview();
        return;
    }
    
    ListView* lv = ListView::instance();
    if (!lv) return;

    QStringList paths = lv->resolveDraggedLocalPaths(mime);

    if (paths.isEmpty()) {
        event->ignore();
        return;
    }

    const bool isInternalDrag = (event->source() != nullptr);
    const bool copyMode = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool preferShortcutForExternal = !isInternalDrag && lv->shouldCreateShortcutForExternalDrop(paths);
    ListItem* hitItem = hitItemAt(event->pos());

    const QVector<ListItem*> draggedItems = isInternalDrag
        ? lv->orderedDraggedItemsForPaths(paths)
        : QVector<ListItem*>();

    if (hitItem && !draggedItems.contains(hitItem)) {
        clearReorderPreview();
        if (lv->handleDroppedPathsOnItem(paths, hitItem, copyMode, preferShortcutForExternal)) {
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (mappedFolderState() != MappedFolderState::Normal) {
            event->ignore();
            return;
        }

        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const std::vector<std::wstring> srcPaths = toNativeWStringPaths(paths);
        if (targetFolder.isEmpty() || srcPaths.empty()) {
            event->ignore();
            return;
        }

        bool ok = false;
        if (preferShortcutForExternal) {
            ok = lv->createShortcutsInFolder(paths, targetFolder);
        } else if (copyMode) {
            ok = CSystemApi::CopyFiles(srcPaths, targetFolder.toStdWString());
        } else {
            ok = CSystemApi::MoveFiles(srcPaths, targetFolder.toStdWString());
        }

        if (ok && !copyMode && !draggedItems.isEmpty()) {
            QSet<LinBox*> sourceBoxes;
            for (ListItem* draggedItem : draggedItems) {
                if (!draggedItem) {
                    continue;
                }
                if (!draggedItem->boxId().isEmpty()) {
                    if (LinBox* sourceBox = BoxManager::instance()->findBoxById(draggedItem->boxId())) {
                        if (sourceBox && sourceBox != this) {
                            sourceBox->removeListItem(draggedItem, false);
                            sourceBoxes.insert(sourceBox);
                        }
                    }
                }
            }
            for (LinBox* sourceBox : sourceBoxes) {
                sourceBox->refreshLayout();
                sourceBox->update();
            }
        }

        if (ok) {
            refreshLayout();
            update();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(preferShortcutForExternal ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }

    if (!draggedItems.isEmpty()) {
        lv->resetRenameClickState();
        const int insertIndex = insertionIndexForPos(event->pos(), draggedItems, &m_reorderIndicatorAfterPrev);
        clearReorderPreview();

        if (draggedItemsContainMappedSource(draggedItems)) {
            if (lv->importPathsToBox(paths, this, copyMode, insertIndex, preferShortcutForExternal)) {
                event->setDropAction(preferShortcutForExternal ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        bool allFromThisBox = true;
        for (ListItem* draggedItem : draggedItems) {
            if (!m_items.contains(draggedItem)) {
                allFromThisBox = false;
                break;
            }
        }

        if (allFromThisBox) {
            QVector<ListItem*> orderedDraggedItems;
            for (ListItem* existingItem : m_items) {
                if (draggedItems.contains(existingItem)) {
                    orderedDraggedItems.append(existingItem);
                }
            }
            for (ListItem* draggedItem : orderedDraggedItems) {
                m_items.removeAll(draggedItem);
            }
            const int clampedIndex = qBound(0, insertIndex, m_items.size());
            for (int i = 0; i < orderedDraggedItems.size(); ++i) {
                m_items.insert(clampedIndex + i, orderedDraggedItems[i]);
            }
            m_lastLayoutItemCount = -1;
            refreshLayout();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }

        lv->moveItemsToBox(draggedItems, this, insertIndex, true);
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (isInternalDrag) {
        for (const QString& filePath : paths) {
            lv->moveItemToBox(filePath, this, false);
        }

        this->refreshLayout();
        this->raise();

        if (lv->isAutoArrangeEnabled()) {
            lv->loadIconLayer();
        } else {
            lv->update();
        }

        lv->requestSaveBoxes();
        event->accept();
        clearReorderPreview();
        return;
    }

    if (lv->importPathsToBox(paths, this, copyMode, -1, preferShortcutForExternal)) {
        event->accept();
    } else {
        event->ignore();
    }
    clearReorderPreview();
}

void LinBox::keyPressEvent(QKeyEvent* event) {
    ListView* lv = ListView::instance();
    if (lv) {
        const bool extendSelection = event->modifiers() & Qt::ShiftModifier;
        const bool ctrlPressed = event->modifiers() & Qt::ControlModifier;
        const bool shortcutPaste = ctrlPressed && (event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_V;
        if (event->matches(QKeySequence::SelectAll)) {
            lv->applySelectionState(lv->selectableItemsForHost(this), nullptr, false);
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Copy)) {
            lv->writeSelectionToClipboard(false);
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Cut)) {
            lv->writeSelectionToClipboard(true);
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Paste)) {
            lv->clearCutMarks();
            const QStringList filePaths = clipboardLocalFilePathsForBox();
            if (filePaths.isEmpty()) {
                event->accept();
                return;
            }

            const bool isCutOperation = clipboardContainsCutItemsForBox();
            const bool preferShortcut = lv->shouldCreateShortcutForExternalDrop(filePaths);
            bool ok = false;

            if (isFolderMapped()) {
                const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
                const std::vector<std::wstring> srcPaths = toNativeWStringPaths(filePaths);
                if (preferShortcut) {
                    ok = lv->createShortcutsInFolder(filePaths, targetFolder);
                } else if (isCutOperation) {
                    ok = CSystemApi::MoveFiles(srcPaths, targetFolder.toStdWString());
                } else {
                    ok = CSystemApi::CopyFiles(srcPaths, targetFolder.toStdWString());
                }
                if (ok) {
                    lv->reloadMappedFolderBox(this);
                    refreshLayout();
                    update();
                }
            } else {
                ok = lv->importPathsToBox(filePaths, this, !isCutOperation, -1, preferShortcut);
                if (ok) {
                    refreshLayout();
                    update();
                    raise();
                }
            }

            if (ok && isCutOperation) {
                QGuiApplication::clipboard()->clear();
            }
            event->accept();
            return;
        }
        if (shortcutPaste) {
            lv->clearCutMarks();
            const QStringList filePaths = clipboardLocalFilePathsForBox();
            if (filePaths.isEmpty()) {
                event->accept();
                return;
            }

            bool ok = false;
            if (isFolderMapped()) {
                const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
                ok = lv->createShortcutsInFolder(filePaths, targetFolder);
                if (ok) {
                    lv->reloadMappedFolderBox(this);
                    refreshLayout();
                    update();
                }
            } else {
                ok = lv->importPathsToBox(filePaths, this, true, -1, true);
                if (ok) {
                    refreshLayout();
                    update();
                    raise();
                }
            }

            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            lv->clearSelectionState();
            RenameManager::instance()->cancelRename();
            event->accept();
            return;
        }
        if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Return) {
            ListItem* target = lv->focusedItem() ? lv->focusedItem() : (lv->selectedItems().isEmpty() ? nullptr : lv->selectedItems().first());
            if (target && !target->filePath().isEmpty()) {
                CSystemApi::File_RightMenuOperations(reinterpret_cast<const TCHAR*>(QDir::toNativeSeparators(target->filePath()).utf16()), Z_PROPERTIES);
                event->accept();
                return;
            }
        }
        if (ctrlPressed && event->key() == Qt::Key_Space) {
            if (lv->focusedItem()) {
                lv->toggleItemSelection(lv->focusedItem());
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            ListItem* target = lv->focusedItem() ? lv->focusedItem() : (lv->selectedItems().isEmpty() ? nullptr : lv->selectedItems().first());
            if (target) {
                lv->clearCutMarks();
                target->openItem();
                event->accept();
                return;
            }
        }
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
            lv->moveFocusInHost(this,
                event->key() == Qt::Key_Left ? ListView::FocusMoveDirection::Left : ListView::FocusMoveDirection::Up,
                extendSelection,
                ctrlPressed);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
            lv->moveFocusInHost(this,
                event->key() == Qt::Key_Right ? ListView::FocusMoveDirection::Right : ListView::FocusMoveDirection::Down,
                extendSelection,
                ctrlPressed);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_F2 && lv->selectedItems().size() == 1) {
            RenameManager::instance()->startRename(lv->selectedItems().first());
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Delete) {
        if (lv) {
            bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
            lv->onMenuDelete(shiftPressed);
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

bool LinBox::isDragArea(const QPoint& pos) {
    if (m_titleBar->geometry().contains(pos)) {
        QPoint titlePos = m_titleBar->mapFrom(this, pos);
        QWidget* child = m_titleBar->childAt(titlePos);
        // Drag allowed if clicking the title bar itself or an invisible part
        // Exclude specific interaction widgets
        if (child && (child == m_collapseBtn || child == m_viewSwitchBtn || child == m_titleEdit)) {
            return false;
        }
        return true;
    }
    
    // 2. Content area can also initiate drag if background clicked
    if (m_scrollArea->geometry().contains(pos)) {
        QPoint scrollPos = m_scrollArea->mapFrom(this, pos);
        QWidget* child = m_scrollArea->childAt(scrollPos);
        // Drag allowed if click is on scrollarea but NOT on scrollbar or an icon
        if (child == m_scrollArea->verticalScrollBar()) return false;
        
        QPoint contentPos = m_contentArea->mapFrom(this, pos);
        if (m_contentArea->childAt(contentPos) == nullptr) {
            return true;
        }
    }
    
    return false;
}

void LinBox::updateCursorShape(const QPoint& pos) {
    ResizeDir dir = getResizeDir(pos);
    switch (dir) {
        case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
        case Left: case Right: setCursor(Qt::SizeHorCursor); break;
        case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        case Move: setCursor(Qt::ArrowCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
    }
}

void LinBox::toggleCollapse() {
    QFont iconFont("Segoe Fluent Icons");
    bool fontLoaded = QFontInfo(iconFont).exactMatch();
    if (!fontLoaded) {
        iconFont.setFamily("Segoe MDL2 Assets");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Segoe UI Symbol");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Wingdings");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    iconFont.setPixelSize(14);
    m_collapseBtn->setFont(iconFont);

    if (!m_isCollapsed) {
        m_originalHeight = height();
        m_scrollArea->hide(); // Hide scroll area instead of content area directly
        setFixedHeight(m_titleHeight + 2 * m_borderWidth);
        m_collapseBtn->setText(fontLoaded ? u8"\uE70D" : "+");
        m_isCollapsed = true;
    } else {
        m_scrollArea->show();
        setMinimumSize(m_minBoxWidth, m_minBoxHeight);
        setMaximumHeight(16777215);
        resize(width(), m_originalHeight);
        m_collapseBtn->setText(fontLoaded ? u8"\uE70E" : "v");
        m_isCollapsed = false;
    }
    emit geometryChanged(this);
}

void LinBox::setBackgroundColor(const QColor& color) { m_bgColor = color; refreshStyles(); emit geometryChanged(this); }
void LinBox::setBackgroundOpacity(float opacity) { m_bgOpacity = opacity; refreshStyles(); emit geometryChanged(this); }
void LinBox::setGlassEffect(bool enabled) {
    m_glassEffect = enabled;
    if (!enabled) {
        m_blurCache = QPixmap();
    }
    refreshStyles();
    scheduleBlurRefresh();
    emit geometryChanged(this);
}

void LinBox::refreshStyles() {
    // Ensure the window is transparent to show the DWM blur effect
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setStyleSheet("background: transparent; border: none;");

    // Let LinBox::paintEvent render one unified background for the full box.
    // Keeping title/content transparent avoids subtle alpha/color mismatches.
    m_titleBar->setStyleSheet("QWidget#titleBar { background: transparent; border-radius: 0px; }");
    m_contentArea->setStyleSheet("QWidget#contentArea { background: transparent; border-radius: 0px; }");
    update();
}

void LinBox::scheduleBlurRefresh()
{
    if (m_isCapturingBackground || !m_glassEffect || !isVisible() || width() <= 0 || height() <= 0) {
        return;
    }
    if (m_blurRefreshTimer) {
        m_blurRefreshTimer->start();
    }
}

QPixmap LinBox::captureBackgroundPixmap() const
{
    if (width() <= 0 || height() <= 0) {
        return QPixmap();
    }

    QWidget* sourceParent = parentWidget();
    if (sourceParent) {
        const QRect captureRect = geometry();
        QPixmap background(captureRect.size());
        background.fill(Qt::transparent);

        LinBox* self = const_cast<LinBox*>(this);
        self->m_isCapturingBackground = true;

        QPainter painter(&background);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.translate(-captureRect.topLeft());

        sourceParent->render(
            &painter,
            QPoint(),
            QRegion(captureRect),
            QWidget::DrawWindowBackground | QWidget::IgnoreMask);

        const QObjectList childObjects = sourceParent->children();
        for (QObject* childObject : childObjects) {
            QWidget* childWidget = qobject_cast<QWidget*>(childObject);
            if (!childWidget || childWidget == this || !childWidget->isVisible()) {
                continue;
            }

            const QRect childRect = childWidget->geometry();
            if (!childRect.intersects(captureRect)) {
                continue;
            }

            childWidget->render(
                &painter,
                childRect.topLeft(),
                QRegion(),
                QWidget::DrawWindowBackground | QWidget::DrawChildren | QWidget::IgnoreMask);
        }

        self->m_isCapturingBackground = false;
        return background;
    }

    const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
    QScreen* screen = windowHandle() ? windowHandle()->screen() : QApplication::screenAt(globalRect.center());
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    return screen ? screen->grabWindow(0, globalRect.x(), globalRect.y(), globalRect.width(), globalRect.height())
                  : QPixmap();
}

void LinBox::updateBlurCache()
{
    if (!m_glassEffect || !isVisible()) {
        return;
    }

    const QPixmap captured = captureBackgroundPixmap();
    if (captured.isNull()) {
        return;
    }

    const QRect contentRect = rect().adjusted(m_borderWidth, m_borderWidth, -m_borderWidth, -m_borderWidth);
    if (contentRect.width() <= 0 || contentRect.height() <= 0) {
        return;
    }

    const QPixmap contentCapture = captured.copy(contentRect);
    m_blurCache = buildBlurredGlass(contentCapture, m_bgColor, m_bgOpacity);
    update();
}

void LinBox::showCustomContextMenu(const QPoint& globalPos) {
    if (ListView* lv = ListView::instance()) {
        lv->armPendingShellNewTargetBox(this);
    }

    ShellContextMenu scm;
    POINT p = { globalPos.x(), globalPos.y() };
    const POINT* position = &p;
    HWND ownerHwnd = (HWND)this->winId();

    // Update paste state before showing menu
    RightMenu::getInstance()->updatePasteActionsState();
    scm.DisplayBoxBackgroundContextMenu();
    const QString contextPath = isFolderMapped()
        ? normalizeMappedFolderPath(mappedFolderPath())
        : defaultDesktopPath();
    if (!contextPath.isEmpty()) {
        scm.setFilesystemDirectory(contextPath.toStdString());
    }

    std::vector<HBITMAP> tempBitmaps;
    std::vector<ShellContextMenu::CustomMenuItem> items;
    std::vector<ShellContextMenu::CustomMenuItem> boxItems;

    // 1. Add Box-specific context actions
    // Rename Box
    ShellContextMenu::CustomMenuItem renameItem;
    QIcon renameIcon(":/Assets/rename.svg");
    HBITMAP hRenameBmp = QtWin::toHBITMAP(renameIcon.pixmap(16, 16), QtWin::HBitmapPremultipliedAlpha);
    renameItem.hBitmap = hRenameBmp;
    tempBitmaps.push_back(hRenameBmp);
    renameItem.text = tr("Rename Box").toStdWString();
    renameItem.callback = [this]() { this->beginTitleEditing(); };
    boxItems.push_back(renameItem);

    // Toggle View mode
    ShellContextMenu::CustomMenuItem viewItem;
    QIcon viewIcon;
    if(m_showType == BoxShowType::List_ShowType){
        viewIcon = QIcon(":/Assets/list.svg");
    }else{
        viewIcon = QIcon(":/Assets/grid.svg");
    }
    HBITMAP hViewBmp = QtWin::toHBITMAP(viewIcon.pixmap(16, 16), QtWin::HBitmapPremultipliedAlpha);
    viewItem.hBitmap = hViewBmp;
    tempBitmaps.push_back(hViewBmp);
    viewItem.text = (m_showType == BoxShowType::Icon_ShowType ? tr("List View") : tr("Icon View")).toStdWString();
    viewItem.callback = [this]() {
        this->setShowType(this->m_showType == BoxShowType::Icon_ShowType
                        ? BoxShowType::List_ShowType
                        : BoxShowType::Icon_ShowType);
    };
    boxItems.push_back(viewItem);

    // Settings
    ShellContextMenu::CustomMenuItem settingsItem;
    QIcon settingsIcon(":/Assets/settings.svg");
    HBITMAP hSettingsBmp = QtWin::toHBITMAP(settingsIcon.pixmap(16, 16), QtWin::HBitmapPremultipliedAlpha);
    settingsItem.hBitmap = hSettingsBmp;
    tempBitmaps.push_back(hSettingsBmp);
    settingsItem.text = tr("Box Settings").toStdWString();
    settingsItem.callback = [this]() {
        if (ListView::instance()) {
            ListView::instance()->showSettings(this);
        }
    };
    boxItems.push_back(settingsItem);

    // Destroy Box
    ShellContextMenu::CustomMenuItem destroyItem;
    destroyItem.text = tr("Destroy Box").toStdWString();
    QIcon icon(":/Assets/destroy.svg");
    HBITMAP hBmp = QtWin::toHBITMAP(icon.pixmap(16, 16), QtWin::HBitmapPremultipliedAlpha);
    destroyItem.hBitmap = hBmp;
    tempBitmaps.push_back(hBmp);
    destroyItem.callback = [this]() { emit destroyRequested(this); };
    boxItems.push_back(destroyItem);

    // 2. Get shared actions from RightMenu and convert them
    if (QMenu* sharedMenu = RightMenu::getInstance()->getCustomMenu()) {
        auto sharedItems = RightMenu::exportCustomMenuItems(sharedMenu, tempBitmaps, false, true);
        if (!sharedItems.empty()) {
            items.insert(items.end(), sharedItems.begin(), sharedItems.end());
        }
    }

    if (!items.empty() && !boxItems.empty()) {
        ShellContextMenu::CustomMenuItem sep;
        sep.isSeparator = true;
        items.push_back(sep);
    }

    for (auto& boxItem : boxItems) {
        items.push_back(std::move(boxItem));
    }

    // Set custom items and show the native menu
    scm.setCustomMenuItems(items);
    scm.ShowMenu(ownerHwnd, position, nullptr);

    // Clean up temporary bitmaps used for icons
    for (HBITMAP h : tempBitmaps) {
        DeleteObject(h);
    }
}

void LinBox::updateDragTip(const QPoint& pos, bool isCtrl, bool createShortcut) {
    if (!m_dragTip) {
        m_dragTip = new FluentDragTip();
    }

    QString targetText = tr("this Box");
    if (isFolderMapped()) {
        const QFileInfo mappedInfo(m_mappedFolderPath);
        const QString folderName = mappedInfo.fileName().isEmpty() ? m_mappedFolderPath : mappedInfo.fileName();
        targetText = folderName;
    }

    if (createShortcut) {
        m_dragTip->setText(QString("<b style='color:#70a1ff'>%1</b> <span>%2</span>").arg(tr("Create shortcut in"), targetText));
    } else {
        m_dragTip->setText(isCtrl
            ? QString("<b style='color:#70a1ff'>%1</b> <span>%2</span>").arg(tr("Copy to"), targetText)
            : QString("<b style='color:#70a1ff'>%1</b> <span>%2</span>").arg(tr("Move to"), targetText));
    }

    QPoint targetPos = pos + QPoint(24, 24);
    if (targetPos.x() + m_dragTip->width() > width()) targetPos.setX(pos.x() - m_dragTip->width() - 8);
    if (targetPos.y() + m_dragTip->height() > height()) targetPos.setY(pos.y() - m_dragTip->height() - 8);

    m_dragTip->showAt(mapToGlobal(targetPos));
}

