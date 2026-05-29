#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "desktop/ListView.h"
#include "desktop/LinDesktop.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"
#include "app/AppFonts.h"
#include "desktop/ListViewMappedBoxHelper.h"
#include "models/DesktopQuadTree.h"
#include "ui/SettingUI.h"
#include <QPushButton>
#include <QtMath>
#include <QProcess>
#include <QStandardPaths>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QCursor>
#include "SystemApi/SystemApi.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <QInputDialog>
#include <QMenu>
#include <QMoveEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <climits>
#include <shobjidl.h>
#include "../OS/SystemApi/resources.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInfoTip.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "desktop/ListViewSelectionSnapshot.h"
#include "box/LinBox.h"
#include "plugins/PluginManager.h"
#include "services/HotkeyManager.h"
#include "services/AppPaths.h"
#include "services/IconCacheManager.h"
#include "services/SpaceLookIpcClient.h"
#include "services/DpiScaleHelper.h"
#include "box/BoxManager.h"
#include "desktop/ListViewDebugPanel.h"
#include "desktop/DesktopSnakeGame.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"

namespace {
constexpr double kLinWidgetCellBlockThreshold = 0.15;

QString normalizeDesktopPath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path);
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QString normalizeMappedFolderPathForLookup(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(QDir(path).absolutePath());
    if (normalized.length() == 2 && normalized[1] == ':') {
        normalized += '\\';
    } else if (normalized.length() > 3) {
        while (normalized.endsWith('\\') || normalized.endsWith('/')) {
            normalized.chop(1);
        }
    }
    return normalized;
}

constexpr int kDesktopIconTextGap = 8;
constexpr int kBackgroundSelectionThrottleFastMs = 8;
constexpr int kBackgroundSelectionThrottleBalancedMs = 16;
constexpr int kBackgroundSelectionThrottleBusyMs = 32;
constexpr int kBackgroundSelectionThrottleMaxMs = 48;
constexpr int kBackgroundSelectionThrottleRelaxFrameThreshold = 3;

int iconCellHeightForTwoTextLines(int iconSide)
{
    QFont labelFont = AppFonts::uiFont(9);

    const QFontMetrics metrics(labelFont);
    const int twoLineTextHeight = metrics.lineSpacing();
    return iconSide + twoLineTextHeight;
}

float normalizedSavedBoxOpacity(const QVariant& value)
{
    const double rawOpacity = value.toDouble();
    if (rawOpacity <= 0.0) {
        return 0.45f;
    }

    if (rawOpacity > 1.0) {
        return static_cast<float>(qBound(0.0, rawOpacity / 100.0, 1.0));
    }

    return static_cast<float>(qBound(0.0, rawOpacity, 1.0));
}

float clampedRestoredBoxOpacity(float opacity)
{
    if (!qIsFinite(opacity)) {
        return 0.45f;
    }
    return qBound(0.0f, opacity, 1.0f);
}

LinBox::BoxShowType restoredBoxShowType(int showType)
{
    if (showType == static_cast<int>(LinBox::BoxShowType::List_ShowType)) {
        return LinBox::BoxShowType::List_ShowType;
    }
    return LinBox::BoxShowType::Icon_ShowType;
}

QRect clampedRestoredBoxGeometry(const QRect& geometry)
{
    constexpr int kMinBoxWidth = 180;
    constexpr int kMinBoxHeight = 120;
    constexpr int kFallbackBoxWidth = 360;
    constexpr int kFallbackBoxHeight = 260;
    constexpr int kScreenMargin = 8;

    QRect safeGeometry = geometry;
    if (!safeGeometry.isValid() || safeGeometry.width() <= 0 || safeGeometry.height() <= 0) {
        safeGeometry = QRect(QPoint(80, 80), QSize(kFallbackBoxWidth, kFallbackBoxHeight));
    }

    safeGeometry.setWidth(qMax(kMinBoxWidth, safeGeometry.width()));
    safeGeometry.setHeight(qMax(kMinBoxHeight, safeGeometry.height()));

    QScreen* screen = QGuiApplication::screenAt(safeGeometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return safeGeometry;
    }

    QRect bounds = screen->availableGeometry().adjusted(kScreenMargin, kScreenMargin, -kScreenMargin, -kScreenMargin);
    if (bounds.width() < kMinBoxWidth || bounds.height() < kMinBoxHeight) {
        bounds = screen->availableGeometry();
    }

    safeGeometry.setWidth(qMin(safeGeometry.width(), qMax(kMinBoxWidth, bounds.width())));
    safeGeometry.setHeight(qMin(safeGeometry.height(), qMax(kMinBoxHeight, bounds.height())));

    if (!bounds.intersects(safeGeometry)) {
        safeGeometry.moveCenter(bounds.center());
    }
    if (safeGeometry.left() < bounds.left()) {
        safeGeometry.moveLeft(bounds.left());
    }
    if (safeGeometry.top() < bounds.top()) {
        safeGeometry.moveTop(bounds.top());
    }
    if (safeGeometry.right() > bounds.right()) {
        safeGeometry.moveRight(bounds.right());
    }
    if (safeGeometry.bottom() > bounds.bottom()) {
        safeGeometry.moveBottom(bounds.bottom());
    }

    return safeGeometry;
}

#ifdef Q_OS_WIN
QPoint nativeCursorPosition()
{
    POINT cursor = {};
    GetCursorPos(&cursor);
    return QPoint(cursor.x, cursor.y);
}

QRect nativeWindowRect(QWidget* widget)
{
    if (!widget) {
        return {};
    }

    RECT rect = {};
    if (!GetWindowRect(reinterpret_cast<HWND>(widget->winId()), &rect)) {
        return {};
    }

    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

UINT nativeWindowDpi(QWidget* widget)
{
    if (!widget) {
        return 0;
    }

    return GetDpiForWindow(reinterpret_cast<HWND>(widget->winId()));
}

void logListViewDpiTrace(const char* source, QWidget* widget, const QPoint& localPos, const QPoint& globalPos)
{
    QScreen* widgetScreen = widget ? widget->screen() : nullptr;
    qDebug() << "[DpiTrace][ListView]"
             << "source=" << source
             << "local=" << localPos
             << "qtGlobal=" << globalPos
             << "mapToGlobal=" << (widget ? widget->mapToGlobal(localPos) : QPoint())
             << "qtCursor=" << QCursor::pos()
             << "nativeCursor=" << nativeCursorPosition()
             << "widgetDpr=" << (widget ? widget->devicePixelRatioF() : 0.0)
             << "screenDpr=" << (widgetScreen ? widgetScreen->devicePixelRatio() : 0.0)
             << "screenGeometry=" << (widgetScreen ? widgetScreen->geometry() : QRect())
             << "screenAvailable=" << (widgetScreen ? widgetScreen->availableGeometry() : QRect())
             << "nativeWindowRect=" << nativeWindowRect(widget)
             << "nativeDpi=" << nativeWindowDpi(widget);
}
#endif

quint64 packGridCell(int gridX, int gridY)
{
    return ((quint64)(uint)gridX << 32) | (quint64)(uint)gridY;
}

QPoint pointToGridCell(const QPoint& pos, int marginLeft, int marginTop, int strideX, int strideY)
{
    const int gridX = qMax(0, qRound((double)(pos.x() - marginLeft) / qMax(1, strideX)));
    const int gridY = qMax(0, qRound((double)(pos.y() - marginTop) / qMax(1, strideY)));
    return QPoint(gridX, gridY);
}

QPoint gridCellToPoint(int gridX, int gridY, int marginLeft, int marginTop, int strideX, int strideY)
{
    return QPoint(marginLeft + gridX * strideX, marginTop + gridY * strideY);
}

int maxGridRowsForHeight(int viewHeight, int marginTop, int bottomPadding, int iconHeight, int strideY)
{
    int rows = 0;
    int gridY = 0;
    while (marginTop + gridY * strideY <= viewHeight - bottomPadding - iconHeight) {
        ++rows;
        ++gridY;
    }
    return qMax(1, rows);
}

int maxGridColsForWidth(int viewWidth, int marginLeft, int iconWidth, int strideX)
{
    int cols = 0;
    int gridX = 0;
    while (marginLeft + gridX * strideX <= viewWidth - iconWidth) {
        ++cols;
        ++gridX;
    }
    return qMax(1, cols);
}

QPoint gridIndexToCell(int linearIndex, int maxRows)
{
    const int safeRows = qMax(1, maxRows);
    return QPoint(linearIndex / safeRows, linearIndex % safeRows);
}

int pointToGridLinearIndex(const QPoint& pos, int marginLeft, int marginTop, int strideX, int strideY, int maxRows)
{
    const int safeStrideX = qMax(1, strideX);
    const int safeStrideY = qMax(1, strideY);
    const int safeRows = qMax(1, maxRows);

    int gridX = qMax(0, qFloor(static_cast<double>(pos.x() - marginLeft) / safeStrideX));
    int gridY = qMax(0, qFloor(static_cast<double>(pos.y() - marginTop) / safeStrideY));

    gridX += gridY / safeRows;
    gridY %= safeRows;
    return gridX * safeRows + gridY;
}

bool isPathFromMappedBox(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return sourceBox->isFolderMapped();
    }
    return false;
}

bool draggedPathsContainMappedSource(const QStringList& paths)
{
    for (const QString& path : paths) {
        if (isPathFromMappedBox(path)) {
            return true;
        }
    }
    return false;
}

bool isBoxDragSource(const QObject* source)
{
    return qobject_cast<const LinBox*>(source) != nullptr;
}

bool isMappedBoxDragSource(const QObject* source)
{
    const auto* box = qobject_cast<const LinBox*>(source);
    return box && box->isFolderMapped();
}

bool isNormalBoxDragSource(const QObject* source)
{
    const auto* box = qobject_cast<const LinBox*>(source);
    return box && !box->isFolderMapped();
}

bool shouldSkipLinWidgetResourceEntry(const QString& name)
{
    return name == QStringLiteral(".webview2") ||
        name == QStringLiteral(".git") ||
        name == QStringLiteral("node_modules") ||
        name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0;
}

bool copyLinWidgetResourceDirectory(const QString& sourcePath, const QString& targetPath)
{
    const QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }

    QDir().mkpath(targetPath);
    bool copiedAny = false;
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (shouldSkipLinWidgetResourceEntry(entry.fileName())) {
            continue;
        }

        const QString targetEntryPath = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir()) {
            copiedAny = copyLinWidgetResourceDirectory(entry.absoluteFilePath(), targetEntryPath) || copiedAny;
            continue;
        }
        if (QFileInfo::exists(targetEntryPath)) {
            QFile::remove(targetEntryPath);
        }
        copiedAny = QFile::copy(entry.absoluteFilePath(), targetEntryPath) || copiedAny;
    }
    return copiedAny;
}

QString runtimeLinWidgetsDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
}

bool isMappedDragStillInsideSourceBox(const QObject* source, const QPoint& globalPos)
{
    const auto* box = qobject_cast<const LinBox*>(source);
    if (!box || !box->isFolderMapped()) {
        return false;
    }

    const QRect sourceGlobalRect(box->mapToGlobal(QPoint(0, 0)), box->size());
    return sourceGlobalRect.contains(globalPos);
}

void resetStateForDesktopView(DesktopRootItemState& state, int iconWidth, int iconHeight)
{
    state.model.boxId.clear();
    state.model.visible = true;
    state.model.layoutMode = DesktopLayoutMode::IconMode;
    state.model.baseSize = QSize(iconWidth, iconHeight);
    state.model.effectiveSize = state.model.baseSize;
    state.model.visibleTextHeight = 0;
    state.model.isSelected = false;
    state.model.isHovered = false;
    state.model.focused = false;
    state.model.renderData = DesktopRootItemRenderData();
    state.model.renderData.selected = false;
    state.model.renderData.hovered = false;
    state.renderDataDirty = true;
}

void logDesktopLayoutMetrics(const char* tag,
                             const ListView* view,
                             int layoutLeft,
                             int layoutTop,
                             int usableRight,
                             int usableBottom,
                             int itemWidth,
                             int itemHeight,
                             int strideX,
                             int strideY)
{
    qDebug().nospace()
        << "[DesktopLayout][" << tag << "] "
        << "viewMode=" << static_cast<int>(view->viewMode())
        << " layoutLeft=" << layoutLeft
        << " layoutTop=" << layoutTop
        << " usableRight=" << usableRight
        << " usableBottom=" << usableBottom
        << " itemWidth=" << itemWidth
        << " itemHeight=" << itemHeight
        << " strideX=" << strideX
        << " strideY=" << strideY;
}

}

int maxGridRowsForBounds(int layoutTop, int usableBottom, int itemHeight, int strideY);
int maxGridColsForBounds(int layoutLeft, int usableRight, int itemWidth, int strideX);

QRect ListView::desktopLayoutBounds() const
{
    QScreen* activeScreen = screen();
    if (!activeScreen && windowHandle()) {
        activeScreen = windowHandle()->screen();
    }
    if (!activeScreen) {
        activeScreen = QApplication::primaryScreen();
    }
    if (!activeScreen) {
        return rect();
    }

    const QRect globalViewRect(mapToGlobal(QPoint(0, 0)), size());
    const QRect usableGlobalRect = activeScreen->availableGeometry().intersected(globalViewRect);
    if (!usableGlobalRect.isValid() || usableGlobalRect.isEmpty()) {
        return rect();
    }

    const QRect usableLocalRect(usableGlobalRect.topLeft() - globalViewRect.topLeft(), usableGlobalRect.size());
    return usableLocalRect.intersected(rect());
}

int ListView::desktopLayoutLeft() const
{
    return qMax(m_marginLeft, desktopLayoutBounds().left());
}

int ListView::desktopLayoutTop() const
{
    return qMax(m_marginTop, desktopLayoutBounds().top());
}

int ListView::desktopUsableRight() const
{
    const QRect bounds = desktopLayoutBounds();
    return qMax(desktopLayoutLeft(), qMin(width() - 1, bounds.right()));
}

int ListView::desktopUsableBottom() const
{
    const QRect bounds = desktopLayoutBounds();
    return qMax(desktopLayoutTop(), qMin(height() - 1, bounds.bottom() - m_bottomPadding));
}

int ListView::desktopItemFootprintHeight() const
{
    if (m_iconShowRule.viewMode == ViewMode::SmallIcon) {
        const int iconSide = scalePx(32);
        const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
        return scalePx(1) + iconSide + scalePx(kDesktopIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
    }

    if (m_iconShowRule.viewMode == ViewMode::LargeIcon) {
        const int iconSide = scalePx(96);
        const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
        return scalePx(1) + iconSide + scalePx(kDesktopIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
    }

    const int iconSide = scalePx(48);
    const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
    return scalePx(1) + iconSide + scalePx(kDesktopIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
}

QVector<QRect> ListView::desktopLinWidgetOccupiedRects() const
{
    QVector<QRect> rects;
    if (!m_linWidgetManager) {
        return rects;
    }

    for (const LinWidget::WidgetLayoutState& state : m_linWidgetManager->layoutStates()) {
        if (!state.visible || state.geometry.isEmpty() || !state.geometry.isValid()) {
            continue;
        }
        rects.append(state.geometry);
    }
    return rects;
}

QSet<quint64> ListView::desktopLinWidgetOccupiedCells() const
{
    QSet<quint64> cells;
    const QVector<QRect> widgetRects = desktopLinWidgetOccupiedRects();
    if (widgetRects.isEmpty()) {
        return cells;
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));

    const QSize itemSize(m_iconWidth, itemHeight);
    for (const QRect& widgetRect : widgetRects) {
        for (int x = 0; x < maxCols; ++x) {
            for (int y = 0; y < maxRows; ++y) {
                const QRect itemRect(gridCellToPoint(x, y, layoutLeft, layoutTop, strideX, strideY), itemSize);
                const QRect overlap = itemRect.intersected(widgetRect);
                if (overlap.isEmpty()) {
                    continue;
                }
                const double xOverlapRatio = static_cast<double>(overlap.width()) / static_cast<double>(qMax(1, itemSize.width()));
                const double yOverlapRatio = static_cast<double>(overlap.height()) / static_cast<double>(qMax(1, itemSize.height()));
                if (xOverlapRatio <= kLinWidgetCellBlockThreshold || yOverlapRatio <= kLinWidgetCellBlockThreshold) {
                    continue;
                }
                cells.insert(packGridCell(x, y));
            }
        }
    }

    return cells;
}

QVector<QRect> ListView::desktopBlockedOccupiedRects() const
{
    return desktopLinWidgetOccupiedRects();
}

QSet<quint64> ListView::desktopBlockedOccupiedCells() const
{
    return desktopLinWidgetOccupiedCells();
}

void ListView::paintLinWidgetBlockedCellsOverlay(QPainter& painter, const QRegion& dirtyRegion) const
{
    if (!m_showLinWidgetBlockedCells) {
        return;
    }

    const QSet<quint64> cells = desktopLinWidgetOccupiedCells();
    if (cells.isEmpty()) {
        return;
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(220, 38, 38, 190), 1));
    painter.setBrush(QColor(239, 68, 68, 58));

    for (int x = 0; x < maxCols; ++x) {
        for (int y = 0; y < maxRows; ++y) {
            if (!cells.contains(packGridCell(x, y))) {
                continue;
            }

            const QRect cellRect(gridCellToPoint(x, y, layoutLeft, layoutTop, strideX, strideY),
                                 QSize(m_iconWidth, itemHeight));
            if (!dirtyRegion.intersects(cellRect)) {
                continue;
            }

            painter.drawRoundedRect(cellRect.adjusted(1, 1, -1, -1), 5, 5);
        }
    }

    painter.setPen(QPen(QColor(14, 165, 233, 230), 2));
    painter.setBrush(QColor(14, 165, 233, 34));
    for (const QRect& widgetRect : desktopLinWidgetOccupiedRects()) {
        if (!dirtyRegion.intersects(widgetRect)) {
            continue;
        }
        painter.drawRoundedRect(widgetRect.adjusted(1, 1, -1, -1), 6, 6);
    }

    painter.restore();
}

QPoint ListView::resolveNearestGridCreatePosition(const QPoint& requestedPos, const QSet<quint64>& occupiedSlots) const
{
    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));
    const int visibleCapacity = qMax(1, maxRows * maxCols);
    const QPoint preferredCell = pointToGridCell(requestedPos, layoutLeft, layoutTop, strideX, strideY);
    const QPoint clampedPreferred(qBound(0, preferredCell.x(), maxCols - 1),
                                  qBound(0, preferredCell.y(), maxRows - 1));

    if (occupiedSlots.size() < visibleCapacity) {
        QVector<QPair<int, QPoint>> candidates;
        const int maxRadius = qMax(maxCols, maxRows);
        for (int radius = 0; radius <= maxRadius; ++radius) {
            candidates.clear();
            const int minX = qMax(0, clampedPreferred.x() - radius);
            const int maxX = qMin(maxCols - 1, clampedPreferred.x() + radius);
            const int minY = qMax(0, clampedPreferred.y() - radius);
            const int maxY = qMin(maxRows - 1, clampedPreferred.y() + radius);

            for (int x = minX; x <= maxX; ++x) {
                for (int y = minY; y <= maxY; ++y) {
                    if (qMax(qAbs(x - clampedPreferred.x()), qAbs(y - clampedPreferred.y())) != radius) {
                        continue;
                    }
                    if (occupiedSlots.contains(packGridCell(x, y))) {
                        continue;
                    }
                    const int dx = x - clampedPreferred.x();
                    const int dy = y - clampedPreferred.y();
                    candidates.append(qMakePair(dx * dx + dy * dy, QPoint(x, y)));
                }
            }

            if (!candidates.isEmpty()) {
                std::sort(candidates.begin(), candidates.end(), [maxRows](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                    if (a.first != b.first) {
                        return a.first < b.first;
                    }
                    const int indexA = a.second.x() * maxRows + a.second.y();
                    const int indexB = b.second.x() * maxRows + b.second.y();
                    return indexA < indexB;
                });
                const QPoint cell = candidates.first().second;
                return gridCellToPoint(cell.x(), cell.y(), layoutLeft, layoutTop, strideX, strideY);
            }
        }
    }

    const QPoint overflowCell = gridIndexToCell(occupiedSlots.size() % visibleCapacity, maxRows);
    return gridCellToPoint(overflowCell.x(), overflowCell.y(), layoutLeft, layoutTop, strideX, strideY);
}

QPoint ListView::resolveNearestFreeCreatePosition(const QPoint& requestedPos) const
{
    const int itemHeight = desktopItemFootprintHeight();
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int minX = layoutLeft;
    const int minY = layoutTop;
    const int maxX = qMax(minX, desktopUsableRight() - m_iconWidth + 1);
    const int maxY = qMax(minY, desktopUsableBottom() - itemHeight + 1);
    const QPoint clampedPos(qBound(minX, requestedPos.x(), maxX),
                            qBound(minY, requestedPos.y(), maxY));
    const QSize itemSize(m_iconWidth, itemHeight);
    const QVector<QRect> componentRects = desktopBlockedOccupiedRects();

    auto isFree = [&](const QPoint& candidatePos) {
        const QRect candidateRect(candidatePos, itemSize);
        for (const QRect& componentRect : componentRects) {
            if (candidateRect.intersects(componentRect)) {
                return false;
            }
        }
        for (const auto& state : m_itemStates) {
            if (!state.model.boxId.isEmpty() || !state.model.visible) {
                continue;
            }
            const QRect stateRect(state.model.position, itemSize);
            if (candidateRect.intersects(stateRect)) {
                return false;
            }
        }
        for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
            const QRect pendingRect(it.value(), itemSize);
            if (candidateRect.intersects(pendingRect)) {
                return false;
            }
        }
        return true;
    };

    if (isFree(clampedPos)) {
        return clampedPos;
    }

    const int step = 16;
    const int maxRadius = qMax(maxX - minX, maxY - minY) + step;
    for (int radius = step; radius <= maxRadius; radius += step) {
        QVector<QPair<int, QPoint>> candidates;
        for (int dx = -radius; dx <= radius; dx += step) {
            for (int dy = -radius; dy <= radius; dy += step) {
                if (qMax(qAbs(dx), qAbs(dy)) != radius) {
                    continue;
                }
                const QPoint candidate(qBound(minX, clampedPos.x() + dx, maxX),
                                       qBound(minY, clampedPos.y() + dy, maxY));
                if (!isFree(candidate)) {
                    continue;
                }
                candidates.append(qMakePair(dx * dx + dy * dy, candidate));
            }
        }
        if (!candidates.isEmpty()) {
            std::sort(candidates.begin(), candidates.end(), [](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                if (a.first != b.first) {
                    return a.first < b.first;
                }
                if (a.second.y() != b.second.y()) {
                    return a.second.y() < b.second.y();
                }
                return a.second.x() < b.second.x();
            });
            return candidates.first().second;
        }
    }

    return clampedPos;
}

QPoint ListView::localPosFromGhostWidget(const QWidget* ghostWidget) const
{
    if (!ghostWidget) {
        return QPoint();
    }
    return mapFromGlobal(ghostWidget->pos());
}

bool ListView::rectIntersectsDesktopBlockedArea(const QRect& rect) const
{
    for (const QRect& componentRect : desktopBlockedOccupiedRects()) {
        if (rect.intersects(componentRect)) {
            return true;
        }
    }
    return false;
}

QPoint ListView::resolveNearestGridAreaForDrag(const QVector<QPoint>& relativeCells,
                                               const QPoint& preferredTopLeftCell,
                                               const QSet<quint64>& occupiedSlots) const
{
    if (relativeCells.isEmpty()) {
        return QPoint(-1, -1);
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));

    Q_UNUSED(strideX);
    Q_UNUSED(strideY);
    Q_UNUSED(layoutLeft);

    int groupWidth = 1;
    int groupHeight = 1;
    QSet<quint64> uniqueRelativeSlots;
    QVector<QPoint> uniqueRelativeCells;
    uniqueRelativeCells.reserve(relativeCells.size());

    for (const QPoint& cell : relativeCells) {
        if (cell.x() < 0 || cell.y() < 0) {
            continue;
        }
        const quint64 key = packGridCell(cell.x(), cell.y());
        if (uniqueRelativeSlots.contains(key)) {
            continue;
        }
        uniqueRelativeSlots.insert(key);
        uniqueRelativeCells.append(cell);
        groupWidth = qMax(groupWidth, cell.x() + 1);
        groupHeight = qMax(groupHeight, cell.y() + 1);
    }

    if (uniqueRelativeCells.isEmpty() || groupWidth > maxCols || groupHeight > maxRows) {
        return QPoint(-1, -1);
    }

    const QPoint clampedPreferred(qBound(0, preferredTopLeftCell.x(), maxCols - groupWidth),
                                  qBound(0, preferredTopLeftCell.y(), maxRows - groupHeight));

    auto candidateIsFree = [&](const QPoint& topLeftCell) {
        if (topLeftCell.x() < 0 || topLeftCell.y() < 0 ||
            topLeftCell.x() + groupWidth > maxCols ||
            topLeftCell.y() + groupHeight > maxRows) {
            return false;
        }

        for (const QPoint& relativeCell : uniqueRelativeCells) {
            const QPoint cell = topLeftCell + relativeCell;
            if (occupiedSlots.contains(packGridCell(cell.x(), cell.y()))) {
                return false;
            }
        }
        return true;
    };

    if (candidateIsFree(clampedPreferred)) {
        return clampedPreferred;
    }

    const int maxRadius = qMax(maxCols, maxRows);
    for (int radius = 1; radius <= maxRadius; ++radius) {
        QVector<QPair<int, QPoint>> candidates;
        const int minX = qMax(0, clampedPreferred.x() - radius);
        const int maxX = qMin(maxCols - groupWidth, clampedPreferred.x() + radius);
        const int minY = qMax(0, clampedPreferred.y() - radius);
        const int maxY = qMin(maxRows - groupHeight, clampedPreferred.y() + radius);

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                if (qMax(qAbs(x - clampedPreferred.x()), qAbs(y - clampedPreferred.y())) != radius) {
                    continue;
                }
                const QPoint candidate(x, y);
                if (!candidateIsFree(candidate)) {
                    continue;
                }
                const int dx = x - clampedPreferred.x();
                const int dy = y - clampedPreferred.y();
                candidates.append(qMakePair(dx * dx + dy * dy, candidate));
            }
        }

        if (!candidates.isEmpty()) {
            std::sort(candidates.begin(), candidates.end(), [maxRows](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                if (a.first != b.first) {
                    return a.first < b.first;
                }
                const int indexA = a.second.x() * maxRows + a.second.y();
                const int indexB = b.second.x() * maxRows + b.second.y();
                return indexA < indexB;
            });
            return candidates.first().second;
        }
    }

    return QPoint(-1, -1);
}

QMap<QString, QPoint> ListView::resolveDesktopDragFinalPositions(const QStringList& draggedPaths,
                                                                 const QPoint& releasePos,
                                                                 const QHash<QString, QPoint>& previewPositions) const
{
    QMap<QString, QPoint> result;
    QSet<QString> draggedPathSet;
    QVector<QPair<QString, const DesktopRootItemState*>> draggedStates;

    for (const QString& path : draggedPaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty() || draggedPathSet.contains(normalizedPath)) {
            continue;
        }
        const DesktopRootItemState* state = findDesktopStateByPath(normalizedPath);
        if (!state || !shouldPaintDesktopModelState(*state)) {
            continue;
        }
        draggedPathSet.insert(normalizedPath);
        draggedStates.append(qMakePair(normalizedPath, state));
    }

    if (draggedStates.isEmpty()) {
        return result;
    }

    const int itemHeight = desktopItemFootprintHeight();
    const QSize itemSize(m_iconWidth, itemHeight);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int usableRight = desktopUsableRight();
    const int usableBottom = desktopUsableBottom();
    const int minX = layoutLeft;
    const int minY = layoutTop;
    const int maxX = qMax(minX, usableRight - m_iconWidth + 1);
    const int maxY = qMax(minY, usableBottom - itemHeight + 1);

    QHash<QString, QPoint> ghostLocalPositions = previewPositions;
    if (ghostLocalPositions.isEmpty()) {
        for (const GhostInfo& ghost : m_ghosts) {
            if (!ghost.widget || ghost.filePath.isEmpty()) {
                continue;
            }
            const QString normalizedPath = normalizeDesktopItemPath(ghost.filePath);
            if (!normalizedPath.isEmpty()) {
                ghostLocalPositions.insert(normalizedPath, localPosFromGhostWidget(ghost.widget));
            }
        }
    }

    if (m_iconShowRule.gridAlignment) {
        const int strideX = qMax(1, m_iconWidth + m_colSpacing);
        const int strideY = qMax(1, itemHeight + m_rowSpacing);
        const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, usableBottom, itemHeight, strideY));
        const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, usableRight, m_iconWidth, strideX));

        auto clampCellToBounds = [&](const QPoint& cell) {
            return QPoint(qBound(0, cell.x(), maxCols - 1),
                          qBound(0, cell.y(), maxRows - 1));
        };
        auto normalizedCellForPos = [&](const QPoint& pos) {
            return clampCellToBounds(pointToGridCell(pos, layoutLeft, layoutTop, strideX, strideY));
        };

        QVector<QPair<QString, QPoint>> relativeCellsByPath;
        QVector<QPoint> relativeCells;
        int originalMinCellX = INT_MAX;
        int originalMinCellY = INT_MAX;

        QHash<QString, QPoint> originalCells;
        for (const auto& pair : draggedStates) {
            const QPoint cell = normalizedCellForPos(pair.second->model.position);
            originalCells.insert(pair.first, cell);
            originalMinCellX = qMin(originalMinCellX, cell.x());
            originalMinCellY = qMin(originalMinCellY, cell.y());
        }

        int previewMinCellX = INT_MAX;
        int previewMinCellY = INT_MAX;
        bool hasPreviewCells = false;
        for (const auto& pair : draggedStates) {
            const QPoint previewPos = ghostLocalPositions.value(pair.first, pair.second->model.position);
            const QPoint previewCell = normalizedCellForPos(previewPos);
            previewMinCellX = qMin(previewMinCellX, previewCell.x());
            previewMinCellY = qMin(previewMinCellY, previewCell.y());
            hasPreviewCells = true;
        }

        for (const auto& pair : draggedStates) {
            const QPoint originalCell = originalCells.value(pair.first);
            const QPoint relativeCell(originalCell.x() - originalMinCellX,
                                      originalCell.y() - originalMinCellY);
            relativeCellsByPath.append(qMakePair(pair.first, relativeCell));
            relativeCells.append(relativeCell);
        }

        QSet<quint64> occupiedSlots = desktopBlockedOccupiedCells();
        for (const auto& state : m_itemStates) {
            if (!shouldPaintDesktopModelState(state)) {
                continue;
            }
            const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
            if (draggedPathSet.contains(normalizedPath)) {
                continue;
            }
            const QPoint cell = normalizedCellForPos(state.model.position);
            occupiedSlots.insert(packGridCell(cell.x(), cell.y()));
        }
        for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
            const QPoint cell = normalizedCellForPos(it.value());
            occupiedSlots.insert(packGridCell(cell.x(), cell.y()));
        }

        const QPoint preferredTopLeftCell = hasPreviewCells
            ? QPoint(previewMinCellX, previewMinCellY)
            : normalizedCellForPos(releasePos);
        const QPoint resolvedTopLeftCell = resolveNearestGridAreaForDrag(relativeCells, preferredTopLeftCell, occupiedSlots);
        if (resolvedTopLeftCell.x() < 0 || resolvedTopLeftCell.y() < 0) {
            return QMap<QString, QPoint>();
        }

        for (const auto& pair : relativeCellsByPath) {
            const QPoint cell = resolvedTopLeftCell + pair.second;
            result.insert(pair.first, gridCellToPoint(cell.x(), cell.y(), layoutLeft, layoutTop, strideX, strideY));
        }
        return result;
    }

    QVector<QRect> occupiedRects = desktopBlockedOccupiedRects();
    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (draggedPathSet.contains(normalizedPath)) {
            continue;
        }
        occupiedRects.append(QRect(state.model.position, itemSize));
    }
    for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
        occupiedRects.append(QRect(it.value(), itemSize));
    }

    auto isFree = [&](const QPoint& candidatePos, const QVector<QRect>& rects) {
        const QRect candidateRect(candidatePos, itemSize);
        for (const QRect& occupiedRect : rects) {
            if (candidateRect.intersects(occupiedRect)) {
                return false;
            }
        }
        return true;
    };

    auto nearestFreePos = [&](const QPoint& requestedPos, const QVector<QRect>& rects) {
        const QPoint clampedPos(qBound(minX, requestedPos.x(), maxX),
                                qBound(minY, requestedPos.y(), maxY));
        if (isFree(clampedPos, rects)) {
            return clampedPos;
        }

        const int step = 16;
        const int maxRadius = qMax(maxX - minX, maxY - minY) + step;
        for (int radius = step; radius <= maxRadius; radius += step) {
            QVector<QPair<int, QPoint>> candidates;
            for (int dx = -radius; dx <= radius; dx += step) {
                for (int dy = -radius; dy <= radius; dy += step) {
                    if (qMax(qAbs(dx), qAbs(dy)) != radius) {
                        continue;
                    }
                    const QPoint candidate(qBound(minX, clampedPos.x() + dx, maxX),
                                           qBound(minY, clampedPos.y() + dy, maxY));
                    if (!isFree(candidate, rects)) {
                        continue;
                    }
                    candidates.append(qMakePair(dx * dx + dy * dy, candidate));
                }
            }
            if (!candidates.isEmpty()) {
                std::sort(candidates.begin(), candidates.end(), [](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                    if (a.first != b.first) {
                        return a.first < b.first;
                    }
                    if (a.second.y() != b.second.y()) {
                        return a.second.y() < b.second.y();
                    }
                    return a.second.x() < b.second.x();
                });
                return candidates.first().second;
            }
        }

        return QPoint(INT_MIN, INT_MIN);
    };

    for (const auto& pair : draggedStates) {
        const QPoint preferredPos = ghostLocalPositions.value(pair.first, pair.second->model.position);
        const QPoint resolvedPos = nearestFreePos(preferredPos, occupiedRects);
        if (resolvedPos.x() == INT_MIN || resolvedPos.y() == INT_MIN) {
            return QMap<QString, QPoint>();
        }
        result.insert(pair.first, resolvedPos);
        occupiedRects.append(QRect(resolvedPos, itemSize));
    }

    return result;
}

QSet<quint64> ListView::collectDesktopIconOccupiedCells(const QStringList& ignoredPaths) const
{
    QSet<QString> ignoredSet;
    for (const QString& path : ignoredPaths) {
        ignoredSet.insert(normalizeDesktopItemPath(path));
    }

    QSet<quint64> occupiedCells;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);

    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (ignoredSet.contains(normalizedPath)) {
            continue;
        }
        const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
        occupiedCells.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
        const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
        occupiedCells.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    return occupiedCells;
}

// 闂傚倸鐗婇悷锕傚焵椤戣法鍔嶉柛銊﹀哺瀹曘劌螣閸濆嫮浠存繝娈垮枛椤戝懐鈧?
ListView* ListView::m_instance = nullptr;
int ListItemData::s_targetIconSize = ListView::kMediumIconW;

ListView::ListView(QWidget* parent) : QWidget(parent),
    m_lastRightClickPos(-1, -1)  // 闂佸憡甯楃换鍌烇綖閹版澘绀岄柡宓啰顦﹂梻浣诡儥閸犳岸宕戦敐澶婄閻犱礁纾粔瀵哥磽閸愭儳鏋欓悹鎰枛瀵噣鎮╅搹顐ｇ彙闂?
{
    setAcceptDrops(true);
    // 闁荤姳绀佹晶浠嬫偪閸℃娴栫€光偓閸曨剚銆冮梺鐓庡暱閺堫剙鈻嶅▎鎾崇倞闁告挆鍐炬毈
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);


    // 闁荤姳绀佹晶浠嬫偪閸℃稑绫嶉柣妯挎閻濊埖淇婂Δ鈧Λ娑㈡偘閵夆晛鐭?
    // 闁荤姳绀佹晶浠嬫偪閸℃稒鐒婚煫鍥ㄦ⒐椤牠鏌ら崘鍙夋拱婵?
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    // 闁荤姳绀佹晶浠嬫偪閸℃稒鐒婚煫鍥ㄦ⒐椤牗顨ラ悙鑸电【闁告ɑ鍨归幏瀣箹娴ｆ彃浜鹃煫鍥ㄦ皑缁犳牜绱掗幇顓ф當鐟滅増鐩幆鍥┾偓锝庝簻閻撳倿鏌涘顒勫弰婵為棿鍗抽幆鍐礋椤愩垺鏆ラ柣?
    //setAttribute(Qt::WA_TransparentForMouseEvents); // 闂佺绻嬪ù鍥敊韫囨稒鍊烽柣鐔告緲濮ｅ﹪姊洪銏㈢缂佽鍟扮划锝呂旈埀顒冦亹?
    //setAttribute(Qt::WA_OpaquePaintEvent);  // 闂佸憡鍑归崹鎶藉极閵堝鍤勯悘鐐靛亾閻濐垶姊洪銏㈢婵″弶鍨垮畷?

    setGeometry(QApplication::primaryScreen()->geometry());
    //setAutoFillBackground(true);

    monitor = new DesktopInfoMonitor(this);
    startListeningSpecialIcon();
    m_windowsConfigMonitor = new WindowsConfigMonitor(this);
    connect(m_windowsConfigMonitor, &WindowsConfigMonitor::showHiddenFilesChanged,
        this, &ListView::onWindowsShowHiddenFilesChanged);
    m_windowsConfigMonitor->startListening();
    m_showHiddenFiles = m_windowsConfigMonitor->showHiddenFiles();

    // Initialize Plugin System & Hotkeys ASYNCHRONOUSLY to avoid recursion in constructor
    QTimer::singleShot(0, this, [this]() {
        PluginManager::instance()->loadPlugins();
        
        // Register a test hotkey for the Baidu OCR plugin
        // Alt + S = Sample OCR Trigger
        if (!PluginManager::instance()->allPlugins().isEmpty()) {
     // Register system-wide Hotkeys
    // Note: OCR and other plugins now register their own hotkeys in ocr_config.ini
    // and PluginManager handles the registration dynamically.
        }
    });

    //m_deskMonitor = new DirectoryMonitor(reinterpret_cast<HWND>(winId()),false, this);
    //m_deskMonitor->startListening();
    
    // 闂佸吋鍎抽崲鑼躲亹閸ヮ剚鍎戦柣鏃堫棑閺変粙鏌ｉ妸銉ヮ仾妞ゃ垺鍨垮Λ鍐閿濆洨鍞撮悗?
    QString desktopPath;
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        desktopPath = QString::fromWCharArray(path);
        CoTaskMemFree(path);
    } else {
        // 闂傚倸瀚粔鑸殿殽閸ヮ剙妫橀悷娆忓閺€鎶芥煥濞戞瑧鐓┑鐐叉喘閹粙鍩夐〃绠僡ndardPaths
        desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    m_desktopDir = desktopPath; // 婵烇絽娲︾换鍌炴偤閵娾晛绀嗛柣妤€鐗嗛悘鍥煕濞戞顏囥亹婢舵劖鐓?
    
    qDebug() << "闂佺儵鏅滈崹鍓佹暜鐠轰警娴栫€光偓閸曨剚銆冮柣鐘辫閸ㄦ壆娆?" << desktopPath;
    
    // 濠电偛顦崝宀勫船娴犲鍤婃い蹇撳閺嗘澘鈽夐弬娆炬Ш閻炴凹鍋婂畷鍦偓锝呭缁€澶愭煕韫囧孩绁版い鏂跨箳閹瑰嫰濡歌濞堢姷绱掔€ｎ亶鍎旀俊璇х畵瀹曟艾顪冮崣銉х畾闂佸憡鐟ㄥ〒鍦閸洘鐒?
    qRegisterMetaType<FileSystemEventInfo>("FileSystemEventInfo");
    
    // 婵炶揪缍€濞夋洟寮ˉ鐬歯闂佺儵鏅╅崰鏍礊瀹ュ鍎庨柟瀵稿仜娴犳﹢鏌?
    m_linMonitor = new LinDirectoryMonitor(desktopPath, false, this);
    connect(m_linMonitor, &LinDirectoryMonitor::fileSystemChanged, this, &ListView::onFileSystemChanged);
    m_linMonitor->startListening();

    rightMenu = RightMenu::getInstance();
    rightMenu->initializeCustomMenu();
    rightMenu->setHWND((HWND)this->winId());

    // Set persistence path first so we can load viewMode before creating items
    setFocusPolicy(Qt::StrongFocus);
    m_pendingShellNewTargetTimer = new QTimer(this);
    m_pendingShellNewTargetTimer->setSingleShot(true);
    m_pendingShellNewTargetTimer->setInterval(15000);
    connect(m_pendingShellNewTargetTimer, &QTimer::timeout, this, &ListView::clearPendingShellNewTargetBox);
    m_backgroundSelectionThrottleTimer = new QTimer(this);
    m_backgroundSelectionThrottleTimer->setSingleShot(true);
    connect(m_backgroundSelectionThrottleTimer, &QTimer::timeout, this, &ListView::flushBackgroundSelectionUpdate);
    m_snakeGameController = new DesktopSnakeGameController(this);
    m_persistencePath = AppPaths::desktopDataFilePath();
    
    // Load view mode configuration before initializing desktop item models.
    // This keeps icon sizing and render state consistent during startup.

    this->initialize();
    this->initRightMenuConnections();
    this->syncViewModeSizes(); // Call syncViewModeSizes after initialization
    
    // Initialize BoxManager
    BoxManager::instance(this);
    BoxManager::instance()->setPersistencePath(m_persistencePath);
    connect(BoxManager::instance(), &BoxManager::saveRequested, this, &ListView::saveBoxes);
    this->hide();
    // Defer loading boxes to ensure the event loop is running and windows can be properly shown/mapped
    QTimer::singleShot(100, [this](){
        this->loadBoxes(true);
        const QString configPath = AppPaths::configFilePath();
        QSettings settings(configPath, QSettings::IniFormat);
        const bool isFirstLaunch = settings.value("Firslanuch", true).toBool();
        if (isFirstLaunch) {
            this->setPersistenceEnabled(true); 
            settings.setValue("Firslanuch", false);
            settings.sync();
        }
        this->onMenuRefresh();
        this->raise();
        this->initializeLinWidgets();

    });
}

LinWidget::WidgetManager* ListView::linWidgetManager()
{
    if (!m_linWidgetManager) {
        m_linWidgetManager = new LinWidget::WidgetManager(this);
        m_linWidgetManager->setDesktopLayerWindow(winId());
        connect(m_linWidgetManager, &LinWidget::WidgetManager::widgetSettingsRequested,
                this, &ListView::openLinWidgetSettings);
    } else if (m_linWidgetManager->desktopLayerWindow() != winId()) {
        m_linWidgetManager->setDesktopLayerWindow(winId());
    }
    return m_linWidgetManager;
}

void ListView::openLinWidgetSettings(const QString& instanceId)
{
    LinWidget::WidgetManager* manager = linWidgetManager();
    if (!manager || instanceId.trimmed().isEmpty()) {
        return;
    }

    const LinWidget::WidgetInstance instance = manager->widgetInstance(instanceId.trimmed());
    if (instance.instanceId.isEmpty()) {
        qWarning() << "[LinWidgetSettings] instance not found" << instanceId;
        return;
    }

    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(instance.packagePath, &manifest, &errorMessage)) {
        qWarning() << "[LinWidgetSettings] manifest load failed" << instanceId << errorMessage;
        return;
    }

    const QString settingsPath = manifest.settings.trimmed();
    if (settingsPath.isEmpty() || !QFileInfo::exists(QDir(instance.packagePath).filePath(settingsPath))) {
        qWarning() << "[LinWidgetSettings] settings page missing" << instanceId << settingsPath;
        return;
    }

    auto* dialog = new LinWidget::WidgetSettingsDialog(
        instance.packagePath,
        manifest,
        instance.grantedPermissions,
        instance.isDevelopment,
        instance.instanceId,
        this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->initialize(&errorMessage);
    connect(dialog, &QDialog::finished, this, [this, instanceId]() {
        if (LinWidget::WidgetManager* manager = linWidgetManager()) {
            manager->restartWidget(instanceId);
        }
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    if (!errorMessage.isEmpty()) {
        qWarning() << "[LinWidgetSettings]" << errorMessage;
    }
}

void ListView::setShowLinWidgetBlockedCells(bool show)
{
    if (m_showLinWidgetBlockedCells == show) {
        return;
    }
    m_showLinWidgetBlockedCells = show;
    update();
}

void ListView::ensureBundledLinWidgetsInstalled()
{
    const QDir bundledDir(runtimeLinWidgetsDir());
    if (!bundledDir.exists()) {
        qDebug() << "[LinWidgetAutoLoad] runtime bundled widgets missing" << bundledDir.absolutePath();
        return;
    }

    const QString targetRoot = AppPaths::userLinWidgetsDir();
    const QString markerPath = QDir(targetRoot).filePath(QStringLiteral(".defaults-installed"));
    if (QFileInfo::exists(markerPath)) {
        return;
    }

    const QFileInfoList entries = bundledDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString targetPath = QDir(targetRoot).filePath(entry.fileName());
        if (QFileInfo::exists(targetPath)) {
            continue;
        }
        copyLinWidgetResourceDirectory(entry.absoluteFilePath(), targetPath);
    }

    QFile marker(markerPath);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write("1\n");
    }
}

void ListView::initializeLinWidgets()
{
    ensureBundledLinWidgetsInstalled();

    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    LinWidget::WidgetManager* manager = linWidgetManager();
    const bool linWidgetsEnabled = settings.value("LinWidgets/Enabled", true).toBool();
    manager->setEnabled(linWidgetsEnabled);
    manager->setShowWebViewBounds(settings.value("LinWidgets/ShowWebViewBounds", false).toBool());
    setShowLinWidgetBlockedCells(settings.value("LinWidgets/ShowBlockedCells", false).toBool());

    LinWidget::WidgetRegistryManager registry;
    QString registryError;
    registry.load(&registryError);
    registry.syncFromFolder(AppPaths::userLinWidgetsDir(), &registryError);
    registry.save(&registryError);

    if (!linWidgetsEnabled) {
        qDebug() << "[LinWidgetAutoLoad] runtime disabled";
        return;
    }

    QString restoreError;
    if (!manager->restoreFromConfig(QString(), &restoreError) && !restoreError.isEmpty()) {
        qDebug() << "[LinWidgetAutoLoad] restore failed" << restoreError;
    }
}

void ListView::syncLinWidgetLayer()
{
    if (!m_linWidgetManager) {
        return;
    }
    m_linWidgetManager->setDesktopLayerWindow(winId());
    m_linWidgetManager->syncAllWidgetWindowLayers();
}

void ListView::showDebugPanel()
{
    if (!m_debugPanel) {
        m_debugPanel = new ListViewDebugPanel(this);
        m_debugPanel->setAttribute(Qt::WA_DeleteOnClose, true);
    }

    m_debugPanel->show();
    m_debugPanel->raise();
    m_debugPanel->activateWindow();
}

void ListView::updateSystemIconVisibility(const QString& clsid, bool hidden)
{
    qDebug() << "updateSystemIconVisibility:" << clsid << "->" << (hidden ? "hiding" : "showing");
    
    // 闁荤喐鐟ョ€氼垳鈧灚鐗犲畷鐘诲嫉閸㈡〗ID闂佸搫绉堕崢褏妲愰敓鐘虫櫖闁割偀鎳ｉ埡鍛挃闁靛牆鎳庣拋鏌ユ煠鐎圭姵顥夐柣掳鍔戝畷鐑藉Ω瑜庨悾?:闂佸憡鎸哥粔鍓佹閹达附鏅?
    QString normalizedClsid = clsid;
    normalizedClsid.replace("::", "");

    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }

    DesktopItemInitContext context = currentDesktopItemInitContext();
    for (auto& state : m_itemStates) {
        QString itemClsid = state.model.itemData.getCLSID();
        itemClsid.replace("::", "");
        if (itemClsid != normalizedClsid) {
            continue;
        }

        DesktopRootItemModel updatedModel;
        initDesktopRootItemModel(updatedModel, state.model.itemData, context);
        updatedModel.layoutMode = state.model.layoutMode;
        updatedModel.position = state.model.position;
        updatedModel.baseSize = state.model.baseSize;
        updatedModel.effectiveSize = state.model.effectiveSize;
        updatedModel.visibleTextHeight = state.model.visibleTextHeight;
        updatedModel.isSelected = state.model.isSelected;
        updatedModel.isHovered = state.model.isHovered;
        updatedModel.focused = state.model.focused;
        updatedModel.isCutMarked = state.model.isCutMarked;
        updatedModel.isFullNameExpanded = state.model.isFullNameExpanded;
        updatedModel.customSortOrder = state.model.customSortOrder;
        updatedModel.expandedPreviewText = state.model.expandedPreviewText;
        updatedModel.isRenaming = state.model.isRenaming;
        updatedModel.isReady = true;
        if (hidden) {
            updatedModel.visible = false;
        }
        state.model = updatedModel;
        markDesktopRenderDataDirty(&state);
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    loadIconLayer();
    update();
    repaint();
    return;
}

int maxGridRowsForBounds(int layoutTop, int usableBottom, int itemHeight, int strideY)
{
    int rows = 0;
    int gridY = 0;
    while (layoutTop + gridY * strideY + itemHeight - 1 <= usableBottom) {
        ++rows;
        ++gridY;
    }
    return qMax(1, rows);
}

int maxGridColsForBounds(int layoutLeft, int usableRight, int itemWidth, int strideX)
{
    int cols = 0;
    int gridX = 0;
    while (layoutLeft + gridX * strideX + itemWidth - 1 <= usableRight) {
        ++cols;
        ++gridX;
    }
    return qMax(1, cols);
}

void ListView::onWindowsShowHiddenFilesChanged(bool showHiddenFiles)
{
    if (m_showHiddenFiles == showHiddenFiles) {
        return;
    }

    m_showHiddenFiles = showHiddenFiles;
    refreshVisibilityForWindowsConfig();
}

void ListView::startListeningSpecialIcon()
{
    // 閻庢鍠掗崑鎾斥攽椤旂⒈鍎戞繛鎻掑暣瀹?
    if (monitor->startListening()) {
        sysIconRegisterInfo = monitor->getCurrentIcons();

        /*for (const auto& icon : monitor->getCurrentIcons()) {
            qDebug() << icon.name << ":" << (icon.isHidden ? "hiddening" : "showing");
        }*/

        // 闁荤姳绀佹晶浠嬫偪閸℃稑鐐婇柣鎰摠閺嗩亞绱掗弮鈧悷鈺佲枔閿濆瀵?
        //monitor->setIconVisibility("{645FF040-5081-101B-9F08-00AA002F954E}", true);
    }
    // 闁哄鏅濋崑鐐垫暜鐎涙鈹嶉柍銉ュ暱婵?
    connect(monitor, &DesktopInfoMonitor::iconVisibilityChanged,
        this, &ListView::updateSystemIconVisibility);
}

bool ListView::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_HOTKEY) {
        handleHotkey(static_cast<int>(msg->wParam));
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (msg && (msg->message == WM_SHOWWINDOW ||
                msg->message == WM_WINDOWPOSCHANGED ||
                msg->message == WM_ACTIVATE ||
                msg->message == WM_ACTIVATEAPP)) {
        QTimer::singleShot(0, this, [this]() {
            syncLinWidgetLayer();
        });
    }

    return QWidget::nativeEvent(eventType, message, result);
}

void ListView::handleHotkey(int id) {
    QString actionId = HotkeyManager::instance()->getActionId(id);
    if (!actionId.isEmpty()) {
        qDebug() << "[ListView] Dispatching Hotkey ID:" << id << "to Action:" << actionId;
        PluginManager::instance()->triggerPlugin(actionId);
    }
}
ListView::~ListView() {
    saveBoxes();
    
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
    }

    if (m_infoTip) {
        delete m_infoTip;
        m_infoTip = nullptr;
    }
    
    // Clean up ghost widgets
    for (auto& ghost : m_ghosts) {
        if (ghost.widget) {
            ghost.widget->deleteLater();
        }
    }
    m_ghosts.clear();

}
DesktopRootItemState* ListView::findAnyItemStateByPath(const QString& filePath)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        return state;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return box->findItemStateByPath(filePath);
    }

    return nullptr;
}

const DesktopRootItemState* ListView::findAnyItemStateByPath(const QString& filePath) const
{
    if (const DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        return state;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return box->findItemStateByPath(filePath);
    }

    return nullptr;
}
void ListView::clearCheckedItem()
{
    clearDesktopSelection();
    m_focusedDesktopPath.clear();
    m_anchorDesktopPath.clear();
    m_focusedBoxPath.clear();
    m_anchorBoxPath.clear();
    m_selectedBoxPaths.clear();
    resetRenameClickState();
    
    // Also clear selection state for items inside boxes
    BoxManager* bm = BoxManager::instance();
    if (bm) {
        QSet<LinBox*> dirtyBoxes;
        for (const auto& box : bm->allBoxes()) {
            if (box) {
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    if (state.model.isSelected) {
                        clearItemExpandedState(state.model.filePath);
                        state.model.isSelected = false;
                        state.model.renderData.selected = false;
                        state.model.renderData.hovered = state.model.isHovered;
                        dirtyBoxes.insert(box);
                    }
                    if (state.model.focused) {
                        state.model.focused = false;
                        dirtyBoxes.insert(box);
                    }
                }
            }
        }
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
    }
}

void ListView::clearSelectionState()
{
    clearCheckedItem();
    clearFocusedItem();
    clearCutMarks();
    resetRenameClickState();
}

void ListView::setSelectedItemPaths(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    const QString normalizedFocusedPath = normalizeDesktopItemPath(focusedPath);
    QStringList desktopPaths;
    QStringList boxPaths;

    for (const QString& path : filePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (const DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            if (state->model.boxId.isEmpty()) {
                if (!desktopPaths.contains(normalizedPath)) {
                    desktopPaths.append(normalizedPath);
                }
                continue;
            }
        }

        if (findAnyItemStateByPath(normalizedPath) && !boxPaths.contains(normalizedPath)) {
            boxPaths.append(normalizedPath);
        }
    }

    if (!desktopPaths.isEmpty()) {
        BoxManager* bm = BoxManager::instance();
        if (bm) {
            QSet<LinBox*> dirtyBoxes;
            for (const QString& path : m_selectedBoxPaths) {
                clearItemExpandedState(path);
            }
            for (const auto& box : bm->allBoxes()) {
                if (!box) {
                    continue;
                }
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    if (state.model.isSelected || state.model.focused) {
                        state.model.isSelected = false;
                        state.model.renderData.selected = false;
                        state.model.renderData.hovered = state.model.isHovered;
                        state.model.focused = false;
                        dirtyBoxes.insert(box);
                    }
                }
            }
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
        }
        m_selectedBoxPaths.clear();
        m_focusedBoxPath.clear();
        m_anchorBoxPath.clear();
        clearFocusedItem();
        applyDesktopSelection(desktopPaths,
            normalizedFocusedPath.isEmpty() ? desktopPaths.last() : normalizedFocusedPath,
            expandSingle && desktopPaths.size() == 1);
        if (m_anchorDesktopPath.isEmpty()) {
            m_anchorDesktopPath = desktopPaths.first();
        }
        return;
    }

    if (!boxPaths.isEmpty()) {
        clearDesktopSelection();
        clearFocusedItem();
        m_focusedDesktopPath.clear();
        m_anchorDesktopPath.clear();
        for (const QString& path : m_selectedBoxPaths) {
            clearItemExpandedState(path);
        }

        BoxManager* bm = BoxManager::instance();
        QSet<LinBox*> dirtyBoxes;
        const QString resolvedFocusedPath = normalizedFocusedPath.isEmpty() ? boxPaths.last() : normalizedFocusedPath;
        if (bm) {
            for (const auto& box : bm->allBoxes()) {
                if (!box) {
                    continue;
                }
                bool boxDirty = false;
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    const QString statePath = normalizeDesktopItemPath(state.model.filePath);
                    const bool selected = boxPaths.contains(statePath);
                    const bool focused = statePath == resolvedFocusedPath;
                    if (state.model.isSelected != selected) {
                        state.model.isSelected = selected;
                        state.model.renderData.selected = selected;
                        state.model.renderData.hovered = state.model.isHovered && !selected;
                        boxDirty = true;
                    }
                    if (state.model.focused != focused) {
                        state.model.focused = focused;
                        boxDirty = true;
                    }
                }
                if (boxDirty) {
                    dirtyBoxes.insert(box);
                }
            }
        }

        for (const QString& path : boxPaths) {
            setItemFullNameExpanded(path, false);
        }
        if (expandSingle && boxPaths.size() == 1) {
            setItemFullNameExpanded(boxPaths.first(), true);
        }

        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }

        m_selectedBoxPaths = boxPaths.toVector();
        m_focusedBoxPath = resolvedFocusedPath;
        if (m_anchorBoxPath.isEmpty()) {
            m_anchorBoxPath = boxPaths.first();
        }
        return;
    }

    clearSelectionState();
}

void ListView::setSelectedItems(const QStringList& filePaths, bool expandSingle)
{
    const QString focusedPath = filePaths.isEmpty() ? QString() : normalizeDesktopItemPath(filePaths.last());
    setSelectedItemPaths(filePaths, focusedPath, expandSingle);
}

void ListView::applySelectionState(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    setSelectedItemPaths(filePaths, focusedPath, expandSingle);
}

void ListView::setItemFullNameExpanded(const QString& filePath, bool expanded)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        if (state->model.isFullNameExpanded == expanded &&
            (expanded || state->model.expandedPreviewText.isEmpty())) {
            return;
        }

        const QRect oldDirty = desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect);
        state->model.isFullNameExpanded = expanded;
        if (!expanded) {
            state->model.expandedPreviewText.clear();
        }
        markDesktopRenderDataDirty(state);
        updateAllItemRenderData();
        update(oldDirty.united(
            desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect)));
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            if (state->model.isFullNameExpanded == expanded &&
                (expanded || state->model.expandedPreviewText.isEmpty())) {
                return;
            }
            state->model.isFullNameExpanded = expanded;
            if (!expanded) {
                state->model.expandedPreviewText.clear();
            }
            state->renderDataDirty = true;
            box->refreshLayout();
            box->update();
            return;
        }
    }
}

void ListView::setItemExpandedPreviewText(const QString& filePath, const QString& text)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        if (state->model.expandedPreviewText == text) {
            return;
        }

        const QRect oldDirty = desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect);
        state->model.expandedPreviewText = text;
        markDesktopRenderDataDirty(state);
        updateAllItemRenderData();
        update(oldDirty.united(
            desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect)));
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            if (state->model.expandedPreviewText == text) {
                return;
            }
            state->model.expandedPreviewText = text;
            state->renderDataDirty = true;
            box->refreshLayout();
            box->update();
            return;
        }
    }
}

void ListView::clearItemExpandedState(const QString& filePath)
{
    setItemExpandedPreviewText(filePath, QString());
    setItemFullNameExpanded(filePath, false);
}

void ListView::setItemRenaming(const QString& filePath, bool renaming)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        if (state->model.isRenaming == renaming) {
            return;
        }

        const QRect oldDirty = desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect);
        state->model.isRenaming = renaming;
        markDesktopRenderDataDirty(state);
        updateAllItemRenderData();
        update(oldDirty.united(
            desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect)));
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            if (state->model.isRenaming == renaming) {
                return;
            }
            state->model.isRenaming = renaming;
            state->renderDataDirty = true;
            box->refreshLayout();
            box->update();
            return;
        }
    }
}

void ListView::setFocusedItemPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    BoxManager* bm = BoxManager::instance();
    QSet<LinBox*> dirtyBoxes;
    auto updateBoxFocusState = [&](const QString& targetPath) {
        if (!bm) {
            return;
        }
        for (const auto& box : bm->allBoxes()) {
            if (!box) {
                continue;
            }
            bool boxDirty = false;
            for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                const bool focused = !targetPath.isEmpty()
                    && normalizeDesktopItemPath(state.model.filePath) == targetPath;
                if (state.model.focused != focused) {
                    state.model.focused = focused;
                    boxDirty = true;
                }
            }
            if (boxDirty) {
                dirtyBoxes.insert(box);
            }
        }
    };

    if (normalizedPath.isEmpty()) {
        clearFocusedItem();
        m_focusedDesktopPath.clear();
        m_focusedBoxPath.clear();
        updateBoxFocusState(QString());
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
        return;
    }

    if (const DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
        if (state->model.boxId.isEmpty()) {
            clearFocusedItem();
            m_focusedDesktopPath = normalizedPath;
            m_focusedBoxPath.clear();
            updateBoxFocusState(QString());
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
            return;
        }
    }

    if (findAnyItemStateByPath(normalizedPath)) {
        clearFocusedItem();
        m_focusedDesktopPath.clear();
        m_focusedBoxPath = normalizedPath;
        updateBoxFocusState(normalizedPath);
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
        return;
    }

    clearFocusedItem();
    m_focusedBoxPath.clear();
    updateBoxFocusState(QString());
    for (LinBox* dirtyBox : dirtyBoxes) {
        if (!dirtyBox) {
            continue;
        }
        dirtyBox->refreshLayout();
        dirtyBox->update();
    }
}

void ListView::clearFocusedItem()
{
    if (!m_focusedDesktopPath.isEmpty()) {
        clearItemExpandedState(m_focusedDesktopPath);
    }
    if (!m_focusedBoxPath.isEmpty()) {
        clearItemExpandedState(m_focusedBoxPath);
    }
    m_focusedDesktopPath.clear();
    m_focusedBoxPath.clear();
}

void ListView::toggleItemSelection(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    if (const DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
        if (state->model.boxId.isEmpty()) {
            QStringList updatedPaths = selectedDesktopPaths();
            if (updatedPaths.contains(normalizedPath)) {
                updatedPaths.removeAll(normalizedPath);
                clearItemExpandedState(normalizedPath);
            } else {
                updatedPaths.append(normalizedPath);
                setItemFullNameExpanded(normalizedPath, false);
            }
            applyDesktopSelection(updatedPaths, normalizedPath, false);
            if (m_anchorDesktopPath.isEmpty() && !updatedPaths.isEmpty()) {
                m_anchorDesktopPath = normalizedPath;
            }
            return;
        }
    }

    if (findAnyItemStateByPath(normalizedPath)) {
        QStringList updatedPaths = selectedBoxPaths();
        if (m_selectedBoxPaths.contains(normalizedPath)) {
            updatedPaths.removeAll(normalizedPath);
            clearItemExpandedState(normalizedPath);
        } else {
            updatedPaths.append(normalizedPath);
            setItemFullNameExpanded(normalizedPath, false);
        }
        setSelectedItemPaths(updatedPaths, normalizedPath, false);
        if (m_anchorBoxPath.isEmpty() && !updatedPaths.isEmpty()) {
            m_anchorBoxPath = normalizedPath;
        }
    }
}

void ListView::selectRangeToPath(const QString& filePath, LinBox* box)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    if (!box) {
        const QStringList orderedPaths = selectableDesktopPaths();
        if (orderedPaths.isEmpty()) {
            return;
        }

        const QString anchorPath = m_anchorDesktopPath.isEmpty() ? normalizedPath : m_anchorDesktopPath;
        const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
        const int itemIndex = orderedPaths.indexOf(normalizedPath);
        if (anchorIndex < 0 || itemIndex < 0) {
            applyDesktopSelection({ normalizedPath }, normalizedPath, true);
            m_anchorDesktopPath = normalizedPath;
            return;
        }

        QStringList rangePaths;
        const int start = qMin(anchorIndex, itemIndex);
        const int end = qMax(anchorIndex, itemIndex);
        for (int i = start; i <= end; ++i) {
            rangePaths.append(orderedPaths.at(i));
        }
        applyDesktopSelection(rangePaths, normalizedPath, false);
        return;
    }

    const QStringList orderedPaths = selectableBoxPaths(box);
    if (orderedPaths.isEmpty()) {
        return;
    }

    const QString anchorPath = m_anchorBoxPath.isEmpty() ? normalizedPath : m_anchorBoxPath;
    const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
    const int itemIndex = orderedPaths.indexOf(normalizedPath);
    if (anchorIndex < 0 || itemIndex < 0) {
        setSelectedItemPaths({ normalizedPath }, normalizedPath, true);
        m_anchorBoxPath = normalizedPath;
        return;
    }

    QStringList rangePaths;
    const int start = qMin(anchorIndex, itemIndex);
    const int end = qMax(anchorIndex, itemIndex);
    for (int i = start; i <= end; ++i) {
        rangePaths.append(orderedPaths.at(i));
    }
    setSelectedItemPaths(rangePaths, normalizedPath, false);
}

void ListView::moveFocusInHost(LinBox* box, FocusMoveDirection direction, bool extendSelection, bool focusOnly)
{
    if (!box) {
        const QStringList orderedPaths = selectableDesktopPaths();
        if (orderedPaths.isEmpty()) {
            return;
        }

        const QString currentPath = effectiveFocusedDesktopPath();
        QString targetPath = findDirectionalDesktopFocusCandidate(currentPath, direction);
        if (targetPath.isEmpty() && !currentPath.isEmpty()) {
            const int currentIndex = orderedPaths.indexOf(currentPath);
            if (currentIndex >= 0) {
                const int delta = (direction == FocusMoveDirection::Left || direction == FocusMoveDirection::Up) ? -1 : 1;
                const int fallbackIndex = qBound(0, currentIndex + delta, orderedPaths.size() - 1);
                targetPath = orderedPaths.at(fallbackIndex);
            }
        }

        if (targetPath.isEmpty()) {
            return;
        }

        if (focusOnly) {
            m_focusedDesktopPath = targetPath;
            return;
        }

        if (extendSelection) {
            QString anchorPath = m_anchorDesktopPath.isEmpty() ? targetPath : m_anchorDesktopPath;
            const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
            const int itemIndex = orderedPaths.indexOf(targetPath);
            if (anchorIndex >= 0 && itemIndex >= 0) {
                QStringList rangePaths;
                const int start = qMin(anchorIndex, itemIndex);
                const int end = qMax(anchorIndex, itemIndex);
                for (int i = start; i <= end; ++i) {
                    rangePaths.append(orderedPaths.at(i));
                }
                applyDesktopSelection(rangePaths, targetPath, false);
            } else {
                applyDesktopSelection({ targetPath }, targetPath, true);
            }
            if (m_anchorDesktopPath.isEmpty()) {
                m_anchorDesktopPath = anchorPath;
            }
            return;
        }

        applyDesktopSelection({ targetPath }, targetPath, true);
        m_anchorDesktopPath = targetPath;
        return;
    }

    const QStringList orderedPaths = selectableBoxPaths(box);
    if (orderedPaths.isEmpty()) {
        return;
    }

    const QString currentPath = effectiveFocusedBoxPath();
    QString targetPath = findDirectionalBoxFocusCandidate(box, currentPath, direction);

    if (targetPath.isEmpty() && !currentPath.isEmpty()) {
        const int currentIndex = orderedPaths.indexOf(currentPath);
        if (currentIndex >= 0) {
            const int delta = (direction == FocusMoveDirection::Left || direction == FocusMoveDirection::Up) ? -1 : 1;
            const int fallbackIndex = qBound(0, currentIndex + delta, orderedPaths.size() - 1);
            targetPath = orderedPaths.at(fallbackIndex);
        }
    }

    if (targetPath.isEmpty()) {
        return;
    }

    if (focusOnly) {
        setFocusedItemPath(targetPath);
        return;
    }

    if (extendSelection) {
        const QString anchorPath = m_anchorBoxPath.isEmpty() ? targetPath : m_anchorBoxPath;
        const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
        const int itemIndex = orderedPaths.indexOf(targetPath);
        if (anchorIndex >= 0 && itemIndex >= 0) {
            QStringList rangePaths;
            const int start = qMin(anchorIndex, itemIndex);
            const int end = qMax(anchorIndex, itemIndex);
            for (int i = start; i <= end; ++i) {
                rangePaths.append(orderedPaths.at(i));
            }
            setSelectedItemPaths(rangePaths, targetPath, false);
        } else {
            setSelectedItemPaths({ targetPath }, targetPath, true);
        }
        if (m_anchorBoxPath.isEmpty()) {
            m_anchorBoxPath = anchorPath;
        }
        return;
    }

    setSelectedItemPaths({ targetPath }, targetPath, true);
    m_anchorBoxPath = targetPath;
}

void ListView::clearCutMarks()
{
    for (auto& state : m_itemStates) {
        state.model.isCutMarked = false;
        state.model.renderData.cutMarked = false;
    }

    BoxManager* bm = BoxManager::instance();
    if (bm) {
        QSet<LinBox*> dirtyBoxes;
        for (const auto& box : bm->allBoxes()) {
            if (!box) {
                continue;
            }
            bool boxDirty = false;
            for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                if (!state.model.isCutMarked && !state.model.renderData.cutMarked) {
                    continue;
                }
                state.model.isCutMarked = false;
                state.model.renderData.cutMarked = false;
                boxDirty = true;
            }
            if (boxDirty) {
                dirtyBoxes.insert(box);
            }
        }
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
}

void ListView::applyCutMarks(const QStringList& filePaths)
{
    clearCutMarks();
    for (const QString& path : filePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            state->model.isCutMarked = true;
            state->model.renderData.cutMarked = true;
            continue;
        }
        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (DesktopRootItemState* state = box->findItemStateByPath(normalizedPath)) {
                state->model.isCutMarked = true;
                state->model.renderData.cutMarked = true;
                box->refreshLayout();
                box->update();
            }
        }
    }
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
}

QStringList ListView::selectedItemPaths() const
{
    QStringList paths;
    paths.append(selectedDesktopPaths());
    for (const QString& path : m_selectedBoxPaths) {
        if (!path.isEmpty() && !paths.contains(path)) {
            paths.append(path);
        }
    }
    return paths;
}

QString ListView::focusedItemPath() const
{
    if (!m_focusedDesktopPath.isEmpty()) {
        return effectiveFocusedDesktopPath();
    }
    if (!m_focusedBoxPath.isEmpty()) {
        return effectiveFocusedBoxPath();
    }
    return QString();
}

QString ListView::currentSpaceLookHoveredPath() const
{
    const QPoint cursorPos = QCursor::pos();
    const QVector<LinBox*> allBoxes = boxes();
    for (LinBox* box : allBoxes) {
        if (!box || !box->isVisible()) {
            continue;
        }

        const QRect globalRect(box->mapToGlobal(QPoint(0, 0)), box->size());
        if (!globalRect.contains(cursorPos)) {
            continue;
        }

        const QString hoveredPath = box->hoveredItemPath();
        if (!hoveredPath.trimmed().isEmpty()) {
            return normalizeDesktopItemPath(hoveredPath);
        }
    }

    if (!m_hoveredDesktopPath.trimmed().isEmpty()) {
        return effectiveFocusedDesktopPath().isEmpty()
            ? normalizeDesktopItemPath(m_hoveredDesktopPath)
            : normalizeDesktopItemPath(m_hoveredDesktopPath);
    }

    return QString();
}

QString ListView::anchorItemPath() const
{
    if (!m_anchorDesktopPath.isEmpty()) {
        return effectiveAnchorDesktopPath();
    }
    if (!m_anchorBoxPath.isEmpty()) {
        return effectiveAnchorBoxPath();
    }
    return QString();
}

QString ListView::draggedItemPath() const
{
    if (!m_draggedDesktopPath.isEmpty()) {
        return normalizeDesktopItemPath(m_draggedDesktopPath);
    }
    if (!m_draggedBoxPath.isEmpty()) {
        return normalizeDesktopItemPath(m_draggedBoxPath);
    }
    return QString();
}

void ListView::setDraggedItemPath(const QString& filePath, bool fromBox)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (fromBox) {
        m_draggedBoxPath = normalizedPath;
        if (m_draggedDesktopPath == normalizedPath) {
            m_draggedDesktopPath.clear();
        }
        return;
    }

    m_draggedDesktopPath = normalizedPath;
    if (m_draggedBoxPath == normalizedPath) {
        m_draggedBoxPath.clear();
    }
}

void ListView::clearDraggedItemPath(bool fromBoxOnly)
{
    if (fromBoxOnly) {
        m_draggedBoxPath.clear();
        return;
    }

    m_draggedDesktopPath.clear();
    m_draggedBoxPath.clear();
}

void ListView::restoreSelectionStateByPaths(const QStringList& selectedPaths, const QString& focusedPath, const QString& anchorPath, bool expandSingle)
{
    setSelectedItemPaths(selectedPaths, focusedPath, expandSingle);
    m_anchorDesktopPath = anchorPath.isEmpty() ? QString() : normalizeDesktopItemPath(anchorPath);
    m_anchorBoxPath = anchorPath.isEmpty() ? QString() : normalizeDesktopItemPath(anchorPath);
}

void ListView::writeSelectionToClipboard(bool cutOperation)
{
    if (m_selectedBoxPaths.isEmpty() && m_selectedDesktopPaths.isEmpty()) {
        return;
    }

    QList<QUrl> urls;
    for (const QString& path : m_selectedDesktopPaths) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (!state) {
            continue;
        }
        if (state->model.objectType == ObjectType::SpecialFolder) {
            continue;
        }
        const QString filePath = state->model.filePath;
        if (filePath.isEmpty() || filePath.startsWith("::")) {
            continue;
        }
        urls.append(QUrl::fromLocalFile(filePath));
    }
    for (const QString& path : selectedBoxPaths()) {
        const DesktopRootItemState* state = findAnyItemStateByPath(path);
        if (!state) {
            continue;
        }
        if (state->model.objectType == ObjectType::SpecialFolder) {
            continue;
        }
        const QString filePath = state->model.filePath;
        if (filePath.isEmpty() || filePath.startsWith("::")) {
            continue;
        }
        urls.append(QUrl::fromLocalFile(filePath));
    }

    if (urls.isEmpty()) {
        return;
    }

    ListViewFileOpsHelper::writeUrlsToClipboard(urls, cutOperation);
    if (cutOperation) {
        for (const QString& path : m_selectedDesktopPaths) {
            if (DesktopRootItemState* state = findDesktopStateByPath(path)) {
                state->model.isCutMarked = true;
                state->model.renderData.cutMarked = true;
            }
        }

        BoxManager* bm = BoxManager::instance();
        if (bm) {
            QSet<LinBox*> dirtyBoxes;
            for (const QString& path : selectedBoxPaths()) {
                if (LinBox* box = bm->findBoxContainingItem(path)) {
                    if (DesktopRootItemState* state = box->findItemStateByPath(path)) {
                        state->model.isCutMarked = true;
                        state->model.renderData.cutMarked = true;
                        dirtyBoxes.insert(box);
                    }
                }
            }
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
        }
    } else {
        clearCutMarks();
    }
}

void ListView::armPendingShellNewTargetBox(LinBox* box)
{
    m_pendingShellNewTargetBox = box;
    if (m_pendingShellNewTargetTimer) {
        m_pendingShellNewTargetTimer->start();
    }
}

void ListView::clearPendingShellNewTargetBox()
{
    m_pendingShellNewTargetBox.clear();
    if (m_pendingShellNewTargetTimer) {
        m_pendingShellNewTargetTimer->stop();
    }
}

void ListView::requestSystemRightMenu(ObjectType objType, QPoint point, const QStringList& checkedPaths)
{
    RightMenu* r = RightMenu::getInstance();
    QVector<DesktopRootItemModel> models;
    models.reserve(qMax<int>(checkedPaths.size(), selectedItemPaths().size()));
    for (const QString& path : checkedPaths) {
        if (const DesktopRootItemState* state = findAnyItemStateByPath(path)) {
            models.push_back(state->model);
        }
    }

    if (models.isEmpty() && objType != ObjectType::BackGround) {
        for (const QString& path : selectedItemPaths()) {
            if (const DesktopRootItemState* state = findAnyItemStateByPath(path)) {
                bool exists = false;
                for (const DesktopRootItemModel& model : models) {
                    if (normalizeDesktopItemPath(model.filePath) == normalizeDesktopItemPath(state->model.filePath)) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    models.push_back(state->model);
                }
            }
        }
    }

    if (checkedPaths.size() == 1 || models.size() == 1) {
        r->setShowType(objType);
        if (!models.isEmpty()) {
            const bool extendedVerbs = (QApplication::queryKeyboardModifiers() & Qt::ShiftModifier) != 0;
            r->showMenu(point, models.first(), extendedVerbs);
        }
        return;
    }
    else if (objType == ObjectType::BackGround) {
        r->setShowType(ObjectType::BackGround);
        const bool extendedVerbs = (QApplication::queryKeyboardModifiers() & Qt::ShiftModifier) != 0;
        r->showMenu(point, DesktopRootItemModel{}, extendedVerbs);
        return;
    }
    else {
        r->setShowType(ObjectType::MultObject);
        if (!models.isEmpty()) {
            const bool extendedVerbs = (QApplication::queryKeyboardModifiers() & Qt::ShiftModifier) != 0;
            r->showMultObjectMenu(point, models, extendedVerbs);
        }
    }
}

void ListView::refreshCheckedItem(QRect rect)
{
    QRegion uncoveredSelectionRegion(rect);
    if (!m_backgroundSelectionOcclusionRegion.isEmpty()) {
        uncoveredSelectionRegion = uncoveredSelectionRegion.subtracted(m_backgroundSelectionOcclusionRegion);
    }

    if (uncoveredSelectionRegion.isEmpty()) {
        resetBackgroundSelectionCandidateCache();
        clearCheckedItem();
        return;
    }

    updateBackgroundSelectionCandidateCache(uncoveredSelectionRegion);

    m_backgroundSelectionNextSelectionSet.clear();
    m_backgroundSelectionNextSelectionStates.clear();
    QString fallbackFocusPath;

    m_backgroundSelectionNextSelectionSet.reserve(m_backgroundSelectionCandidateStates.size());
    m_backgroundSelectionNextSelectionStates.reserve(m_backgroundSelectionCandidateStates.size());
    for (DesktopRootItemState* state : m_backgroundSelectionCandidateStates) {
        if (!state || !shouldPaintDesktopModelState(*state)) {
            continue;
        }

        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (!uncoveredSelectionRegion.intersects(state->model.renderData.selectionContentRect)) {
            continue;
        }

        if (!m_backgroundSelectionNextSelectionSet.contains(normalizedPath)) {
            m_backgroundSelectionNextSelectionSet.insert(normalizedPath);
            m_backgroundSelectionNextSelectionStates.append(state);
            fallbackFocusPath = normalizedPath;
        }
    }

    if (m_selectedDesktopPathSet == m_backgroundSelectionNextSelectionSet) {
        return;
    }

    QRegion dirtyRegion;
    m_backgroundSelectionPreviousSelectionPaths = m_selectedDesktopPaths;
    for (const QString& path : m_backgroundSelectionPreviousSelectionPaths) {
        if (m_backgroundSelectionNextSelectionSet.contains(path)) {
            continue;
        }
        clearItemExpandedState(path);
        if (DesktopRootItemState* state = findDesktopStateByPath(path)) {
            setDesktopItemSelected(state, path, false, &dirtyRegion);
        }
    }

    for (DesktopRootItemState* state : m_backgroundSelectionNextSelectionStates) {
        if (!state) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty() || m_selectedDesktopPathSet.contains(normalizedPath)) {
            continue;
        }
        setDesktopItemSelected(state, normalizedPath, true, &dirtyRegion);
    }

    if (!dirtyRegion.isEmpty()) {
        update(dirtyRegion);
    }

    m_focusedDesktopPath = m_backgroundSelectionNextSelectionSet.isEmpty()
        ? QString()
        : (m_backgroundSelectionNextSelectionSet.contains(m_focusedDesktopPath) ? m_focusedDesktopPath : fallbackFocusPath);

    if (!m_backgroundSelectionNextSelectionSet.isEmpty() && m_anchorDesktopPath.isEmpty()) {
        m_anchorDesktopPath = fallbackFocusPath;
    }
}

void ListView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QRegion dirtyRegion = event->region();
    const bool reducePaintCostForSelection = m_isSelecting;
    painter.setRenderHint(QPainter::Antialiasing, !reducePaintCostForSelection);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !reducePaintCostForSelection);
    painter.setRenderHint(QPainter::TextAntialiasing);

    if (m_hideDesktopItemsForRefresh) {
        return;
    }

    auto paintDesktopState = [&](const DesktopRootItemState& state) {
        if (!shouldPaintDesktopModelState(state)) {
            return;
        }

        if (!dirtyRegion.intersects(state.model.renderData.itemRect)) {
            return;
        }

        paintDesktopRootItemDynamic(painter, state.model.renderData);
        paintDesktopRootItemStatic(painter, state.model.renderData);
    };

    // Paint normal items first so expanded filename labels can sit above siblings.
    for (const auto& state : m_itemStates) {
        if (state.model.renderData.expandedFullName) {
            continue;
        }
        paintDesktopState(state);
    }

    for (const auto& state : m_itemStates) {
        if (!state.model.renderData.expandedFullName) {
            continue;
        }
        paintDesktopState(state);
    }

    const QRect indicatorRect = desktopAutoArrangeInsertionIndicatorRect();
    if (!indicatorRect.isNull() && dirtyRegion.intersects(indicatorRect.adjusted(-4, -3, 4, 3))) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 220, 220, 80));
        painter.drawRoundedRect(indicatorRect.adjusted(-4, -3, 4, 3), 5, 5);
        painter.setBrush(QColor(0, 220, 220, 230));
        painter.drawRoundedRect(indicatorRect, 2, 2);
        painter.restore();
    }

    paintLinWidgetBlockedCellsOverlay(painter, dirtyRegion);
}

void ListView::contextMenuEvent(QContextMenuEvent* event)
{
#ifdef Q_OS_WIN
    logListViewDpiTrace("contextMenu", this, event->pos(), event->globalPos());
#endif
    if (event->reason() == QContextMenuEvent::Mouse && this->rect().contains(event->pos())) {
        if (DesktopRootItemState* hitState = findDesktopStateAt(event->pos())) {
            const QString itemPath = normalizeDesktopItemPath(hitState->model.filePath);
            if (!isDesktopPathSelected(itemPath)) {
                applyDesktopSelection({ itemPath }, itemPath, true);
                m_anchorDesktopPath = itemPath;
            } else {
                m_focusedDesktopPath = itemPath;
            }

            QVector<DesktopRootItemModel> desktopModels = selectedDesktopModels();
            RightMenu* menu = RightMenu::getInstance();
            const bool extendedVerbs = (event->modifiers() & Qt::ShiftModifier) != 0;
            if (desktopModels.size() == 1) {
                menu->setShowType(hitState->model.objectType);
                menu->showMenu(event->globalPos(), desktopModels.first(), extendedVerbs);
            } else if (!desktopModels.isEmpty()) {
                menu->setShowType(ObjectType::MultObject);
                menu->showMultObjectMenu(event->globalPos(), desktopModels, extendedVerbs);
            }
            event->accept();
            return;
        }

        auto rightClickPoint = mapToGlobal(event->pos());
        this->m_lastRightClickPos = event->pos();

        // Since ListItems now intercept their own contextMenuEvent and emit signals,
        // this handler is only reached if the background (desktop) is clicked.
        
        // 婵犵鈧啿鈧綊鎮樻径鎰煑闁规儼濮ら弳娑㈡煟閹邦喗鍤€闁搞値鍠楅幏鍛吋閸モ晜鐎繛鎴炴尭缁夋潙锕㈤鍛浄閻犱礁纾粔鍧楁煕閵壯冧壕闁绘牭缍佸畷鐘诲传閸曨厼骞嶉梺鍛婂姇閹锋垹妲愬┑鍡愨偓鎺楀川鐎涙褰戦悗鍦焾瀵泛锕㈡笟鈧弻鍛緞婢跺骸骞€
        clearCheckedItem();
        // 闂佸搫瀚晶浠嬪Φ濮橆剦娴栫€光偓閸曨剚銆冮梺姹囧妼鐎氼剝銇愰幖浣圭叆妞ゆ柨鐏濈紞鍛存煕?
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint);
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
#ifdef Q_OS_WIN
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        logListViewDpiTrace("mousePress", this, event->pos(), event->globalPos());
    }
#endif
    RenameManager::instance()->cancelRename();
    if (event->button() == Qt::LeftButton) {
        if (DesktopRootItemState* hitState = findDesktopStateAt(event->pos())) {
            this->activateWindow();
            this->setFocus(Qt::MouseFocusReason);

            const QString itemPath = normalizeDesktopItemPath(hitState->model.filePath);
            const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();

            if (modifiers & Qt::ControlModifier) {
                const bool selected = isDesktopPathSelected(itemPath);
                setDesktopItemSelected(itemPath, !selected);
                m_focusedDesktopPath = itemPath;
                if (m_anchorDesktopPath.isEmpty()) {
                    m_anchorDesktopPath = itemPath;
                }
                m_wasSelectedBeforeClick = false;
            } else if (modifiers & Qt::ShiftModifier) {
                QStringList orderedPaths;
                for (const auto& state : m_itemStates) {
                    if (shouldPaintDesktopModelState(state)) {
                        orderedPaths.append(normalizeDesktopItemPath(state.model.filePath));
                    }
                }

                const QString anchorPath = m_anchorDesktopPath.isEmpty() ? itemPath : m_anchorDesktopPath;
                const int anchorIndex = orderedPaths.indexOf(anchorPath);
                const int itemIndex = orderedPaths.indexOf(itemPath);
                if (anchorIndex >= 0 && itemIndex >= 0) {
                    QStringList rangePaths;
                    const int start = qMin(anchorIndex, itemIndex);
                    const int end = qMax(anchorIndex, itemIndex);
                    for (int i = start; i <= end; ++i) {
                        rangePaths.append(orderedPaths.at(i));
                    }
                    applyDesktopSelection(rangePaths, itemPath, false);
                } else {
                    applyDesktopSelection({ itemPath }, itemPath, true);
                    m_anchorDesktopPath = itemPath;
                }
                m_wasSelectedBeforeClick = false;
            } else {
                if (!isDesktopPathSelected(itemPath)) {
                    applyDesktopSelection({ itemPath }, itemPath, true);
                    m_anchorDesktopPath = itemPath;
                    m_wasSelectedBeforeClick = false;
                } else {
                    m_focusedDesktopPath = itemPath;
                    setItemFullNameExpanded(itemPath, true);
                    m_wasSelectedBeforeClick = (m_selectedDesktopPaths.size() == 1);
                }
            }

            m_lastClickedDesktopPath = itemPath;
            m_clickTimer.restart();
            if (!(modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
                m_anchorDesktopPath = itemPath;
            }

            m_draggedDesktopPath = itemPath;
            m_dragOffset = event->pos() - hitState->model.renderData.itemRect.topLeft();
            m_isDragging = true;
            m_startPos = event->pos();
            event->accept();
            return;
        }

        clearHoveredDesktopItem();
        beginBackgroundSelection(event->pos());
        event->accept();
        return;
    }

    event->ignore();
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    // handle multi-dragging
    if (m_isDragging && !m_ghosts.isEmpty()) {
        QPoint mousePos = event->globalPos();
        moveDesktopGhostsForPreview(mousePos);
        updateGhostDragHoverPreview(mousePos);
        return;
    }

    if (m_isDragging && !m_draggedDesktopPath.isEmpty()) {
        if ((event->pos() - m_startPos).manhattanLength() > QApplication::startDragDistance()) {
            startDesktopItemDrag(m_startPos);
        }
        event->accept();
        return;
    }

    if (m_isSelecting) {
        QRect rect;
        const bool shouldShowSelection = updateBackgroundSelection(event->pos(), &rect);
        if (LinDesktop* desktop = qobject_cast<LinDesktop*>(parentWidget())) {
            if (desktop->selectArea) {
                desktop->selectArea->setSelectionRect(rect);
            }
            if (shouldShowSelection) {
                if (desktop->selectArea && !desktop->selectArea->isVisible()) {
                    desktop->selectArea->show();
                }
                if (desktop->selectArea) {
                    desktop->selectArea->lower();
                }
            } else {
                desktop->clearSelectionOverlay();
            }
        }
        event->accept();
        return;
    }

    updateHoveredDesktopItem(event->pos());


    event->ignore();

}

void ListView::mouseReleaseEvent(QMouseEvent* event)
{
    // No-op for Item-based drags, as QDropEvent now handles them
    if (m_isDragging) {
        if (!m_draggedDesktopPath.isEmpty()) {
            finalizeActiveDrag(nullptr, event);
            m_draggedDesktopPath.clear();
            event->accept();
            return;
        }
        m_isDragging = false;
    }
    clearDesktopAutoArrangeReorderPreview();
    
    // Clear selection state if we were selecting background
    if (m_isSelecting) {
        if (LinDesktop* desktop = qobject_cast<LinDesktop*>(parentWidget())) {
            desktop->clearSelectionOverlay();
        }
        endBackgroundSelection();
        event->accept();
        return;
    }
    event->ignore();
}

void ListView::beginBackgroundSelection(const QPoint& startPos)
{
    clearCheckedItem();
    setFocus();
    if (mouseGrabber() != this) {
        grabMouse();
    }
    rebuildSpatialIndex();
    rebuildBackgroundSelectionOcclusionRegion();
    m_backgroundSelectionThrottleTimer->stop();
    m_backgroundSelectionFrameTimer.invalidate();
    m_backgroundSelectionThrottleMs = kBackgroundSelectionThrottleFastMs;
    m_backgroundSelectionRefreshCostMs = 0.0;
    m_backgroundSelectionThrottleRelaxFrames = 0;
    m_pendingBackgroundSelectionPos = startPos;
    m_hasPendingBackgroundSelectionUpdate = false;
    resetBackgroundSelectionCandidateCache();

    m_isSelecting = true;
    m_startPos = startPos;
    m_selectionStart = startPos;
    m_selectionRect = QRect(startPos, QSize(0, 0));
}

bool ListView::updateBackgroundSelection(const QPoint& currentPos, QRect* selectionRect)
{
    if (!m_isSelecting) {
        return false;
    }

    const QRect rect = QRect(m_selectionStart, currentPos).normalized();
    const bool shouldShowSelection = rect.width() > 3 || rect.height() > 3;
    const QRect previousRect = m_selectionRect;
    m_selectionRect = rect;
    m_pendingBackgroundSelectionPos = currentPos;
    m_hasPendingBackgroundSelectionUpdate = true;

    if (shouldShowSelection) {
        const int throttleMs = currentBackgroundSelectionThrottleMs();
        const bool shouldFlushNow =
            !m_backgroundSelectionFrameTimer.isValid() ||
            m_backgroundSelectionFrameTimer.elapsed() >= throttleMs;

        if (shouldFlushNow) {
            flushBackgroundSelectionUpdate();
        } else if (!m_backgroundSelectionThrottleTimer->isActive()) {
            const int remainingMs = qMax(1, throttleMs - static_cast<int>(m_backgroundSelectionFrameTimer.elapsed()));
            m_backgroundSelectionThrottleTimer->start(remainingMs);
        }
    } else {
        m_backgroundSelectionThrottleTimer->stop();
        m_hasPendingBackgroundSelectionUpdate = false;
        m_selectionRect = QRect();
        resetBackgroundSelectionCandidateCache();
        clearCheckedItem();
        if (!previousRect.isNull()) {
            update(previousRect.adjusted(-2, -2, 2, 2));
        }
    }

    if (selectionRect) {
        *selectionRect = shouldShowSelection ? rect : QRect();
    }

    return shouldShowSelection;
}

void ListView::endBackgroundSelection()
{
    const QRect previousRect = m_selectionRect;
    if (m_hasPendingBackgroundSelectionUpdate) {
        flushBackgroundSelectionUpdate();
    }
    m_backgroundSelectionThrottleTimer->stop();
    m_backgroundSelectionFrameTimer.invalidate();
    m_hasPendingBackgroundSelectionUpdate = false;
    m_isSelecting = false;
    m_startPos = QPoint();
    m_selectionStart = QPoint();
    m_selectionRect = QRect();
    m_backgroundSelectionOcclusionRegion = QRegion();
    m_backgroundSelectionThrottleMs = kBackgroundSelectionThrottleFastMs;
    m_backgroundSelectionRefreshCostMs = 0.0;
    m_backgroundSelectionThrottleRelaxFrames = 0;
    resetBackgroundSelectionCandidateCache();
    if (mouseGrabber() == this) {
        releaseMouse();
    }
    if (!previousRect.isNull()) {
        update(previousRect.adjusted(-2, -2, 2, 2));
    }
}

void ListView::flushBackgroundSelectionUpdate()
{
    if (!m_isSelecting || !m_hasPendingBackgroundSelectionUpdate) {
        return;
    }

    m_backgroundSelectionThrottleTimer->stop();
    const QRect rect = QRect(m_selectionStart, m_pendingBackgroundSelectionPos).normalized();
    QElapsedTimer refreshTimer;
    refreshTimer.start();
    if (rect.width() > 3 || rect.height() > 3) {
        refreshCheckedItem(rect);
    } else {
        clearCheckedItem();
    }

    updateBackgroundSelectionThrottle(qMax<qint64>(1, refreshTimer.elapsed()), rect);
    m_backgroundSelectionFrameTimer.restart();
    m_hasPendingBackgroundSelectionUpdate = false;
}

void ListView::markSpatialIndexDirty()
{
    m_spatialIndexDirty = true;
}

void ListView::resetBackgroundSelectionCandidateCache()
{
    m_backgroundSelectionPreviousUncoveredRegion = QRegion();
    m_backgroundSelectionCandidateStates.clear();
    m_backgroundSelectionCandidateScratchStates.clear();
    m_backgroundSelectionCandidateSet.clear();
}

void ListView::updateBackgroundSelectionCandidateCache(const QRegion& uncoveredSelectionRegion)
{
    if (!m_spatialIndex) {
        m_backgroundSelectionCandidateStates.clear();
        m_backgroundSelectionCandidateStates.reserve(m_itemStates.size());
        for (auto& state : m_itemStates) {
            if (uncoveredSelectionRegion.intersects(state.model.renderData.selectionContentRect)) {
                m_backgroundSelectionCandidateStates.append(&state);
            }
        }
        m_backgroundSelectionPreviousUncoveredRegion = uncoveredSelectionRegion;
        return;
    }

    const QRegion addedRegion = m_backgroundSelectionPreviousUncoveredRegion.isEmpty()
        ? uncoveredSelectionRegion
        : uncoveredSelectionRegion.subtracted(m_backgroundSelectionPreviousUncoveredRegion);

    if (!addedRegion.isEmpty()) {
        const QVector<QRect> addedRects = addedRegion.rects();
        for (const QRect& addedRect : addedRects) {
            const QVector<DesktopRootItemState*> queryStates = m_spatialIndex->query(addedRect);
            for (DesktopRootItemState* state : queryStates) {
                if (!state || m_backgroundSelectionCandidateSet.contains(state)) {
                    continue;
                }
                if (!uncoveredSelectionRegion.intersects(state->model.renderData.selectionContentRect)) {
                    continue;
                }
                m_backgroundSelectionCandidateSet.insert(state);
                m_backgroundSelectionCandidateStates.append(state);
            }
        }
    }

    if (!m_backgroundSelectionPreviousUncoveredRegion.isEmpty()) {
        const QRegion removedRegion = m_backgroundSelectionPreviousUncoveredRegion.subtracted(uncoveredSelectionRegion);
        if (!removedRegion.isEmpty()) {
            m_backgroundSelectionCandidateScratchStates.clear();
            m_backgroundSelectionCandidateScratchStates.reserve(m_backgroundSelectionCandidateStates.size());
            for (DesktopRootItemState* state : m_backgroundSelectionCandidateStates) {
                if (state && uncoveredSelectionRegion.intersects(state->model.renderData.selectionContentRect)) {
                    m_backgroundSelectionCandidateScratchStates.append(state);
                    continue;
                }
                if (state) {
                    m_backgroundSelectionCandidateSet.remove(state);
                }
            }
            m_backgroundSelectionCandidateStates.swap(m_backgroundSelectionCandidateScratchStates);
        }
    }

    m_backgroundSelectionPreviousUncoveredRegion = uncoveredSelectionRegion;
}

void ListView::rebuildBackgroundSelectionOcclusionRegion()
{
    m_backgroundSelectionOcclusionRegion = QRegion();

    BoxManager* manager = BoxManager::instance();
    if (!manager) {
        return;
    }

    const QWidget* sharedParent = parentWidget();
    for (const QPointer<LinBox>& boxPtr : manager->allBoxes()) {
        LinBox* box = boxPtr.data();
        if (!box || !box->isVisible()) {
            continue;
        }

        QRect boxRectInView;
        if (box->parentWidget() == sharedParent) {
            boxRectInView = QRect(mapFromParent(box->geometry().topLeft()), box->geometry().size());
        } else {
            boxRectInView = QRect(mapFromGlobal(box->mapToGlobal(QPoint(0, 0))), box->size());
        }

        if (!boxRectInView.isEmpty()) {
            m_backgroundSelectionOcclusionRegion += boxRectInView;
        }
    }
}

int ListView::currentBackgroundSelectionThrottleMs() const
{
    return m_backgroundSelectionThrottleMs;
}

void ListView::updateBackgroundSelectionThrottle(qint64 refreshElapsedMs, const QRect& selectionRect)
{
    const qreal viewportArea = qMax(1, width() * height());
    const qreal selectionArea = qMax(1, selectionRect.width() * selectionRect.height());
    const qreal coverage = qBound<qreal>(0.0, selectionArea / viewportArea, 1.0);
    const qreal weightedCostMs = qMax<qreal>(1.0, static_cast<qreal>(refreshElapsedMs)) * (1.0 + coverage * 0.35);

    if (m_backgroundSelectionRefreshCostMs <= 0.0) {
        m_backgroundSelectionRefreshCostMs = weightedCostMs;
    } else {
        m_backgroundSelectionRefreshCostMs = (m_backgroundSelectionRefreshCostMs * 0.7) + (weightedCostMs * 0.3);
    }

    int targetThrottleMs = kBackgroundSelectionThrottleFastMs;
    if (m_backgroundSelectionRefreshCostMs >= 9.0 || refreshElapsedMs >= 12) {
        targetThrottleMs = kBackgroundSelectionThrottleMaxMs;
    } else if (m_backgroundSelectionRefreshCostMs >= 5.5 || refreshElapsedMs >= 8) {
        targetThrottleMs = kBackgroundSelectionThrottleBusyMs;
    } else if (m_backgroundSelectionRefreshCostMs >= 2.5 || refreshElapsedMs >= 4) {
        targetThrottleMs = kBackgroundSelectionThrottleBalancedMs;
    }

    if (targetThrottleMs > m_backgroundSelectionThrottleMs) {
        m_backgroundSelectionThrottleMs = targetThrottleMs;
        m_backgroundSelectionThrottleRelaxFrames = 0;
    } else if (targetThrottleMs < m_backgroundSelectionThrottleMs) {
        ++m_backgroundSelectionThrottleRelaxFrames;
        if (m_backgroundSelectionThrottleRelaxFrames >= kBackgroundSelectionThrottleRelaxFrameThreshold) {
            m_backgroundSelectionThrottleMs = targetThrottleMs;
            m_backgroundSelectionThrottleRelaxFrames = 0;
        }
    } else {
        m_backgroundSelectionThrottleRelaxFrames = 0;
    }

    if (m_isSelecting && m_backgroundSelectionThrottleTimer && m_backgroundSelectionThrottleTimer->isActive() && m_backgroundSelectionFrameTimer.isValid()) {
        const int remainingMs = qMax(1, m_backgroundSelectionThrottleMs - static_cast<int>(m_backgroundSelectionFrameTimer.elapsed()));
        m_backgroundSelectionThrottleTimer->start(remainingMs);
    }
}

void ListView::markDesktopRenderDataDirty(DesktopRootItemState* state)
{
    if (!state) {
        return;
    }

    state->renderDataDirty = true;
}

void ListView::markAllDesktopRenderDataDirty()
{
    for (auto& state : m_itemStates) {
        state.renderDataDirty = true;
    }
}

void ListView::markDesktopStateIndexDirty()
{
    m_desktopStateIndexDirty = true;
}

void ListView::rebuildDesktopStateIndex()
{
    if (!m_desktopStateIndexDirty) {
        return;
    }

    m_desktopStateIndex.clear();
    m_desktopStateIndex.reserve(m_itemStates.size());
    for (auto& state : m_itemStates) {
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            m_desktopStateIndex.insert(normalizedPath, &state);
        }
    }
    m_desktopStateIndexDirty = false;
}

void ListView::rebuildSpatialIndex()
{
    if (!m_spatialIndex) {
        m_spatialIndex = std::make_unique<DesktopQuadTree>(rect());
    }

    const QRect currentBounds = rect();
    if (!m_spatialIndexDirty && m_spatialIndexBounds == currentBounds) {
        return;
    }

    m_spatialIndex->setBounds(currentBounds);
    m_spatialIndex->rebuild(collectAutoArrangeStates());
    m_spatialIndexBounds = currentBounds;
    m_spatialIndexDirty = false;
}

QVector<DesktopRootItemState> ListView::queryItemsInRect(const QRect& rect) const
{
    QVector<DesktopRootItemState> results;

    for (const DesktopRootItemState& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        if (state.model.renderData.itemRect.intersects(rect)) {
            results.push_back(state);
        }
    }

    for (const QPointer<LinBox>& boxPtr : BoxManager::instance()->allBoxes()) {
        LinBox* box = boxPtr.data();
        if (!box) {
            continue;
        }
        for (const DesktopRootItemState& state : box->itemStates()) {
            if (state.model.renderData.itemRect.intersects(rect)) {
                results.push_back(state);
            }
        }
    }

    return results;
}

void ListView::dragEnterEvent(QDragEnterEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->mimeData()->hasUrls()) {
        const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
        const bool desktopSource = (event->source() == this);
        const bool boxSource = isBoxDragSource(event->source());
        const bool externalSource = !desktopSource && !boxSource;
        const QPoint globalPos = mapToGlobal(event->pos());
        if (isMappedDragStillInsideSourceBox(event->source(), globalPos)) {
            if (m_dragTip) {
                m_dragTip->hide();
            }
            clearDesktopAutoArrangeReorderPreview();
            event->ignore();
            return;
        }
        if (!desktopSource) {
            if (shouldPreferShortcutForDrop(draggedPaths, event->keyboardModifiers(), externalSource)) {
                event->setDropAction(Qt::LinkAction);
            } else if (event->keyboardModifiers() & Qt::ControlModifier) {
                event->setDropAction(Qt::CopyAction);
            } else {
                event->setDropAction(Qt::MoveAction);
            }
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                event->dropAction(),
                QCoreApplication::translate("ListView", "Desktop"));
            event->accept();
            return;
        }
        if (shouldPreferShortcutForDrop(draggedPaths, event->keyboardModifiers(), false)) {
            event->setDropAction(Qt::LinkAction);
        } else if (event->keyboardModifiers() & Qt::ControlModifier) {
            event->setDropAction(Qt::CopyAction);
        } else {
            event->setDropAction(Qt::MoveAction);
        }
        ListViewFileOpsHelper::updateInternalDragDescription(
            event->mimeData(),
            event->dropAction(),
            QCoreApplication::translate("ListView", "Desktop"));
        event->accept();
    }
}

void ListView::dragLeaveEvent(QDragLeaveEvent* event)
{
    if (m_dragTip) m_dragTip->hide();
    clearDesktopAutoArrangeReorderPreview();
    event->accept();
}

void ListView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        if (m_dragTip) m_dragTip->hide();
        clearDesktopAutoArrangeReorderPreview();
        return;
    }
    const QPoint globalPos = mapToGlobal(event->pos());
    if (isMappedDragStillInsideSourceBox(event->source(), globalPos)) {
        if (m_dragTip) {
            m_dragTip->hide();
        }
        clearDesktopAutoArrangeReorderPreview();
        event->ignore();
        return;
    }
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
    const bool syntheticDesktopPreview =
        event->source() == nullptr &&
        m_isDragging &&
        !m_draggedDesktopPath.isEmpty() &&
        !m_ghosts.isEmpty();
    const bool desktopSource = (event->source() == this) || syntheticDesktopPreview;
    const bool boxSource = !syntheticDesktopPreview && isBoxDragSource(event->source());
    const bool externalSource = !desktopSource && !boxSource;
    const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
        draggedPaths, event->keyboardModifiers(), externalSource);
    if (preferShortcutForDrop) {
        event->setDropAction(Qt::LinkAction);
    } else if (event->keyboardModifiers() & Qt::ControlModifier) {
        event->setDropAction(Qt::CopyAction);
    } else {
        event->setDropAction(Qt::MoveAction);
    }
    event->accept();

    const bool isInternalDrag = desktopSource;

    // 1. Find if we are hovering over an item
    const DesktopRootItemState* hitItem = findDesktopItemAt(event->pos());
    if (hitItem && isDesktopPathSelected(hitItem->model.filePath)) {
        hitItem = nullptr;
    }

    bool isCtrl = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool canPreviewDesktopReorder = isInternalDrag &&
        m_iconShowRule.autoArrange &&
        m_iconShowRule.sortType == SortOrderType::SortByCustom &&
        !preferShortcutForDrop &&
        !isCtrl;
    const QStringList reorderDraggedPaths = canPreviewDesktopReorder
        ? orderedDraggedPaths(draggedPaths)
        : QStringList();
    if (canPreviewDesktopReorder &&
        !reorderDraggedPaths.isEmpty() &&
        !draggedPathsContainMappedSource(reorderDraggedPaths)) {
        updateDesktopAutoArrangeReorderPreview(event->pos(), draggedPaths);
        ListViewFileOpsHelper::updateInternalDragDescription(
            event->mimeData(),
            Qt::MoveAction,
            QCoreApplication::translate("ListView", "Desktop"));
        if (m_dragTip) {
            m_dragTip->hide();
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    clearDesktopAutoArrangeReorderPreview();

    if (hitItem) {
        const QString currentDraggedPath = this->draggedItemPath();
        if (isInternalDrag && !currentDraggedPath.isEmpty()) {
            DesktopRootItemModel draggedModel;
            const bool hasDraggedModel = resolveItemModelByPath(currentDraggedPath, draggedModel);
            const QString draggedClsid = hasDraggedModel ? draggedModel.itemData.getCLSID() : QString();
            const QString draggedFilePath = hasDraggedModel ? draggedModel.filePath : QString();
            bool isDraggedPC = (draggedClsid == APP_MY_COMPUTER_CLS);
            bool isDraggedNetwork = (draggedClsid == APP_NETWORK_CLS);
            bool isDraggedControlPanel = (draggedClsid == APP_CONTROL_PANEL_CLS);
            bool isDraggedControlPanel1 = (draggedClsid == APP_CONTROL_PANEL1_CLS);
            bool isDraggedRecycleBin = (draggedClsid == APP_RECYCLE_BIN_CLS && draggedFilePath == "::{645FF040-5081-101B-9F08-00AA002F954E}");
            bool isLinux = (draggedClsid == APP_LINUX_CLS);
            bool isDraggedSystemIcon = isDraggedPC || isDraggedNetwork || isDraggedControlPanel || isDraggedControlPanel1 || isDraggedRecycleBin || isLinux;

            if (isDraggedSystemIcon) {
                ListViewFileOpsHelper::updateInternalDragDescription(
                    event->mimeData(),
                    Qt::IgnoreAction,
                    hitItem->model.displayName,
                    true);
                ensureDragTip();
                m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
                m_dragTip->adjustSize();
                m_dragTip->move(event->pos() + QPoint(24, 24));
                m_dragTip->show();
                m_dragTip->raise();
                event->setDropAction(Qt::IgnoreAction);
                event->accept();
                return;
            }
        }

        ensureDragTip();

        QString clsid = hitItem->model.itemData.getCLSID();
        bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
        bool isPC = (clsid == APP_MY_COMPUTER_CLS);
        bool isNetwork = (clsid == APP_NETWORK_CLS);
        bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
        bool isFolder = (hitItem->model.objectType == ObjectType::Folder_ObjectType ||
                         hitItem->model.objectType == ObjectType::SpecialFolder ||
                         hitItem->model.objectType == ObjectType::DiskLetter);

        if (isPC || isNetwork || isControlPanel) {
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                Qt::IgnoreAction,
                hitItem->model.displayName,
                true);
            m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            return;
        }

        QString actionKeyword;
        QString highlightColor = "#70a1ff";
        DesktopRootItemModel draggedModel;
        const QString draggedPathForTip = this->draggedItemPath();
        const bool isDraggedDiskLetter = !draggedPathForTip.isEmpty()
            && resolveItemModelByPath(draggedPathForTip, draggedModel)
            && draggedModel.objectType == ObjectType::DiskLetter;
        QString htmlText;

        if (isRecycleBin) {
            actionKeyword = "Move to";
            highlightColor = "#ff4d4d";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        } else if (isFolder && preferShortcutForDrop) {
            actionKeyword = "Create shortcut in";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>闂?/b> <span>%2</span> <b style='color:%1'>闂備焦褰冮懟顖炲垂閸楃儐鍤堟繛宸簼閹崇娀鏌?/b>")
                .arg(highlightColor, hitItem->model.displayName);
        } else if (isFolder) {
            actionKeyword = isCtrl ? "Copy to" : "Move to";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        } else {
            actionKeyword = "Open with";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        }
        if (isRecycleBin) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_MOVE,
                QCoreApplication::translate("ListView", "Move to %1"),
                hitItem->model.displayName);
        } else if (isFolder && preferShortcutForDrop) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_LINK,
                QCoreApplication::translate("ListView", "Create shortcut in %1"),
                hitItem->model.displayName);
        } else if (!isFolder) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_LABEL,
                QCoreApplication::translate("ListView", "Open with %1"),
                hitItem->model.displayName);
        } else {
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                event->dropAction(),
                hitItem->model.displayName);
        }

        m_dragTip->setText(htmlText);
        m_dragTip->adjustSize();
        m_dragTip->move(event->pos() + QPoint(24, 24));
        m_dragTip->show();
        m_dragTip->raise();
        event->accept();
    } else {
        ListViewFileOpsHelper::updateInternalDragDescription(
            event->mimeData(),
            event->dropAction(),
            QCoreApplication::translate("ListView", "Desktop"));
        if (preferShortcutForDrop) {
            ensureDragTip();
            const QString htmlText = QString("<b style='color:#70a1ff'>Create shortcut in</b> <span>Desktop</span>");
            m_dragTip->setText(htmlText);
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
        } else if (isCtrl) {
            ensureDragTip();
            const QString htmlText = QString("<b style='color:#70a1ff'>Copy to</b> <span>Desktop</span>");
            m_dragTip->setText(htmlText);
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
        } else if (boxSource && !isNormalBoxDragSource(event->source())) {
            ensureDragTip();
            const QString htmlText = QString("<b style='color:#70a1ff'>Move to</b> <span>Desktop</span>");
            m_dragTip->setText(htmlText);
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
        } else if (m_dragTip) {
            m_dragTip->hide();
        }
    }
}

void ListView::dropEvent(QDropEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (m_dragTip) m_dragTip->hide();
    const QPoint globalPos = mapToGlobal(event->pos());
    if (isMappedDragStillInsideSourceBox(event->source(), globalPos)) {
        clearDesktopAutoArrangeReorderPreview();
        event->ignore();
        return;
    }

    const DesktopRootItemState* hitItem = findDesktopItemAt(event->pos());
    if (hitItem && isDesktopPathSelected(hitItem->model.filePath)) {
        hitItem = nullptr;
    }

    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) {
        clearDesktopAutoArrangeReorderPreview();
        return;
    }
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(mime);
    const bool desktopSource = (event->source() == this);
    const bool boxSource = isBoxDragSource(event->source());
    const bool normalBoxSource = isNormalBoxDragSource(event->source());
    const bool mappedBoxSource = isMappedBoxDragSource(event->source());
    const bool externalSource = !desktopSource && !boxSource;
    const bool desktopReorderCopyMode = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool desktopReorderShortcutMode = shouldPreferShortcutForDrop(
        draggedPaths, event->keyboardModifiers(), externalSource);
    const QStringList desktopReorderDraggedPaths = desktopSource
        ? orderedDraggedPaths(draggedPaths)
        : QStringList();
    const bool forceDesktopReorder = desktopSource &&
        m_iconShowRule.autoArrange &&
        m_iconShowRule.sortType == SortOrderType::SortByCustom &&
        !desktopReorderCopyMode &&
        !desktopReorderShortcutMode &&
        !desktopReorderDraggedPaths.isEmpty() &&
        !draggedPathsContainMappedSource(draggedPaths);

    const bool plainNormalBoxRestore = normalBoxSource &&
        !(event->keyboardModifiers() & Qt::ControlModifier) &&
        !shouldPreferShortcutForDrop(draggedPaths, event->keyboardModifiers(), false);

    // Drop onto an APP/File: Open with it; Drop onto Folder: Copy to it
    if (hitItem && !isDesktopPathSelected(hitItem->model.filePath) && !forceDesktopReorder && !plainNormalBoxRestore) {
        const bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
            draggedPaths, event->keyboardModifiers(), externalSource || mappedBoxSource);
        DropInteractionOptions options;
        options.copyMode = isCopyAction;
        options.preferShortcut = preferShortcutForDrop;
        options.preHideDesktopSources = desktopSource;
        options.finalizeMovedSourcesAfterSuccess = boxSource || mappedBoxSource;
        const bool handled = handleDroppedPathsOnItemTarget(draggedPaths, hitItem->model, options);
        if (handled) {
            event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCopyAction ? Qt::CopyAction : Qt::MoveAction));
            event->accept();
            return;
        }

        event->ignore();
        return;
    }

    if (draggedPaths.isEmpty()) {
        clearDesktopAutoArrangeReorderPreview();
        return;
    }

    // Handle dropping onto the desktop background (no hitItem)
    if (!hitItem) {
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
            draggedPaths, event->keyboardModifiers(), externalSource || mappedBoxSource);
        const bool internalBoxSource = normalBoxSource && !draggedPathsContainMappedSource(draggedPaths);
        if ((externalSource || mappedBoxSource) || isCopyAction || preferShortcutForDrop) {
            if (!m_iconShowRule.autoArrange) {
                for (const QString& localPath : draggedPaths) {
                    if (localPath.isEmpty()) {
                        continue;
                    }
                    const QString reservedPath = preferShortcutForDrop
                        ? ListViewFileOpsHelper::uniqueShortcutPathForTarget(localPath, m_desktopDir)
                        : ListViewFileOpsHelper::uniquePasteDestinationPath(localPath, m_desktopDir);
                    reserveCreatedItemPosition(reservedPath, event->pos());
                }
            }

            std::vector<std::wstring> srcPaths;
            for (const QString& localPath : draggedPaths) {
                if (!localPath.isEmpty()) srcPaths.push_back(localPath.toStdWString());
            }

            if (!srcPaths.empty()) {
                // 闂佸搫绉烽～澶婄暤娓氣偓閺屻劑顢欓悙顒傦紭婵烇絽娴傞崳锝夊Χ閹殿喚绠旈柨鏇炲€甸崑鎾村緞鐎ｎ亶浠存繝銏ｆ硾缁夋挳宕哄Δ鍛闁哄稁鍊ｉ埡鍛?
                bool result;
                if (preferShortcutForDrop) {
                    result = createShortcutsInFolder(draggedPaths, m_desktopDir);
                } else if (isCopyAction) {
                    result = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
                } else {
                    result = ListViewFileOpsHelper::moveFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
                }
                if (result) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                    event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCopyAction ? Qt::CopyAction : Qt::MoveAction));
                    event->accept();
                    return;
                }
                event->ignore();
                return;
            }
        }

        if (internalBoxSource && !isCopyAction && !preferShortcutForDrop) {
            QPoint dropPos = event->pos();
            QMap<QString, QPoint> targetPositions;
            QMap<QString, QString> targetReservationKeys;
            int offsetIndex = 0;
            const QStringList orderedPaths = orderedDraggedPaths(draggedPaths);
            const QStringList pathsToRestore = orderedPaths.isEmpty() ? draggedPaths : orderedPaths;

            for (const QString& draggedPath : pathsToRestore) {
                const QString normalizedPath = normalizeDesktopItemPath(draggedPath);
                LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(normalizedPath);
                if (!sourceBox) {
                    continue;
                }

                const QPoint requestedPos = dropPos + QPoint(
                    offsetIndex * qMax(8, m_colSpacing / 2),
                    offsetIndex * qMax(8, m_rowSpacing / 2));
                ++offsetIndex;
                const QString reservationKey = QString("__box_restore__%1").arg(normalizedPath);
                reserveCreatedItemPosition(reservationKey, requestedPos);
                targetReservationKeys.insert(normalizedPath, reservationKey);
            }

            if (!targetReservationKeys.isEmpty()) {
                QSet<LinBox*> sourceBoxes;
                for (auto it = targetReservationKeys.begin(); it != targetReservationKeys.end(); ++it) {
                    DesktopRootItemState restoredState;
                    LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(it.key());
                    if (!sourceBox || !sourceBox->takeItemState(it.key(), restoredState, false)) {
                        continue;
                    }

                    QPoint targetPos;
                    if (!takeReservedCreatedItemPosition(it.value(), targetPos)) {
                        targetPos = resolveCreatePosition(dropPos);
                    }
                    targetPositions.insert(it.key(), targetPos);
                    resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
                    restoredState.model.position = targetPos;
                    m_itemStates.push_back(restoredState);
                    markDesktopStateIndexDirty();
                    sourceBoxes.insert(sourceBox);
                }

                for (LinBox* sourceBox : sourceBoxes) {
                    if (!sourceBox) {
                        continue;
                    }
                    sourceBox->refreshLayout();
                    sourceBox->update();
                }

                updateAllItemRenderData();
                rebuildSpatialIndex();
                update();
                requestSaveBoxes();
                event->accept();
                return;
            }

            event->ignore();
            return;
        }
    }

    if (desktopSource) {
        const bool copyMode = (event->keyboardModifiers() & Qt::ControlModifier);
        const QStringList activeDesktopDragPaths = orderedDraggedPaths(draggedPaths);
        const bool hasMappedSourceItems = draggedPathsContainMappedSource(draggedPaths);
        if (hasMappedSourceItems) {
            bool ok = false;
            if (copyMode) {
                ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
            } else {
                std::vector<std::wstring> srcPaths;
                for (const QString& localPath : draggedPaths) {
                    if (!localPath.isEmpty()) {
                        srcPaths.push_back(QDir::toNativeSeparators(localPath).toStdWString());
                    }
                }
                if (!srcPaths.empty()) {
                    ok = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                }
            }

            if (ok) {
                clearCheckedItem();
                applyAutoArrangeAfterDesktopMutation(false, false);
                requestSaveBoxes();
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        QPoint dropPos = event->pos();

        if (!activeDesktopDragPaths.isEmpty()) {
            if (m_iconShowRule.autoArrange) {
                const bool customSortDrag = (m_iconShowRule.sortType == SortOrderType::SortByCustom);
                const QStringList draggedDesktopPaths = activeDesktopDragPaths;
                const int customInsertIndex = customSortDrag
                    ? (m_desktopReorderInsertIndex >= 0
                        ? m_desktopReorderInsertIndex
                        : desktopCustomInsertionIndexForPos(dropPos, draggedDesktopPaths))
                    : -1;

                if (customSortDrag) {
                    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
                    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
                    QSet<QString> draggedPathSet;
                    QVector<DesktopRootItemState*> orderedDraggedStates;
                    for (const QString& path : draggedDesktopPaths) {
                        draggedPathSet.insert(normalizeDesktopItemPath(path));
                    }
                    for (DesktopRootItemState* state : orderedStates) {
                        if (state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath))) {
                            orderedDraggedStates.append(state);
                        }
                    }
                    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
                        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
                    }), orderedStates.end());
                    const int clampedIndex = qBound(0, customInsertIndex, orderedStates.size());
                    for (int i = 0; i < orderedDraggedStates.size(); ++i) {
                        orderedStates.insert(clampedIndex + i, orderedDraggedStates[i]);
                    }
                    assignCustomSortOrders(orderedStates);
                }

                applyAutoArrangeLayout(true, false);
                clearDesktopAutoArrangeReorderPreview();
                clearCheckedItem();
                event->acceptProposedAction();
                return;
            }

            const int itemHeight = desktopItemFootprintHeight();
            const int strideX = m_iconWidth + m_colSpacing;
            const int strideY = itemHeight + m_rowSpacing;
            const int layoutLeft = desktopLayoutLeft();
            const int layoutTop = desktopLayoutTop();
            const QPoint displacement = dropPos - m_startPos;

            QMap<QString, QPoint> targetPositions;
            bool canPlaceAll = true;
            QSet<QString> activeDraggedPathSet;
            QSet<quint64> occupiedSlots;
            int maxRows = 1;
            int maxCols = 1;
            if (m_iconShowRule.gridAlignment) {
                maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
                maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));
                for (const QString& draggedPath : activeDesktopDragPaths) {
                    activeDraggedPathSet.insert(normalizeDesktopItemPath(draggedPath));
                }
                occupiedSlots.unite(desktopBlockedOccupiedCells());
                for (const auto& otherState : m_itemStates) {
                    if (!shouldPaintDesktopModelState(otherState)) {
                        continue;
                    }
                    const QString otherPath = normalizeDesktopItemPath(otherState.model.filePath);
                    if (!otherPath.isEmpty() && activeDraggedPathSet.contains(otherPath)) {
                        continue;
                    }
                    const QPoint otherCell = pointToGridCell(otherState.model.position, layoutLeft, layoutTop, strideX, strideY);
                    occupiedSlots.insert(packGridCell(qBound(0, otherCell.x(), maxCols - 1), qBound(0, otherCell.y(), maxRows - 1)));
                }
                for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
                    const QPoint pendingCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
                    occupiedSlots.insert(packGridCell(qBound(0, pendingCell.x(), maxCols - 1), qBound(0, pendingCell.y(), maxRows - 1)));
                }
            }

            for (const QString& draggedPath : activeDesktopDragPaths) {
                DesktopRootItemState* state = findDesktopStateByPath(draggedPath);
                if (!state) {
                    continue;
                }

                QPoint targetPos = state->model.position + displacement;
                if (m_iconShowRule.gridAlignment) {
                    const int gridX = qRound((double)(targetPos.x() - layoutLeft) / strideX);
                    const int gridY = qRound((double)(targetPos.y() - layoutTop) / strideY);
                    targetPos.setX(layoutLeft + gridX * strideX);
                    targetPos.setY(layoutTop + gridY * strideY);

                    QPoint targetCell = pointToGridCell(targetPos, layoutLeft, layoutTop, strideX, strideY);
                    targetCell.setX(qBound(0, targetCell.x(), maxCols - 1));
                    targetCell.setY(qBound(0, targetCell.y(), maxRows - 1));
                    if (occupiedSlots.contains(packGridCell(targetCell.x(), targetCell.y()))) {
                        targetPos = resolveNearestGridCreatePosition(targetPos, occupiedSlots);
                        targetCell = pointToGridCell(targetPos, layoutLeft, layoutTop, strideX, strideY);
                        targetCell.setX(qBound(0, targetCell.x(), maxCols - 1));
                        targetCell.setY(qBound(0, targetCell.y(), maxRows - 1));
                    }
                    occupiedSlots.insert(packGridCell(targetCell.x(), targetCell.y()));
                }

                targetPos.setX(qMax(layoutLeft, qMin(targetPos.x(), desktopUsableRight() - m_iconWidth + 1)));
                targetPos.setY(qMax(layoutTop, qMin(targetPos.y(), desktopUsableBottom() - itemHeight + 1)));
                targetPositions.insert(draggedPath, targetPos);
            }

            if (canPlaceAll) {
                for (auto it = targetPositions.begin(); it != targetPositions.end(); ++it) {
                    if (DesktopRootItemState* state = findDesktopStateByPath(it.key())) {
                        state->model.position = it.value();
                        state->model.boxId.clear();
                        state->model.visible = true;
                        markDesktopRenderDataDirty(state);
                    }
                }
                updateAllItemRenderData();
                rebuildSpatialIndex();
                update();
            }
            requestSaveBoxes();
        }
    } else {
        // --- 婵犮垼鍩栭悧鐘诲磿閹绢喖绠柡宓啫姹查梻渚囧亝濡叉帞娆?---
        QString desktopDir = m_desktopDir;
        std::vector<std::wstring> srcPaths;
        for (const QString& local : draggedPaths) {
            if (!local.isEmpty() && QDir::toNativeSeparators(local) != QDir::toNativeSeparators(desktopDir)) {
                srcPaths.push_back(QDir::toNativeSeparators(local).toStdWString());
            }
        }
        
        if (!srcPaths.empty()) {
            qDebug() << "External drop into Desktop:" << desktopDir;
            // 闁荤姴顑呴崯浼村极?Shell API 闁哄鏅滅粙鏍€侀幋锕€绠柣銈庡灣椤︻噣鏌?婵烇絽娲︾换鍐偓?confirm 婵?true闂?
            CSystemApi::CopyFiles(srcPaths, desktopDir.toStdWString());
        }
    }
    applyAutoArrangeAfterDesktopMutation(true, false);
    if (m_dragTip) m_dragTip->hide();
    event->acceptProposedAction();
}

void ListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->button() == Qt::LeftButton) {
        if (DesktopRootItemState* hitState = findDesktopStateAt(event->pos())) {
            clearCutMarks();
            openDesktopRootItem(hitState->model);
            event->accept();
            return;
        }

        if (m_iconShowRule.doubleClickHideIcons) {
            onMenuShowDesktopIcons();
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ListView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const QPoint angleDelta = event->angleDelta();
        const int delta = angleDelta.y() != 0 ? angleDelta.y() : angleDelta.x();
        if (delta != 0) {
            stepIconViewMode(delta > 0 ? 1 : -1);
            event->accept();
            return;
        }
    }

    QWidget::wheelEvent(event);
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    // 闁兼儳鍢茶ぐ鍥┾偓鍦仦濡炲倿鎯冮崟顖涙殯闁烩晜顭囨慨鎼佸箑?
    const bool shiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    const bool ctrlPressed = GetAsyncKeyState(VK_CONTROL) & 0x8000;

    const bool extendSelection = (event->modifiers() & Qt::ShiftModifier) || shiftPressed;
    const bool shortcutPaste = ctrlPressed && (event->key() == Qt::Key_V);
    const bool shortcutNewFolder = ctrlPressed && extendSelection && (event->key() == Qt::Key_N);
    const bool permanentDelete = shiftPressed && (event->key() == Qt::Key_Delete);

    qDebug() << QString("Press Key: %1, Shift: %2, Delete: %3, permanentDelete: %4")
            .arg(QKeySequence(event->key()).toString())
            .arg(shiftPressed ? "Yes" : "No")
            .arg(event->key() == Qt::Key_Delete ? "Yes" : "No")
            .arg(permanentDelete ? "Yes" : "No");
    if (event->matches(QKeySequence::SelectAll)) {
        QStringList desktopPaths;
        for (const auto& state : m_itemStates) {
            if (shouldPaintDesktopModelState(state)) {
                desktopPaths.append(normalizeDesktopItemPath(state.model.filePath));
            }
        }
        applyDesktopSelection(desktopPaths, m_focusedDesktopPath, false);
        applySelectionState({}, QString(), false);
        event->accept();
        qDebug() << "Select All triggered";
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        writeSelectionToClipboard(false);
        event->accept();
        qDebug() << "Copy triggered";
        return;
    }
    //first judge if it's a Shift+Delete for permanent delete, if not, then handle regular Delete key
    if (permanentDelete) {
        onMenuDelete(true);
        event->accept();
        qDebug() << "Permanent Delete triggered";
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        writeSelectionToClipboard(true);
        event->accept();
        qDebug() << "Cut triggered";
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        clearCutMarks();
        onMenuPaste();
        event->accept();
        qDebug() << "Paste triggered";
        return;
    }
    
    if (shortcutPaste) {
        onMenuPasteShortcut();
        event->accept();
        qDebug() << "Paste Shortcut triggered";
        return;
    }
    if (shortcutNewFolder) {
        createNewFolderInHost();
        event->accept();
        qDebug() << "New Folder triggered";
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        clearSelectionState();
        RenameManager::instance()->cancelRename();
        event->accept();
        qDebug() << "Escape triggered: Cleared selection and canceled rename";
        return;
    }
    if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Return) {
        const DesktopRootItemState* desktopTarget = !m_focusedDesktopPath.isEmpty()
            ? findDesktopStateByPath(m_focusedDesktopPath)
            : (m_selectedDesktopPaths.isEmpty() ? nullptr : findDesktopStateByPath(m_selectedDesktopPaths.first()));
        const QString targetPath = desktopTarget
            ? QString()
            : (focusedItemPath().isEmpty()
                ? (selectedBoxPaths().isEmpty() ? QString() : selectedBoxPaths().first())
                : focusedItemPath());
        if (desktopTarget && !desktopTarget->model.filePath.isEmpty()) {
            CSystemApi::File_RightMenuOperations(reinterpret_cast<const TCHAR*>(QDir::toNativeSeparators(desktopTarget->model.filePath).utf16()), Z_PROPERTIES);
            event->accept();
            qDebug() << "Alt+Enter triggered: Opened properties for" << desktopTarget->model.displayName;
            return;
        }
        if (!targetPath.isEmpty() && showPropertiesForPath(targetPath)) {
            event->accept();
            qDebug() << "Alt+Enter triggered: Opened properties for" << QFileInfo(targetPath).completeBaseName();
            return;
        }
    }
    if (ctrlPressed && event->key() == Qt::Key_Space) {
        if (!m_focusedDesktopPath.isEmpty()) {
            const bool selected = isDesktopPathSelected(m_focusedDesktopPath);
            setDesktopItemSelected(m_focusedDesktopPath, !selected);
            if (m_anchorDesktopPath.isEmpty()) {
                m_anchorDesktopPath = m_focusedDesktopPath;
            }
        } else if (!m_focusedBoxPath.isEmpty()) {
            toggleItemSelection(m_focusedBoxPath);
            if (m_anchorBoxPath.isEmpty()) {
                m_anchorBoxPath = m_focusedBoxPath;
            }
        }
        event->accept();
        qDebug() << "Ctrl+Space triggered: Toggled selection for focused item";
        return;
    }
    if (event->key() == Qt::Key_Space) {
        const DesktopRootItemState* desktopTarget = !m_hoveredDesktopPath.isEmpty()
            ? findDesktopStateByPath(m_hoveredDesktopPath)
            : (!m_focusedDesktopPath.isEmpty()
                ? findDesktopStateByPath(m_focusedDesktopPath)
                : (m_selectedDesktopPaths.isEmpty() ? nullptr : findDesktopStateByPath(m_selectedDesktopPaths.first())));
        const QString targetPath = desktopTarget
            ? desktopTarget->model.filePath
            : (focusedItemPath().isEmpty()
                ? (selectedBoxPaths().isEmpty() ? QString() : selectedBoxPaths().first())
                : focusedItemPath());
        if (!targetPath.isEmpty() && sendPreviewRequestByPath(targetPath)) {
            event->accept();
            qDebug() << "Space triggered: Sent preview request for" << QFileInfo(targetPath).completeBaseName();
            return;
        }
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const DesktopRootItemState* desktopTarget = !m_focusedDesktopPath.isEmpty()
            ? findDesktopStateByPath(m_focusedDesktopPath)
            : (m_selectedDesktopPaths.isEmpty() ? nullptr : findDesktopStateByPath(m_selectedDesktopPaths.first()));
        const QString targetPath = desktopTarget
            ? QString()
            : (focusedItemPath().isEmpty()
                ? (selectedBoxPaths().isEmpty() ? QString() : selectedBoxPaths().first())
                : focusedItemPath());
        if (desktopTarget) {
            clearCutMarks();
            openDesktopRootItem(desktopTarget->model);
            event->accept();
            qDebug() << "Enter triggered: Opened" << desktopTarget->model.displayName;
            return;
        }
        if (!targetPath.isEmpty() && openItemByPath(targetPath)) {
            event->accept();
            qDebug() << "Enter triggered: Opened" << QFileInfo(targetPath).completeBaseName();
            return;
        }
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
        moveFocusInHost(nullptr,
            event->key() == Qt::Key_Left ? FocusMoveDirection::Left : FocusMoveDirection::Up,
            extendSelection,
            ctrlPressed);
        event->accept();
        qDebug() << (event->key() == Qt::Key_Left ? "Left" : "Up") << "arrow triggered: Moved focus";
        return;
    }
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
        moveFocusInHost(nullptr,
            event->key() == Qt::Key_Right ? FocusMoveDirection::Right : FocusMoveDirection::Down,
            extendSelection,
            ctrlPressed);
        event->accept();
        qDebug() << (event->key() == Qt::Key_Right ? "Right" : "Down") << "arrow triggered: Moved focus";
        return;
    }
    if (event->key() == Qt::Key_F2) {
        if (m_selectedDesktopPaths.size() == 1) {
            if (const DesktopRootItemState* state = findDesktopStateByPath(m_selectedDesktopPaths.first())) {
                RenameManager::instance()->startRename(state->model.filePath);
                qDebug() << "F2 triggered: Started rename for" << state->model.displayName;
                return;
            }
        } else if (selectedBoxPaths().size() == 1) {
            RenameManager::instance()->startRename(selectedBoxPaths().first());
            qDebug() << "F2 triggered: Started rename for" << selectedBoxPaths().first();
            return;
        }
    }
    if (event->key() == Qt::Key_Delete) {
        /*const Qt::KeyboardModifiers modifiers = event->modifiers() | QGuiApplication::keyboardModifiers();
        bool permanently = (modifiers & Qt::ShiftModifier);*/
        onMenuDelete(false);
        event->accept();
        qDebug() << "Delete triggered: Deleted selected items";
        return;
    }
    
    QWidget::keyPressEvent(event);
}

void ListView::enterEvent(QEvent* event)
{
    QWidget::enterEvent(event);
}

void ListView::leaveEvent(QEvent* event)
{
    clearHoveredDesktopItem();
    QWidget::leaveEvent(event);
}

void ListView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncLinWidgetLayer();
}

void ListView::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncLinWidgetLayer();
}

void ListView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncLinWidgetLayer();
    QTimer::singleShot(100, this, [this]() {
        syncLinWidgetLayer();
    });
}

ListView* ListView::instance(QWidget* parent) {
    if (!m_instance) {
        m_instance = new ListView(parent);
    }
    return m_instance;
}

void ListView::initialize()
{
    // === 闂佸憡鑹鹃張顒勵敆?Windows 濠碘剝顨呴惌鍌氼焽娴兼潙绠抽柟鐑樺灥閻忔瑩鎮规担绋库挃闁?===
    bool osAutoArrange = false;
    bool osSnapToGrid = false;
    CSystemApi::GetDesktopConfigFlags(osAutoArrange, osSnapToGrid);
    m_iconShowRule.autoArrange = osAutoArrange;
    m_iconShowRule.gridAlignment = osSnapToGrid;
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = m_iconShowRule.sortType;
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
    }
    syncViewModeSizes(); // Sync initial sizes to member variables
    qDebug() << "Synced Windows Desktop Layout Rules: AutoArrange=" << osAutoArrange << ", SnapToGrid=" << osSnapToGrid;
    // ===================================

    const vector<ListItemData> allDatas = DesktopItemLoader().loadAllItems();

    m_itemStates.clear();
    markDesktopStateIndexDirty();
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }

    auto normalizeClsid = [](QString clsid) {
        return clsid.replace("::", "").replace("{", "").replace("}", "").toLower();
    };

    DesktopItemInitContext context;
    context.sysIconRegisterInfo = sysIconRegisterInfo;
    context.systemFolderCLSIDs  = SystemFolderCLSIDs;
    context.showHiddenFiles     = m_showHiddenFiles;
    context.getBoxId = [this](const QString& path) { return getBoxIdForItem(path); };

    for (const auto& data : allDatas) {
        DesktopRootItemState state;
        initDesktopRootItemModel(state.model, data, context);

        const QPoint nativePos(
            state.model.fileInfo.absoluteFilePath().isEmpty() ? 0 : data.itemData().position.x,
            state.model.fileInfo.absoluteFilePath().isEmpty() ? 0 : data.itemData().position.y);

        state.model.position = (nativePos.x() != 0 || nativePos.y() != 0)
            ? nativePos
            : QPoint(0, 0);
        m_itemStates.push_back(state);
        markDesktopStateIndexDirty();
    }


    // --- Compact Native Layout Algorithm (Fill the gaps) ---
    // 1. Gather all unique physical coordinates from the entire system desktop
    QVector<QPoint> nativeSlots;
    for (const auto& data : allDatas) {
        QPoint p(data.itemData().position.x, data.itemData().position.y);
        if (p.x() != 0 || p.y() != 0) {
            if (!nativeSlots.contains(p)) {
                nativeSlots.append(p);
            }
        }
    }

    // 2. Sort the physical slots (Column-major: Left->Right, Top->Bottom)
    std::sort(nativeSlots.begin(), nativeSlots.end(), [](const QPoint& a, const QPoint& b){
        if (qAbs(a.x() - b.x()) > 10) return a.x() < b.x();
        return a.y() < b.y();
    });


    QVector<DesktopRootItemState*> startupItemsWithNativePos;
    QVector<DesktopRootItemState*> startupItemsWithoutNativePos;
    startupItemsWithNativePos.reserve(m_itemStates.size());
    startupItemsWithoutNativePos.reserve(m_itemStates.size());
    for (auto& state : m_itemStates) {
        if (!state.model.visible || !state.model.boxId.isEmpty()) {
            continue;
        }
        if (state.model.position == QPoint(0, 0)) {
            startupItemsWithoutNativePos.push_back(&state);
        } else {
            startupItemsWithNativePos.push_back(&state);
        }
    }

    std::sort(startupItemsWithNativePos.begin(), startupItemsWithNativePos.end(), [](const DesktopRootItemState* a, const DesktopRootItemState* b) {
        const QPoint pa = a->model.position;
        const QPoint pb = b->model.position;
        if (qAbs(pa.x() - pb.x()) > 10) {
            return pa.x() < pb.x();
        }
        return pa.y() < pb.y();
    });

    qDebug() << "=== Compacted Native Layout (Model) ===";
    for (int i = 0; i < startupItemsWithNativePos.size() && i < nativeSlots.size(); ++i) {
        startupItemsWithNativePos[i]->model.position = nativeSlots[i];
        qDebug() << QString("Model item(%1) compacted to Position:(%2, %3) - Name: %4")
                    .arg(i)
                    .arg(nativeSlots[i].x())
                    .arg(nativeSlots[i].y())
                    .arg(startupItemsWithNativePos[i]->model.itemData.fullName());
    }

    for (DesktopRootItemState* state : startupItemsWithoutNativePos) {
        if (!state) continue;
        const QPoint generatedPos = generateItemPosition();
        state->model.position = generatedPos;
        qDebug() << QString("Deferred model item placed at Position:(%1, %2) - Name: %3")
                    .arg(generatedPos.x())
                    .arg(generatedPos.y())
                    .arg(state->model.itemData.fullName());
    }
    qDebug() << "======================================";
    updateAllItemRenderData();
}

void ListView::loadIconLayer()
{
    qDebug() << "[ListView] Starting reload. Model item count:" << m_itemStates.size();
    const bool shouldRestorePersistedLayout = m_isLoadingPersistence || m_forceConfigLayout;
    if (m_iconShowRule.autoArrange && !shouldRestorePersistedLayout) {
        qDebug() << "[ListView] Applying auto-layout (Stable Categorized).";
        applyAutoArrangeLayout(false, false);
    } else if (shouldRestorePersistedLayout) {
        qDebug() << "[ListView] Applying persistent layout.";
        loadPersistentLayout();
    } else if (m_iconShowRule.gridAlignment) {
        qDebug() << "[ListView] Applying grid layout.";
        loadGridLayout();
    } else {
        qDebug() << "[ListView] No layout rule active? Defaulting to grid.";
        loadGridLayout();
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();

    // Refresh all boxes to ensure their icons are positioned.
    for (auto& box : BoxManager::instance()->allBoxes()) {
        if (box) {
            box->refreshLayout();
        }
    }

    syncBoxVisibilityWithDesktopIcons();
}


QVector<DesktopRootItemState*> ListView::collectAutoArrangeStates() const
{
    QVector<DesktopRootItemState*> states;
    states.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty() || !state.model.visible) {
            continue;
        }
        states.push_back(const_cast<DesktopRootItemState*>(&state));
    }
    return states;
}

QVector<DesktopRootItemState*> ListView::collectGridOrderedDesktopStates() const
{
    QVector<DesktopRootItemState*> states;
    states.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        states.push_back(const_cast<DesktopRootItemState*>(&state));
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));

    std::stable_sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
        if (!a || !b) {
            return a != nullptr;
        }

        const int indexA = pointToGridLinearIndex(a->model.position, layoutLeft, layoutTop, strideX, strideY, maxRows);
        const int indexB = pointToGridLinearIndex(b->model.position, layoutLeft, layoutTop, strideX, strideY, maxRows);
        if (indexA != indexB) {
            return indexA < indexB;
        }
        if (a->model.position.x() != b->model.position.x()) {
            return a->model.position.x() < b->model.position.x();
        }
        if (a->model.position.y() != b->model.position.y()) {
            return a->model.position.y() < b->model.position.y();
        }
        return normalizeDesktopItemPath(a->model.filePath) < normalizeDesktopItemPath(b->model.filePath);
    });

    return states;
}

bool ListView::applyGridAlignedDesktopDropLayout(const QStringList& draggedPaths, const QPoint& releasePos)
{
    if (draggedPaths.isEmpty()) {
        return false;
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));

    QVector<DesktopRootItemState*> orderedStates = collectGridOrderedDesktopStates();
    if (orderedStates.isEmpty()) {
        return false;
    }

    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (!normalizedPath.isEmpty()) {
            draggedPathSet.insert(normalizedPath);
        }
    }
    if (draggedPathSet.isEmpty()) {
        return false;
    }

    QVector<DesktopRootItemState*> orderedDraggedStates;
    QVector<DesktopRootItemState*> remainingStates;
    orderedDraggedStates.reserve(draggedPathSet.size());
    remainingStates.reserve(orderedStates.size());

    for (DesktopRootItemState* state : orderedStates) {
        if (!state) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (draggedPathSet.contains(normalizedPath)) {
            orderedDraggedStates.append(state);
        } else {
            remainingStates.append(state);
        }
    }

    if (orderedDraggedStates.isEmpty()) {
        return false;
    }

    QHash<DesktopRootItemState*, QPoint> newPositions;
    newPositions.reserve(orderedStates.size());

    auto clampCellToBounds = [&](const QPoint& cell) {
        return QPoint(qBound(0, cell.x(), maxCols - 1), qBound(0, cell.y(), maxRows - 1));
    };

    auto normalizedCellForPos = [&](const QPoint& pos) {
        return clampCellToBounds(pointToGridCell(pos, layoutLeft, layoutTop, strideX, strideY));
    };

    QHash<DesktopRootItemState*, QPoint> originalDraggedCells;
    originalDraggedCells.reserve(orderedDraggedStates.size());
    int originalMinCellX = INT_MAX;
    int originalMinCellY = INT_MAX;
    int originalMaxCellX = INT_MIN;
    int originalMaxCellY = INT_MIN;

    for (DesktopRootItemState* state : orderedDraggedStates) {
        if (!state) {
            continue;
        }
        const QPoint cell = normalizedCellForPos(state->model.position);
        originalDraggedCells.insert(state, cell);
        originalMinCellX = qMin(originalMinCellX, cell.x());
        originalMinCellY = qMin(originalMinCellY, cell.y());
        originalMaxCellX = qMax(originalMaxCellX, cell.x());
        originalMaxCellY = qMax(originalMaxCellY, cell.y());
    }

    if (originalDraggedCells.isEmpty()) {
        return false;
    }

    int previewMinCellX = INT_MAX;
    int previewMinCellY = INT_MAX;
    bool hasPreviewCells = false;
    for (const auto& ghost : m_ghosts) {
        if (!ghost.widget || ghost.filePath.isEmpty()) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(ghost.filePath);
        if (!draggedPathSet.contains(normalizedPath)) {
            continue;
        }
        const QPoint cell = normalizedCellForPos(localPosFromGhostWidget(ghost.widget));
        previewMinCellX = qMin(previewMinCellX, cell.x());
        previewMinCellY = qMin(previewMinCellY, cell.y());
        hasPreviewCells = true;
    }

    QPoint targetTopLeftCell = hasPreviewCells
        ? QPoint(previewMinCellX, previewMinCellY)
        : normalizedCellForPos(releasePos);

    const int selectionWidth = originalMaxCellX - originalMinCellX + 1;
    const int selectionHeight = originalMaxCellY - originalMinCellY + 1;
    if (selectionWidth > maxCols || selectionHeight > maxRows) {
        return false;
    }

    targetTopLeftCell.setX(qBound(0, targetTopLeftCell.x(), maxCols - selectionWidth));
    targetTopLeftCell.setY(qBound(0, targetTopLeftCell.y(), maxRows - selectionHeight));

    QSet<quint64> occupiedSlots = desktopBlockedOccupiedCells();

    for (DesktopRootItemState* state : orderedDraggedStates) {
        if (!state || !originalDraggedCells.contains(state)) {
            continue;
        }

        const QPoint originalCell = originalDraggedCells.value(state);
        const QPoint relativeCell(originalCell.x() - originalMinCellX, originalCell.y() - originalMinCellY);
        const QPoint targetCell = targetTopLeftCell + relativeCell;
        const quint64 slotKey = packGridCell(targetCell.x(), targetCell.y());
        if (occupiedSlots.contains(slotKey)) {
            return false;
        }

        occupiedSlots.insert(slotKey);
        newPositions.insert(state, gridCellToPoint(targetCell.x(), targetCell.y(), layoutLeft, layoutTop, strideX, strideY));
    }

    QVector<DesktopRootItemState*> displacedStates;
    displacedStates.reserve(remainingStates.size());

    for (DesktopRootItemState* state : remainingStates) {
        if (!state) {
            continue;
        }

        const QPoint originalCell = normalizedCellForPos(state->model.position);
        const quint64 slotKey = packGridCell(originalCell.x(), originalCell.y());
        if (!occupiedSlots.contains(slotKey)) {
            occupiedSlots.insert(slotKey);
            newPositions.insert(state, gridCellToPoint(originalCell.x(), originalCell.y(), layoutLeft, layoutTop, strideX, strideY));
        } else {
            displacedStates.append(state);
        }
    }

    auto nearestAvailableCell = [&](const QPoint& preferredCell) {
        QVector<QPair<int, QPoint>> candidates;
        const int maxRadius = qMax(maxCols, maxRows);
        for (int radius = 0; radius <= maxRadius; ++radius) {
            candidates.clear();
            const int minX = qMax(0, preferredCell.x() - radius);
            const int maxX = qMin(maxCols - 1, preferredCell.x() + radius);
            const int minY = qMax(0, preferredCell.y() - radius);
            const int maxY = qMin(maxRows - 1, preferredCell.y() + radius);

            for (int x = minX; x <= maxX; ++x) {
                for (int y = minY; y <= maxY; ++y) {
                    if (qMax(qAbs(x - preferredCell.x()), qAbs(y - preferredCell.y())) != radius) {
                        continue;
                    }

                    const quint64 slotKey = packGridCell(x, y);
                    if (occupiedSlots.contains(slotKey)) {
                        continue;
                    }

                    const int dx = x - preferredCell.x();
                    const int dy = y - preferredCell.y();
                    const int distanceScore = dx * dx + dy * dy;
                    candidates.append(qMakePair(distanceScore, QPoint(x, y)));
                }
            }

            if (!candidates.isEmpty()) {
                std::sort(candidates.begin(), candidates.end(), [&](const QPair<int, QPoint>& a, const QPair<int, QPoint>& b) {
                    if (a.first != b.first) {
                        return a.first < b.first;
                    }

                    const int indexA = a.second.x() * maxRows + a.second.y();
                    const int indexB = b.second.x() * maxRows + b.second.y();
                    return indexA < indexB;
                });
                return candidates.first().second;
            }
        }

        return QPoint(-1, -1);
    };

    for (DesktopRootItemState* state : displacedStates) {
        if (!state) {
            continue;
        }

        const QPoint preferredCell = normalizedCellForPos(state->model.position);
        const QPoint assignedCell = nearestAvailableCell(preferredCell);
        if (assignedCell.x() < 0 || assignedCell.y() < 0) {
            return false;
        }

        occupiedSlots.insert(packGridCell(assignedCell.x(), assignedCell.y()));
        newPositions.insert(state, gridCellToPoint(assignedCell.x(), assignedCell.y(), layoutLeft, layoutTop, strideX, strideY));
    }

    bool changed = false;
    for (DesktopRootItemState* state : orderedStates) {
        if (!state || !newPositions.contains(state)) {
            continue;
        }

        const QPoint newPos = newPositions.value(state);
        if (state->model.position == newPos) {
            continue;
        }

        state->model.position = newPos;
        markDesktopRenderDataDirty(state);
        changed = true;
    }

    if (!changed) {
        return false;
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    requestSaveBoxes();
    return true;
}

void ListView::sortAutoArrangeStates(QVector<DesktopRootItemState*>& states, SortOrderType order) const
{
    const SortOrderType effectiveOrder = (order == SortOrderType::Default)
        ? SortOrderType::SortByCategorized
        : order;

    auto specialPriority = [](const DesktopRootItemState* state) {
        if (!state || state->model.objectType != ObjectType::SpecialFolder) {
            return 100;
        }
        const QString clsid = state->model.clsid;
        if (clsid == APP_USER_FOLDER_CLS) return 1;
        if (clsid == APP_MY_COMPUTER_CLS) return 2;
        if (clsid == APP_NETWORK_CLS) return 3;
        if (clsid == APP_RECYCLE_BIN_CLS) return 4;
        if (clsid == APP_CONTROL_PANEL_CLS) return 5;
        return 6;
    };

    auto categorizedPriorityForState = [](const DesktopRootItemState* state) {
        if (!state) return 999;
        if (state->model.objectType == ObjectType::SpecialFolder) return 0;
        if (state->model.objectType == ObjectType::DiskLetter || state->model.fileInfo.isDir()) return 1;
        const QString ext = state->model.fileInfo.suffix().toLower();
        if (ext == "exe" || ext == "lnk" || ext == "url") return 2;
        static const QStringList docExts = {"txt", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "md", "zip", "rar", "7z"};
        if (docExts.contains(ext)) return 3;
        return 4;
    };

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setIgnorePunctuation(true);
    collator.setLocale(QLocale::system());

    switch (effectiveOrder) {
    case SortOrderType::SortByCustom:
        std::stable_sort(states.begin(), states.end(), [](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int orderA = a ? a->model.customSortOrder : -1;
            const int orderB = b ? b->model.customSortOrder : -1;
            const int safeOrderA = orderA < 0 ? INT_MAX : orderA;
            const int safeOrderB = orderB < 0 ? INT_MAX : orderB;
            if (safeOrderA != safeOrderB) {
                return safeOrderA < safeOrderB;
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    case SortOrderType::SortByName_ASC:
    case SortOrderType::SortByName_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByName_ASC);
        std::sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int prioA = specialPriority(a);
            const int prioB = specialPriority(b);
            if (prioA != prioB) {
                return ascending ? (prioA < prioB) : (prioA > prioB);
            }
            const int result = collator.compare(a->model.sortName, b->model.sortName);
            return ascending ? (result < 0) : (result > 0);
        });
        break;
    }
    case SortOrderType::SortBySize_ASC:
    case SortOrderType::SortBySize_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortBySize_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const bool aIsDir = a->model.fileInfo.isDir();
            const bool bIsDir = b->model.fileInfo.isDir();
            if (aIsDir != bIsDir) {
                return ascending ? aIsDir : !aIsDir;
            }
            const qint64 sizeA = a->model.fileInfo.size();
            const qint64 sizeB = b->model.fileInfo.size();
            if (sizeA != sizeB) {
                return ascending ? (sizeA < sizeB) : (sizeA > sizeB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByType_ASC:
    case SortOrderType::SortByType_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByType_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const bool aIsDirLike = (a->model.objectType == ObjectType::DiskLetter) || a->model.fileInfo.isDir();
            const bool bIsDirLike = (b->model.objectType == ObjectType::DiskLetter) || b->model.fileInfo.isDir();
            const QString extA = aIsDirLike ? QString() : a->model.fileInfo.suffix().toLower();
            const QString extB = bIsDirLike ? QString() : b->model.fileInfo.suffix().toLower();
            if (extA != extB) {
                return ascending ? (extA < extB) : (extA > extB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByDateModified_ASC:
    case SortOrderType::SortByDateModified_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByDateModified_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const QDateTime dateA = a->model.fileInfo.lastModified();
            const QDateTime dateB = b->model.fileInfo.lastModified();
            if (dateA != dateB) {
                return ascending ? (dateA < dateB) : (dateA > dateB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByCategorized:
    default:
        std::sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int pA = categorizedPriorityForState(a);
            const int pB = categorizedPriorityForState(b);
            if (pA != pB) {
                return pA < pB;
            }
            return collator.compare(a->model.sortName, b->model.sortName) < 0;
        });
        break;
    }
}

void ListView::assignCustomSortOrders(const QVector<DesktopRootItemState*>& orderedStates)
{
    int order = 0;
    for (DesktopRootItemState* state : orderedStates) {
        if (!state || !state->model.boxId.isEmpty()) {
            continue;
        }
        state->model.customSortOrder = order++;
    }
}

void ListView::clearDesktopCustomSortOrders()
{
    const QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    for (DesktopRootItemState* state : states) {
        if (!state || !state->model.boxId.isEmpty()) {
            continue;
        }
        state->model.customSortOrder = -1;
    }
}

int ListView::desktopCustomInsertionIndexForPos(const QPoint& pos, const QStringList& draggedPaths) const
{
    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    for (int i = 0; i < orderedStates.size(); ++i) {
        const QRect itemRect = orderedStates[i]->model.renderData.itemRect;
        if (itemRect.contains(pos)) {
            return pos.y() <= itemRect.center().y() ? i : i + 1;
        }
    }

    int bestIndex = 0;
    qint64 bestScore = LLONG_MAX;
    for (int i = 0; i <= orderedStates.size(); ++i) {
        const QRect rect = desktopAutoArrangeInsertionIndicatorRectForIndex(i, orderedStates);
        if (rect.isNull()) {
            continue;
        }

        const int dx = pos.x() < rect.left()
            ? rect.left() - pos.x()
            : (pos.x() > rect.right() ? pos.x() - rect.right() : 0);
        const int dy = pos.y() < rect.top()
            ? rect.top() - pos.y()
            : (pos.y() > rect.bottom() ? pos.y() - rect.bottom() : 0);
        const qint64 score = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (score < bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    return qBound(0, bestIndex, orderedStates.size());
}

void ListView::updateDesktopAutoArrangeReorderPreview(const QPoint& pos, const QStringList& draggedPaths)
{
    const int nextIndex = desktopCustomInsertionIndexForPos(pos, draggedPaths);
    QVector<DesktopRootItemState*> allOrderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(allOrderedStates, SortOrderType::SortByCustom);
    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    QRect nextIndicatorRect;
    const int lineHeight = 2;
    const int horizontalInset = 8;
    const int indicatorWidth = qMax(20, m_iconWidth - horizontalInset * 2);
    for (DesktopRootItemState* draggedState : allOrderedStates) {
        if (!draggedState || !draggedPathSet.contains(normalizeDesktopItemPath(draggedState->model.filePath))) {
            continue;
        }

        const QRect draggedRect = draggedState->model.renderData.itemRect;
        const QRect draggedSlotRect = draggedRect.adjusted(0, -qMax(2, m_rowSpacing / 2), 0, qMax(2, m_rowSpacing / 2));
        if (!draggedSlotRect.contains(pos)) {
            continue;
        }

        const int draggedIndex = allOrderedStates.indexOf(draggedState);
        if (draggedIndex < 0) {
            continue;
        }

        const bool insertBeforeItem = pos.y() <= draggedRect.center().y();
        int y = insertBeforeItem ? draggedRect.top() : draggedRect.bottom();
        int left = draggedRect.left() + horizontalInset;
        int width = indicatorWidth;
        if (insertBeforeItem && draggedIndex > 0 && allOrderedStates[draggedIndex - 1]) {
            const QRect prevRect = allOrderedStates[draggedIndex - 1]->model.renderData.itemRect;
            y = (prevRect.bottom() + draggedRect.top()) / 2;
            y = qMin(y, draggedRect.top() - lineHeight);
            left = qMin(prevRect.left(), draggedRect.left()) + horizontalInset;
            const int right = qMax(prevRect.right(), draggedRect.right()) - horizontalInset;
            width = qMax(20, right - left);
        } else if (!insertBeforeItem && draggedIndex + 1 < allOrderedStates.size() && allOrderedStates[draggedIndex + 1]) {
            const QRect nextRect = allOrderedStates[draggedIndex + 1]->model.renderData.itemRect;
            y = (draggedRect.bottom() + nextRect.top()) / 2;
            y = qMax(y, draggedRect.bottom() + lineHeight);
            left = qMin(draggedRect.left(), nextRect.left()) + horizontalInset;
            const int right = qMax(draggedRect.right(), nextRect.right()) - horizontalInset;
            width = qMax(20, right - left);
        }

        nextIndicatorRect = QRect(left, y - lineHeight / 2, width, lineHeight);
        break;
    }

    for (int i = 0; i < allOrderedStates.size(); ++i) {
        if (!nextIndicatorRect.isNull()) {
            break;
        }
        DesktopRootItemState* hoverState = allOrderedStates[i];
        if (!hoverState || !hoverState->model.renderData.itemRect.contains(pos)) {
            continue;
        }

        const QRect hoverRect = hoverState->model.renderData.itemRect;
        const bool insertBeforeItem = pos.y() <= hoverRect.center().y();
        if (insertBeforeItem) {
            if (i > 0 && allOrderedStates[i - 1]) {
                const QRect prevRect = allOrderedStates[i - 1]->model.renderData.itemRect;
                const int y = (prevRect.bottom() + hoverRect.top()) / 2;
                const int left = qMin(prevRect.left(), hoverRect.left()) + horizontalInset;
                const int right = qMax(prevRect.right(), hoverRect.right()) - horizontalInset;
                nextIndicatorRect = QRect(left,
                    y - lineHeight / 2,
                    qMax(20, right - left),
                    lineHeight);
            } else {
                nextIndicatorRect = QRect(hoverRect.left() + horizontalInset,
                    hoverRect.top() - lineHeight / 2,
                    indicatorWidth,
                    lineHeight);
            }
        } else {
            if (i + 1 < allOrderedStates.size() && allOrderedStates[i + 1]) {
                const QRect nextRect = allOrderedStates[i + 1]->model.renderData.itemRect;
                const int y = (hoverRect.bottom() + nextRect.top()) / 2;
                const int left = qMin(hoverRect.left(), nextRect.left()) + horizontalInset;
                const int right = qMax(hoverRect.right(), nextRect.right()) - horizontalInset;
                nextIndicatorRect = QRect(left,
                    y - lineHeight / 2,
                    qMax(20, right - left),
                    lineHeight);
            } else {
                nextIndicatorRect = QRect(hoverRect.left() + horizontalInset,
                    hoverRect.bottom() - lineHeight / 2,
                    indicatorWidth,
                    lineHeight);
            }
        }
        break;
    }
    if (nextIndicatorRect.isNull()) {
        nextIndicatorRect = desktopAutoArrangeInsertionIndicatorRectForIndex(nextIndex, orderedStates);
    }
    for (DesktopRootItemState* draggedState : allOrderedStates) {
        if (!draggedState || !draggedPathSet.contains(normalizeDesktopItemPath(draggedState->model.filePath)) || nextIndicatorRect.isNull()) {
            continue;
        }

        const QRect draggedRect = draggedState->model.renderData.itemRect;
        if (!draggedRect.intersects(nextIndicatorRect) && !draggedRect.contains(nextIndicatorRect.center())) {
            continue;
        }

        const int y = pos.y() <= draggedRect.center().y()
            ? draggedRect.top() - lineHeight
            : draggedRect.bottom() + lineHeight;
        nextIndicatorRect.moveTop(y - lineHeight / 2);
        break;
    }

    const bool sameDraggedItems = (m_desktopReorderDraggedPaths == draggedPaths);
    if (m_desktopReorderInsertIndex == nextIndex &&
        sameDraggedItems &&
        m_desktopReorderIndicatorRect == nextIndicatorRect) {
        return;
    }

    m_desktopReorderInsertIndex = nextIndex;
    m_desktopReorderDraggedPaths = draggedPaths;
    m_desktopReorderIndicatorRect = nextIndicatorRect;
    update();
}

void ListView::clearDesktopAutoArrangeReorderPreview()
{
    if (m_desktopReorderInsertIndex < 0 &&
        m_desktopReorderDraggedPaths.isEmpty() &&
        m_desktopReorderIndicatorRect.isNull()) {
        return;
    }

    m_desktopReorderInsertIndex = -1;
    m_desktopReorderDraggedPaths.clear();
    m_desktopReorderIndicatorRect = QRect();
    update();
}

QRect ListView::desktopAutoArrangeInsertionIndicatorRect() const
{
    if (!m_iconShowRule.autoArrange ||
        m_iconShowRule.sortType != SortOrderType::SortByCustom ||
        m_desktopReorderInsertIndex < 0) {
        return QRect();
    }

    if (!m_desktopReorderIndicatorRect.isNull()) {
        return m_desktopReorderIndicatorRect;
    }

    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : m_desktopReorderDraggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    return desktopAutoArrangeInsertionIndicatorRectForIndex(m_desktopReorderInsertIndex, orderedStates);
}

QRect ListView::desktopAutoArrangeInsertionIndicatorRectForIndex(int insertIndex, const QVector<DesktopRootItemState*>& orderedStates) const
{
    insertIndex = qBound(0, insertIndex, orderedStates.size());
    const int lineHeight = 2;
    const int horizontalInset = 8;
    const int indicatorWidth = qMax(20, m_iconWidth - horizontalInset * 2);

    if (orderedStates.isEmpty()) {
        return QRect(m_marginLeft + horizontalInset, m_marginTop - lineHeight / 2, indicatorWidth, lineHeight);
    }

    if (insertIndex <= 0) {
        const QRect nextRect = orderedStates.first()->model.renderData.itemRect;
        const int y = qMax(0, nextRect.top());
        return QRect(nextRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
    }

    if (insertIndex >= orderedStates.size()) {
        const QRect prevRect = orderedStates.last()->model.renderData.itemRect;
        const int y = qMin(height() - lineHeight, prevRect.bottom());
        return QRect(prevRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
    }

    const QRect prevRect = orderedStates[insertIndex - 1]->model.renderData.itemRect;
    const QRect nextRect = orderedStates[insertIndex]->model.renderData.itemRect;
    const int strideX = m_iconWidth + m_colSpacing;
    const bool sameColumn = qAbs(prevRect.center().x() - nextRect.center().x()) <= qMax(1, strideX / 2);

    if (sameColumn) {
        const int y = (prevRect.bottom() + nextRect.top()) / 2;
        const int left = qMin(prevRect.left(), nextRect.left()) + horizontalInset;
        const int right = qMax(prevRect.right(), nextRect.right()) - horizontalInset;
        return QRect(left, y - lineHeight / 2, qMax(20, right - left), lineHeight);
    }

    const int y = qMax(0, nextRect.top());
    return QRect(nextRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
}
QVector<QPoint> ListView::layoutAutoArrangeItems(const QVector<DesktopRootItemState*>& states) const
{
    QVector<QPoint> positions;
    positions.reserve(states.size());

    const int itemWidth = m_iconWidth;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = itemWidth + m_colSpacing;
    const int strideY = itemHeight + m_rowSpacing;
    int iconX = desktopLayoutLeft();
    int iconY = desktopLayoutTop();
    const int usableBottom = desktopUsableBottom();
    const int maxRows = maxGridRowsForBounds(desktopLayoutTop(), usableBottom, itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(desktopLayoutLeft(), desktopUsableRight(), itemWidth, strideX);
    const int visibleCapacity = qMax(1, maxRows * maxCols);
    QSet<quint64> occupiedSlots = desktopBlockedOccupiedCells();
    int searchIndex = 0;
    logDesktopLayoutMetrics("AutoArrangeModel",
                            this,
                            desktopLayoutLeft(),
                            desktopLayoutTop(),
                            desktopUsableRight(),
                            usableBottom,
                            itemWidth,
                            itemHeight,
                            strideX,
                            strideY);
    for (DesktopRootItemState* state : states) {
        if (!state) {
            positions.push_back(QPoint());
            continue;
        }

        QPoint assignedCell(-1, -1);
        for (int offset = 0; offset < visibleCapacity; ++offset) {
            const QPoint candidateCell = gridIndexToCell((searchIndex + offset) % visibleCapacity, maxRows);
            const quint64 slotKey = packGridCell(candidateCell.x(), candidateCell.y());
            if (!occupiedSlots.contains(slotKey)) {
                assignedCell = candidateCell;
                occupiedSlots.insert(slotKey);
                searchIndex = (searchIndex + offset + 1) % visibleCapacity;
                break;
            }
        }

        if (assignedCell.x() < 0) {
            assignedCell = gridIndexToCell(searchIndex % visibleCapacity, maxRows);
            searchIndex = (searchIndex + 1) % visibleCapacity;
        }
        iconX = desktopLayoutLeft() + assignedCell.x() * strideX;
        iconY = desktopLayoutTop() + assignedCell.y() * strideY;
        positions.push_back(QPoint(iconX, iconY));
    }

    return positions;
}

void ListView::finalizeAutoArrangeLayout(const QVector<DesktopRootItemState*>& states,
                                         const QVector<QPoint>& positions,
                                         bool persist,
                                         bool refreshSelection)
{
    if (states.size() != positions.size()) {
        return;
    }

    QScopedPointer<ListViewSelectionSnapshot> selectionSnapshot;
    if (refreshSelection) {
        selectionSnapshot.reset(new ListViewSelectionSnapshot(ListViewSelectionSnapshot::capture(*this)));
    }

    for (int i = 0; i < states.size(); ++i) {
        DesktopRootItemState* state = states[i];
        if (!state) {
            continue;
        }

        const QPoint& pos = positions[i];
        state->model.position = pos;
        state->model.baseSize = QSize(m_iconWidth, m_iconHeight);
    }

    markAllDesktopRenderDataDirty();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    if (persist) {
        requestSaveBoxes();
    }
    if (selectionSnapshot) {
        selectionSnapshot->restore(*this, false);
    }
}

void ListView::applyAutoArrangeLayout(bool persist, bool refreshSelection)
{
    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, m_iconShowRule.sortType);
    if (m_iconShowRule.sortType == SortOrderType::SortByCustom) {
        assignCustomSortOrders(states);
    }
    const QVector<QPoint> positions = layoutAutoArrangeItems(states);
    finalizeAutoArrangeLayout(states, positions, persist, refreshSelection);
}

void ListView::applyAutoArrangeAfterDesktopMutation(bool persist, bool refreshSelection)
{
    if (!m_iconShowRule.autoArrange) {
        return;
    }

    applyAutoArrangeLayout(persist, refreshSelection);
}

bool ListView::fileHasHiddenAttribute(const QString& filePath) const
{
    if (filePath.isEmpty() || filePath.startsWith("::")) {
        return false;
    }

    const std::wstring nativePath = QDir::toNativeSeparators(filePath).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN);
}

void ListView::applyWindowsHiddenVisualState(DesktopRootItemModel& model)
{
    const bool isSpecial = model.itemData.type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(model.itemData.getCLSID()) ||
        model.filePath.startsWith("::");
    const bool shouldFade = !isSpecial && m_showHiddenFiles && fileHasHiddenAttribute(model.filePath);
    model.isHiddenFileVisual = shouldFade;
}

bool ListView::shouldShowFileSystemItem(const DesktopRootItemModel& model) const
{
    if (model.objectType == ObjectType::DiskLetter) {
        return false;
    }

    const bool isSpecial = model.itemData.type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(model.itemData.getCLSID()) ||
        model.filePath.startsWith("::");
    if (isSpecial) {
        return true;
    }

    return m_showHiddenFiles || !fileHasHiddenAttribute(model.filePath);
}

void ListView::refreshVisibilityForWindowsConfig()
{
    const QVector<QPointer<LinBox>> boxes = BoxManager::instance()->allBoxes();
    for (const QPointer<LinBox>& box : boxes) {
        if (box && box->isFolderMapped()) {
            reloadMappedFolderBox(box);
        }
    }

    onMenuRefresh();
}

void ListView::syncBoxVisibilityWithDesktopIcons()
{
    const QVector<QPointer<LinBox>> boxes = BoxManager::instance()->allBoxes();
    for (const QPointer<LinBox>& box : boxes) {
        if (!box) {
            continue;
        }

        if (m_iconShowRule.showDesktopIcons) {
            if (!box->isVisible()) {
                box->show();
            }
            box->raise();
        } else {
            box->hide();
        }
    }
}

void ListView::loadAutoLayout()
{
    applyAutoArrangeLayout(false, false);
}

void ListView::loadGridLayout()
{
    {
        const int strideX = m_iconWidth + m_colSpacing;
        const int itemHeight = desktopItemFootprintHeight();
        const int strideY = itemHeight + m_rowSpacing;
        const int layoutLeft = desktopLayoutLeft();
        const int layoutTop = desktopLayoutTop();
        const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
        const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);
        const int visibleCapacity = qMax(1, maxCols * maxRows);
        QSet<quint64> occupiedSlots;
        occupiedSlots.unite(desktopBlockedOccupiedCells());
        int overflowIndex = 0;

        for (auto& state : m_itemStates) {
            if (!shouldPaintDesktopModelState(state)) {
                continue;
            }

            state.model.baseSize = QSize(m_iconWidth, m_iconHeight);
            state.model.effectiveSize = state.model.baseSize;

            QPoint p = state.model.position;
            if (p == QPoint(0, 0)) {
                p = generateItemPosition();
                state.model.position = p;
            }

            if (m_iconShowRule.gridAlignment) {
                const int requestedIndex = pointToGridLinearIndex(p, layoutLeft, layoutTop, strideX, strideY, maxRows);
                int gridX = 0;
                int gridY = 0;

                if (occupiedSlots.size() < visibleCapacity) {
                    for (int step = 0; step < visibleCapacity; ++step) {
                        const int linearIndex = (requestedIndex + step) % visibleCapacity;
                        const QPoint candidateCell = gridIndexToCell(linearIndex, maxRows);
                        const quint64 slotKey = packGridCell(candidateCell.x(), candidateCell.y());
                        if (!occupiedSlots.contains(slotKey)) {
                            gridX = candidateCell.x();
                            gridY = candidateCell.y();
                            occupiedSlots.insert(slotKey);
                            break;
                        }
                    }
                } else {
                    const QPoint overflowCell = gridIndexToCell(overflowIndex % visibleCapacity, maxRows);
                    gridX = overflowCell.x();
                    gridY = overflowCell.y();
                    ++overflowIndex;
                }

                state.model.position = gridCellToPoint(gridX, gridY, layoutLeft, layoutTop, strideX, strideY);
            }
        }

        markAllDesktopRenderDataDirty();
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
        return;
    }
}
ListView::SortOrderType ListView::getOppositeDirection() {

    switch (m_iconShowRule.sortType) {
    case SortOrderType::SortByName_ASC:
        return SortOrderType::SortByName_DESC;
    case SortOrderType::SortByName_DESC:
        return SortOrderType::SortByName_ASC;
    case SortOrderType::SortBySize_ASC:
        return SortOrderType::SortBySize_DESC;
    case SortOrderType::SortBySize_DESC:
        return SortOrderType::SortBySize_ASC;
    case SortOrderType::SortByType_ASC:
        return SortOrderType::SortByType_DESC;
    case SortOrderType::SortByType_DESC:
        return SortOrderType::SortByType_ASC;
    case SortOrderType::SortByDateModified_ASC:
        return SortOrderType::SortByDateModified_DESC;
    case SortOrderType::SortByDateModified_DESC:
        return SortOrderType::SortByDateModified_ASC;
    case SortOrderType::SortByCategorized:
        return SortOrderType::SortByCategorized;
    case SortOrderType::SortByCustom:
        return SortOrderType::SortByCustom;
    case SortOrderType::Default:
        return SortOrderType::SortByName_ASC; // Or stay at Default
    default:
        return SortOrderType::SortByName_ASC;
    }
}
void ListView::sortItems(SortOrderType order)
{
    {
        if (m_iconShowRule.sortType == order) {
            order = getOppositeDirection();
        }

        performSort(order);
        return;
    }

}

void ListView::performSort(SortOrderType order)
{
    switch (order) {
    case SortOrderType::Default:
        // Use Categorized sort if auto-arrange is on, otherwise do nothing
        if (m_iconShowRule.autoArrange) {
            sortByCategorized();
        }
        break;
    case SortOrderType::SortByName_ASC:
        sortByName(true);
        break;
    case SortOrderType::SortByName_DESC:
        sortByName(false);
        break;
    case SortOrderType::SortBySize_ASC:
        sortBySize(true);
        break;
    case SortOrderType::SortBySize_DESC:
        sortBySize(false);
        break;
    case SortOrderType::SortByType_ASC:
        sortByType(true);
        break;
    case SortOrderType::SortByType_DESC:
        sortByType(false);
        break;
    case SortOrderType::SortByDateModified_ASC:
        sortByDate(true);
        break;
    case SortOrderType::SortByDateModified_DESC:
        sortByDate(false);
        break;
    case SortOrderType::SortByCategorized:
        sortByCategorized();
        break;
    case SortOrderType::SortByCustom:
        sortByCustom();
        break;
    default:
        // Do nothing for unknown states
        break;
    }
    m_iconShowRule.sortType = order;
    applyAutoArrangeAfterDesktopMutation(false, false);
}

void ListView::sortByCategorized()
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByCategorized);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByCustom()
{
    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, SortOrderType::SortByCustom);
    assignCustomSortOrders(states);
}

void ListView::sortByName(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByName_ASC : SortOrderType::SortByName_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortBySize(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortBySize_ASC : SortOrderType::SortBySize_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByType(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByType_ASC : SortOrderType::SortByType_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByDate(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByDateModified_ASC : SortOrderType::SortByDateModified_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

bool ListView::addItem(const QString& filePath, bool refresh)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    const bool added = !findAnyItemStateByPath(normalizedPath) && addDesktopModelItemFromPath(normalizedPath);
    
    // 闂佸湱顣介崑鎾绘煛閸繍妲归柡瀣暞缁傛帡宕奸弴鐐插箣闂佸憡姊绘慨鎾矗?m_items 闂?m_allItems
    if (refresh) {
        requestSaveBoxes();
    }
    return added;
}

bool ListView::addItemNoDelete(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty() || findDesktopStateByPath(normalizedPath)) {
        return false;
    }

    return addDesktopModelItemFromPath(normalizedPath);
}

void ListView::removeItem(const QString& filePath)
{
    removeItemPathsBatch({ filePath });
}

void ListView::removeItemNoDelete(const QString& filePath)
{
    setItemPathsTransientHidden({ filePath }, true);
}

void ListView::removeItemsBatch(const QStringList& paths, bool refresh)
{
    removeItemPathsBatch(paths, refresh);
}

void ListView::setItemsTransientHidden(const QStringList& paths, bool hidden)
{
    setItemPathsTransientHidden(paths, hidden);
}

void ListView::trackPendingExternalRemovals(const QStringList& paths)
{
    trackPendingExternalRemovalPaths(paths);
}

bool ListView::consumePendingExternalRemoval(const QString& path)
{
    const QString normalizedPath = normalizeDesktopPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }
    return m_pendingExternalRemovalPaths.remove(normalizedPath);
}

DesktopRootItemState* ListView::findItemByPath(const QString& filePath)
{
    return findAnyItemStateByPath(filePath);
}

const DesktopRootItemState* ListView::findItemByPath(const QString& filePath) const
{
    return findAnyItemStateByPath(filePath);
}

QPoint ListView::resolveCreatePosition(const QPoint& requestedPos) const
{
    QPoint pos = requestedPos;
    if (pos == QPoint(-1, -1)) {
        pos = const_cast<ListView*>(this)->generateItemPosition();
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int usableRight = desktopUsableRight();
    const int usableBottom = desktopUsableBottom();

    if (m_iconShowRule.gridAlignment) {
        const int strideX = m_iconWidth + m_colSpacing;
        const int strideY = itemHeight + m_rowSpacing;
        const int maxRows = maxGridRowsForBounds(layoutTop, usableBottom, itemHeight, strideY);
        const int maxCols = maxGridColsForBounds(layoutLeft, usableRight, m_iconWidth, strideX);
        Q_UNUSED(maxRows);
        Q_UNUSED(maxCols);

        QSet<quint64> occupiedSlots;
        occupiedSlots.unite(desktopBlockedOccupiedCells());
        for (const auto& state : m_itemStates) {
            if (!state.model.boxId.isEmpty() || !state.model.visible) continue;
            const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
        for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
            const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }

        return resolveNearestGridCreatePosition(pos, occupiedSlots);
    }

    Q_UNUSED(usableRight);
    Q_UNUSED(usableBottom);
    return resolveNearestFreeCreatePosition(pos);
}

void ListView::reserveCreatedItemPosition(const QString& filePath, const QPoint& requestedPos)
{
    if (m_iconShowRule.autoArrange) {
        return;
    }

    const QString normalizedPath = normalizeDesktopPath(filePath);
    if (normalizedPath.isEmpty()) return;
    m_pendingCreatedItemPositions[normalizedPath] = resolveCreatePosition(requestedPos);
}

bool ListView::takeReservedCreatedItemPosition(const QString& filePath, QPoint& posOut)
{
    const QString normalizedPath = normalizeDesktopPath(filePath);
    auto it = m_pendingCreatedItemPositions.find(normalizedPath);
    if (it == m_pendingCreatedItemPositions.end()) {
        return false;
    }

    posOut = it.value();
    m_pendingCreatedItemPositions.erase(it);
    return true;
}

bool ListView::handlePendingShellCreatedItem(const QString& newPath)
{
    if (!m_pendingShellNewTargetBox) {
        return false;
    }

    QPointer<LinBox> targetBox = m_pendingShellNewTargetBox;
    clearPendingShellNewTargetBox();
    if (!targetBox) {
        return false;
    }

    if (targetBox->isFolderMapped()) {
        const QString mappedFolderPath = targetBox->mappedFolderPath();
        if (mappedFolderPath.isEmpty() || targetBox->mappedFolderState() != LinBox::MappedFolderState::Normal) {
            return false;
        }

        const QString destinationPath = ListViewFileOpsHelper::uniquePasteDestinationPath(newPath, mappedFolderPath);
        if (!ListViewFileOpsHelper::movePathToExactDestination(newPath, destinationPath)) {
            return false;
        }

        removeDesktopStateByPath(newPath);

        loadMappedFolderItemsForBox(targetBox);
        targetBox->refreshLayout();
        targetBox->raise();
        update();
        return true;
    }

    moveItemToBox(newPath, targetBox, false, -1);
    targetBox->refreshLayout();
    targetBox->raise();
    update();
    return true;
}

/// <summary>
/// 闂佹眹鍨婚崰鎰板垂濮樿鲸鍠嗛柛鏇ㄥ亜閻?
/// 1.unGrid濠碘槅鍨埀顒€纾涵鈧?
/// UI闂佸憡甯楃粙鎴犵磽閹绘唸em,闂佺儵鏅涢悺銊ф暜閹绢喖绠甸柟閭︿簽鏉╂柨顫楄婵敻鎮ラ敐澶婄煑闁规儼濮ら弳娑㈡煕瑜庨崝鏍偉?
/// 闂傚倸鐗婇幏绋╅梺鍛婂笚缁嬫垹绱撻幘缁樻櫖閻忕偟鍘ч埢蹇涙煟閳垛晛澧祌id闂佸憡甯楃粙鎴犵磽閹捐埖鍠嗛柛鏇ㄥ亜閻?
/// 2.Grid濠碘槅鍨埀顒€纾涵鈧?
/// 闂佸搫琚崕鎾敋濡ゅ懎鍐€闁绘挸娴烽幗宥夋偨椤栨艾鏆欓柣顏嶅墮琚欓煫鍥ч叄閸ゅ鏌涢弽褎鎯堥柣鎾寸懇瀵即顢涘顓炲墾闂佸搫鐗嗛ˇ浼村几閸愨晝顩烽悹楦挎閹界娀鏌涢敂鑽ゅ帨缂佽鲸绻冩穱濠囧磼濞戞矮鍑芥繛鎴炴尭缁夋挳鎮洪妸鈺佹嵍闁靛瀵屽鍓х磼娓氬灝鐏柍瑙勭墵瀵粙鎮℃惔锝嗘啢闂佹寧绋戞總鏃€绻涢崶顒佸仺闁靛ě灞藉壋婵炴垶鎸撮崑鎾斥槈閹垮啩绨奸柣妤€鎽滈埀顒佺⊕閸旀洖顭囧Δ鍛唨?
/// </summary>
/// <returns></returns>
QPoint ListView::generateItemPosition()
{
    qDebug() << "[generateItemPosition] gridAlignment=" << m_iconShowRule.gridAlignment 
             << "m_lastRightClickPos=" << m_lastRightClickPos;
    
    // 闂傚倸鐗婇崙鐥砳d闁汇埄鍨伴崯顐︽儑椤掍胶鈻旈柡鍐ㄥ€归弳鐕疘闂佺懓鐏濈粔宕囩礊閺冨倹鍠嗛柨鏇楀亾鐟滄澘鍊块幆鍐礋閸撲胶顦╅梺鍛婄懄婵炲﹪寮銏″殨婵犲﹤鍟粈瀣煛閸屾稒绶茬紓鍌涙崌閺佸秵寰勫☉姘鳖槷闂佺儵鏅涢悺銊ф暜鐎涙ɑ濯撮悹鎭掑妽閺嗗繐顫楄婵敻鎮ラ敐澶婄煑闁规儼濮ら弳娑樏归敐鍛ら柣?
    if (!m_iconShowRule.gridAlignment && m_lastRightClickPos != QPoint(-1, -1)) {
        QPoint pos = m_lastRightClickPos;
        qDebug() << "[generateItemPosition] Using right-click position:" << pos;
        // 闂備焦褰冪粔鍫曟偪?m_lastRightClickPos闂佹寧绋戦惌鍌毼ｇ拠宸桨闁靛繈鍊栨慨娣疘闂佸憡甯楃粙鎴犵磽閹剧粯鍎嶉柛鏇ㄥ墯閻庮喖霉閻樿櫕鏋勭紒鈥冲暞閹峰懐鎹勯妸锔芥闁荤姴娲㈤崕鍗烆焽濡ゅ懎鍐€?
        m_lastRightClickPos = QPoint(-1, -1);
        return resolveCreatePosition(pos);
    }
    
    qDebug() << "[generateItemPosition] Using grid layout position";

    const int strideX = m_iconWidth + m_colSpacing;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideY = itemHeight + m_rowSpacing;
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);
    const int visibleCapacity = qMax(1, maxCols * maxRows);
    QSet<quint64> occupiedSlots;
    int desktopItemCount = 0;
    occupiedSlots.unite(desktopBlockedOccupiedCells());

    for (const auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty() || !state.model.visible) {
            continue;
        }
        if (state.model.position.x() > 0 || state.model.position.y() > 0) {
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
    }
    for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
        ++desktopItemCount;
        const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
        occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    if (occupiedSlots.size() < visibleCapacity) {
        for (int gridX = 0; gridX < maxCols; ++gridX) {
            for (int gridY = 0; gridY < maxRows; ++gridY) {
                if (occupiedSlots.contains(packGridCell(gridX, gridY))) {
                    continue;
                }

                return gridCellToPoint(gridX, gridY, layoutLeft, layoutTop, strideX, strideY);
            }
        }
    }

    const QPoint overflowCell = gridIndexToCell(desktopItemCount % visibleCapacity, maxRows);
    return gridCellToPoint(overflowCell.x(), overflowCell.y(), layoutLeft, layoutTop, strideX, strideY);

    return QPoint(0, 0); // 闂佺绻戠划宀€鑺遍搹顐ｄ氦闁哄倹瀵х粈鈧?
}


void ListView::initRightMenuConnections() {
    RightMenu* menu = RightMenu::getInstance();

    // View sub-menu connections
    connect(menu, &RightMenu::LargeIconsTriggered, this, &ListView::onMenuLargeIcons);
    connect(menu, &RightMenu::MediumIconsTriggered, this, &ListView::onMenuMediumIcons);
    connect(menu, &RightMenu::SmallIconsTriggered, this, &ListView::onMenuSmallIcons);
    connect(menu, &RightMenu::AutoArrangeIconsTriggered, this, &ListView::onMenuAutoArrangeIcons);
    connect(menu, &RightMenu::AlignIconsToGridTriggered, this, &ListView::onMenuAlignIconsToGrid);
    connect(menu, &RightMenu::ShowDesktopIconsTriggered, this, &ListView::onMenuShowDesktopIcons);
    connect(menu, &RightMenu::DoubleClickedHideIconsTriggered, this, &ListView::onMenuDoubleClickHideIcons);

    // Sort sub-menu connections
    connect(menu, &RightMenu::sortByNameTriggered, this, &ListView::onMenuSortByName);
    connect(menu, &RightMenu::sortBySizeTriggered, this, &ListView::onMenuSortBySize);
    connect(menu, &RightMenu::sortByTypeTriggered, this, &ListView::onMenuSortByType);
    connect(menu, &RightMenu::sortByDateTriggered, this, &ListView::onMenuSortByDate);

    // Other actions
    connect(menu, &RightMenu::refreshTriggered, this, &ListView::onMenuRefresh);
    connect(menu, &RightMenu::pasteTriggered, this, &ListView::onMenuPaste);
    connect(menu, &RightMenu::pasteShortcutTriggered, this, &ListView::onMenuPasteShortcut);
    connect(menu, &RightMenu::createBoxTriggered, this, &ListView::onMenuCreateBox);
    connect(menu, &RightMenu::mapFolderTriggered, this, &ListView::onMenuMapFolder);
    connect(menu, &RightMenu::organizeDesktopTriggered, this, &ListView::onMenuOrganizeDesktopByNameSort);
    connect(menu, &RightMenu::linDeskTriggered, this, &ListView::onMenuLinDesk);
    connect(menu, &RightMenu::quitAppTriggered, this, &ListView::onMenuQuitApp);
    
    //init view sub-menu check states
    switch (m_iconShowRule.viewMode) {
    case ViewMode::LargeIcon:
        menu->setLargeIconsChecked(true);
        break;
    case ViewMode::MediumIcon:
        menu->setMediumIconsChecked(true);
        break;
    case ViewMode::SmallIcon:
        menu->setSmallIconsChecked(true);
        break;
    default:
        menu->setLargeIconsChecked(false);
        menu->setMediumIconsChecked(false);
        menu->setSmallIconsChecked(false);
        break;
    }
    
    if(m_iconShowRule.autoArrange) {
        menu->setAutoArrangeIconsChecked(true);
    } else {
        menu->setAutoArrangeIconsChecked(false);
    }
    if(m_iconShowRule.gridAlignment) {
        menu->setAlignIconsToGridChecked(true);
    } else {
        menu->setAlignIconsToGridChecked(false);
    }
    if(m_iconShowRule.showDesktopIcons) {
        menu->setShowDesktopIconsChecked(true);
    } else {
        menu->setShowDesktopIconsChecked(false);
    }
    if(m_iconShowRule.doubleClickHideIcons) {
        menu->setDoubleClickedHideIconsChecked(true);
    } else {
        menu->setDoubleClickedHideIconsChecked(false);
    }

    // init sort-by sub-menu check state
    const SortOrderType visibleSortType =
        (m_iconShowRule.autoArrange && m_iconShowRule.sortType == SortOrderType::SortByCustom)
            ? m_iconShowRule.sortTypeBeforeAutoArrange
            : m_iconShowRule.sortType;
    switch (visibleSortType) {
    case SortOrderType::SortByName_ASC:
    case SortOrderType::SortByName_DESC:
        menu->setSortByNameChecked(true);
        break;
    case SortOrderType::SortBySize_ASC:
    case SortOrderType::SortBySize_DESC:
        menu->setSortBySizeChecked(true);
        break;
    case SortOrderType::SortByType_ASC:
    case SortOrderType::SortByType_DESC:
        menu->setSortByTypeChecked(true);
        break;
    case SortOrderType::SortByDateModified_ASC:
    case SortOrderType::SortByDateModified_DESC:
        menu->setSortByDateChecked(true);
        break;
    default:
        menu->setSortByNameChecked(true); // 婵帗绋掗…鍫ヮ敇婵犳碍鐒诲璺侯槼閸橆剟鏌涘顒傂ょ悮?
        break;
    }
}

void ListView::syncViewModeSizes() {
    m_marginTop = scalePx(kBaseMarginTop);
    m_marginLeft = scalePx(kBaseMarginLeft);
    m_bottomPadding = scalePx(kBaseBottomPadding);

    switch (m_iconShowRule.viewMode) {
        case ViewMode::HugeIcon:
            m_iconWidth = scalePx(kHugeIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kLargeRowSpacing);
            m_colSpacing = scalePx(kLargeColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            break;
        case ViewMode::LargeIcon:
            m_iconWidth  = scalePx(kLargeIconW);
            m_rowSpacing = scalePx(kLargeRowSpacing);
            m_colSpacing = scalePx(kLargeColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            if (rightMenu) rightMenu->setLargeIconsChecked(true);
            break;
        case ViewMode::MediumLargeIcon:
            m_iconWidth = scalePx(kMediumLargeIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kMediumRowSpacing);
            m_colSpacing = scalePx(kMediumColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            break;
        case ViewMode::MediumIcon:
            m_iconWidth  = scalePx(kMediumIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kMediumRowSpacing);
            m_colSpacing = scalePx(kMediumColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            if (rightMenu) rightMenu->setMediumIconsChecked(true);
            break;
        case ViewMode::SmallMediumIcon:
            m_iconWidth = scalePx(kSmallMediumIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kSmallRowSpacing);
            m_colSpacing = scalePx(kSmallColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            break;
        case ViewMode::SmallIcon:
            m_iconWidth = scalePx(kSmallIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kSmallRowSpacing);
            m_colSpacing = scalePx(kSmallColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            if (rightMenu) rightMenu->setSmallIconsChecked(true);
            break;
        default:
            m_iconShowRule.viewMode = ViewMode::MediumIcon;
            m_iconWidth = scalePx(kMediumIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kMediumRowSpacing);
            m_colSpacing = scalePx(kMediumColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            if (rightMenu) rightMenu->setMediumIconsChecked(true);
            break;
    }

    markAllDesktopRenderDataDirty();
}

qreal ListView::currentDpiScale() const
{
    return DpiScaleHelper::uiScaleForWidget(this);
}

DesktopRenderParams ListView::currentRenderParams() const
{
    DesktopRenderParams params;
    params.iconWidth = m_iconWidth;
    params.iconHeight = m_iconHeight;
    params.largeIconW = scalePx(kLargeIconW);
    params.smallIconW = scalePx(kSmallIconW);
    params.iconTextGap = kDesktopIconTextGap;
    params.dpiScale = currentDpiScale();
    params.labelFont = font();
    params.showShortcutArrow = m_iconShowRule.showShortCutArrow;
    return params;
}

DesktopItemInitContext ListView::currentDesktopItemInitContext() const
{
    DesktopItemInitContext context;
    context.sysIconRegisterInfo = sysIconRegisterInfo;
    context.systemFolderCLSIDs = SystemFolderCLSIDs;
    context.showHiddenFiles = m_showHiddenFiles;
    context.getBoxId = [this](const QString& path) {
        return getBoxIdForItem(path);
    };
    return context;
}

bool ListView::addDesktopModelItemFromPath(const QString& filePath)
{
    if (filePath.isEmpty() || findDesktopStateByPath(filePath)) {
        return false;
    }

    const ListItemData data = DesktopItemLoader().loadItem(filePath);
    DesktopRootItemState state;
    initDesktopRootItemModel(state.model, data, currentDesktopItemInitContext());
    state.model.layoutMode = DesktopLayoutMode::IconMode;
    state.model.baseSize = QSize(m_iconWidth, m_iconHeight);
    state.model.effectiveSize = state.model.baseSize;

    QPoint reservedPos;
    if (takeReservedCreatedItemPosition(filePath, reservedPos)) {
        state.model.position = reservedPos;
    } else if (state.model.visible && state.model.boxId.isEmpty() && !m_iconShowRule.autoArrange) {
        state.model.position = generateItemPosition();
    }

    m_itemStates.push_back(state);
    markDesktopStateIndexDirty();
    markDesktopRenderDataDirty(&m_itemStates.back());
    updateAllItemRenderData();

    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeAfterDesktopMutation(false, false);
    } else {
        rebuildSpatialIndex();
        update();
    }
    return true;
}

bool ListView::refreshDesktopModelItemPath(const QString& oldPath, const QString& newPath)
{
    DesktopRootItemState* state = findDesktopStateByPath(oldPath);
    if (!state) {
        return false;
    }

    const QString oldNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
    const ListItemData data = DesktopItemLoader().loadItem(newPath);

    DesktopRootItemModel updatedModel;
    initDesktopRootItemModel(updatedModel, data, currentDesktopItemInitContext());
    updatedModel.layoutMode = state->model.layoutMode;
    updatedModel.position = state->model.position;
    updatedModel.baseSize = QSize(m_iconWidth, m_iconHeight);
    updatedModel.effectiveSize = state->model.effectiveSize;
    updatedModel.visibleTextHeight = state->model.visibleTextHeight;
    updatedModel.isSelected = state->model.isSelected;
    updatedModel.isHovered = state->model.isHovered;
    updatedModel.focused = state->model.focused;
    updatedModel.isCutMarked = state->model.isCutMarked;
    updatedModel.isFullNameExpanded = state->model.isFullNameExpanded;
    updatedModel.customSortOrder = state->model.customSortOrder;
    updatedModel.expandedPreviewText = state->model.expandedPreviewText;
    updatedModel.isRenaming = state->model.isRenaming;
    updatedModel.isReady = true;

    state->model = updatedModel;
    markDesktopStateIndexDirty();
    markDesktopRenderDataDirty(state);

    const QString newNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
    for (QString& selectedPath : m_selectedDesktopPaths) {
        if (selectedPath == oldNormalizedPath) {
            selectedPath = newNormalizedPath;
        }
    }
    if (m_selectedDesktopPathSet.remove(oldNormalizedPath)) {
        m_selectedDesktopPathSet.insert(newNormalizedPath);
    }
    if (m_focusedDesktopPath == oldNormalizedPath) {
        m_focusedDesktopPath = newNormalizedPath;
    }
    if (m_anchorDesktopPath == oldNormalizedPath) {
        m_anchorDesktopPath = newNormalizedPath;
    }
    if (m_lastClickedDesktopPath == oldNormalizedPath) {
        m_lastClickedDesktopPath = newNormalizedPath;
    }
    if (m_draggedDesktopPath == oldNormalizedPath) {
        m_draggedDesktopPath = newNormalizedPath;
    }
    if (m_hoveredDesktopPath == oldNormalizedPath) {
        m_hoveredDesktopPath = newNormalizedPath;
    }

    updateAllItemRenderData();
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeAfterDesktopMutation(false, false);
    } else {
        rebuildSpatialIndex();
        update();
    }
    return true;
}

void ListView::invalidateFolderItemIconCache(const QString& folderPath)
{
    const QString normalizedPath = normalizeDesktopItemPath(folderPath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    const QString iconKey = QString("dir:v3:%1").arg(normalizedPath);
    IconCacheManager::instance().invalidateIcon(
        QString("%1|base|%2").arg(iconKey).arg(ListItemData::s_targetIconSize));
    IconCacheManager::instance().invalidatePixmapsByPrefix(QString("%1|render|").arg(iconKey));
}

bool ListView::refreshBoxModelItemPath(LinBox* box, const QString& oldPath, const QString& newPath)
{
    if (!box) {
        return false;
    }

    DesktopRootItemState* state = box->findItemStateByPath(oldPath);
    if (!state) {
        return false;
    }

    const QString oldNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
    const ListItemData data = DesktopItemLoader().loadItem(newPath);
    DesktopRootItemModel updatedModel;
    initDesktopRootItemModel(updatedModel, data, currentDesktopItemInitContext());
    updatedModel.layoutMode = state->model.layoutMode;
    updatedModel.position = state->model.position;
    updatedModel.baseSize = state->model.baseSize;
    updatedModel.effectiveSize = state->model.effectiveSize;
    updatedModel.visibleTextHeight = state->model.visibleTextHeight;
    updatedModel.isSelected = state->model.isSelected;
    updatedModel.isHovered = state->model.isHovered;
    updatedModel.focused = state->model.focused;
    updatedModel.isCutMarked = state->model.isCutMarked;
    updatedModel.isFullNameExpanded = state->model.isFullNameExpanded;
    updatedModel.customSortOrder = state->model.customSortOrder;
    updatedModel.expandedPreviewText = state->model.expandedPreviewText;
    updatedModel.isRenaming = state->model.isRenaming;
    updatedModel.isReady = true;
    state->model = updatedModel;
    state->renderDataDirty = true;

    const QString newNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
    for (QString& selectedPath : m_selectedBoxPaths) {
        if (selectedPath == oldNormalizedPath) {
            selectedPath = newNormalizedPath;
        }
    }
    if (m_focusedBoxPath == oldNormalizedPath) {
        m_focusedBoxPath = newNormalizedPath;
    }
    if (m_anchorBoxPath == oldNormalizedPath) {
        m_anchorBoxPath = newNormalizedPath;
    }

    box->refreshLayout();
    box->update();
    return true;
}

bool ListView::refreshFolderItemByPath(const QString& folderPath)
{
    const QString normalizedPath = normalizeDesktopItemPath(folderPath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    invalidateFolderItemIconCache(normalizedPath);

    if (refreshDesktopModelItemPath(normalizedPath, normalizedPath)) {
        return true;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
        return refreshBoxModelItemPath(box, normalizedPath, normalizedPath);
    }

    return false;
}

DesktopRootItemState* ListView::findDesktopStateAt(const QPoint& pos)
{
    for (auto it = m_itemStates.rbegin(); it != m_itemStates.rend(); ++it) {
        if (shouldPaintDesktopModelState(*it) && it->model.renderData.itemRect.contains(pos)) {
            return &(*it);
        }
    }
    return nullptr;
}

const DesktopRootItemState* ListView::findDesktopStateAt(const QPoint& pos) const
{
    for (auto it = m_itemStates.crbegin(); it != m_itemStates.crend(); ++it) {
        if (shouldPaintDesktopModelState(*it) && it->model.renderData.itemRect.contains(pos)) {
            return &(*it);
        }
    }
    return nullptr;
}

QString ListView::normalizeDesktopItemPath(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return QString();
    }

    QString normalized = QDir::toNativeSeparators(filePath);
    while (normalized.endsWith('\\')) {
        normalized.chop(1);
    }
    return normalized;
}

DesktopRootItemState* ListView::findDesktopStateByPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return nullptr;
    }

    rebuildDesktopStateIndex();
    const auto it = m_desktopStateIndex.constFind(normalizedPath);
    return it == m_desktopStateIndex.cend() ? nullptr : it.value();
}

const DesktopRootItemState* ListView::findDesktopStateByPath(const QString& filePath) const
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return nullptr;
    }

    ListView* self = const_cast<ListView*>(this);
    self->rebuildDesktopStateIndex();
    const auto it = self->m_desktopStateIndex.constFind(normalizedPath);
    return it == self->m_desktopStateIndex.cend() ? nullptr : it.value();
}

bool ListView::isDesktopPathSelected(const QString& filePath) const
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    return !normalizedPath.isEmpty() && m_selectedDesktopPathSet.contains(normalizedPath);
}

QVector<DesktopRootItemModel> ListView::selectedDesktopModels() const
{
    QVector<DesktopRootItemModel> models;
    models.reserve(m_selectedDesktopPaths.size());
    for (const QString& path : m_selectedDesktopPaths) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (state) {
            models.push_back(state->model);
        }
    }
    return models;
}

QStringList ListView::selectedDesktopPaths() const
{
    QStringList paths;
    for (const QString& path : m_selectedDesktopPaths) {
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

QStringList ListView::selectedBoxPaths() const
{
    QStringList paths;
    for (const QString& path : m_selectedBoxPaths) {
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

QString ListView::effectiveFocusedDesktopPath() const
{
    return normalizeDesktopItemPath(m_focusedDesktopPath);
}

QString ListView::effectiveAnchorDesktopPath() const
{
    return normalizeDesktopItemPath(m_anchorDesktopPath);
}

QString ListView::effectiveFocusedBoxPath() const
{
    return normalizeDesktopItemPath(m_focusedBoxPath);
}

QString ListView::effectiveAnchorBoxPath() const
{
    return normalizeDesktopItemPath(m_anchorBoxPath);
}

QStringList ListView::selectableDesktopPaths() const
{
    QStringList paths;
    paths.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            paths.append(normalizedPath);
        }
    }
    return paths;
}

QStringList ListView::selectableBoxPaths(LinBox* box) const
{
    QStringList paths;
    if (!box) {
        return paths;
    }
    const auto& states = box->itemStates();
    paths.reserve(states.size());
    for (const auto& state : states) {
        if (!state.model.visible) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            paths.append(normalizedPath);
        }
    }
    return paths;
}

QString ListView::findDirectionalDesktopFocusCandidate(const QString& focusedPath, FocusMoveDirection direction) const
{
    if (m_itemStates.isEmpty()) {
        return QString();
    }

    const DesktopRootItemState* focusedState = focusedPath.isEmpty()
        ? nullptr
        : findDesktopStateByPath(focusedPath);
    if (!focusedState) {
        const QStringList orderedPaths = selectableDesktopPaths();
        return orderedPaths.isEmpty() ? QString() : orderedPaths.first();
    }

    const QPoint currentCenter = focusedState->model.renderData.selectionContentRect.center();
    qint64 bestDistanceSquared = LLONG_MAX;
    int bestPrimaryDistance = INT_MAX;
    QString candidatePath;

    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }

        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (normalizedPath.isEmpty() || normalizedPath == normalizeDesktopItemPath(focusedPath)) {
            continue;
        }

        const QPoint candidateCenter = state.model.renderData.selectionContentRect.center();
        const int dx = candidateCenter.x() - currentCenter.x();
        const int dy = candidateCenter.y() - currentCenter.y();

        int primaryDistance = INT_MAX;
        switch (direction) {
        case FocusMoveDirection::Left:
            if (dx >= 0) continue;
            primaryDistance = -dx;
            break;
        case FocusMoveDirection::Right:
            if (dx <= 0) continue;
            primaryDistance = dx;
            break;
        case FocusMoveDirection::Up:
            if (dy >= 0) continue;
            primaryDistance = -dy;
            break;
        case FocusMoveDirection::Down:
            if (dy <= 0) continue;
            primaryDistance = dy;
            break;
        }

        const qint64 distanceSquared = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (distanceSquared < bestDistanceSquared ||
            (distanceSquared == bestDistanceSquared && primaryDistance < bestPrimaryDistance)) {
            bestDistanceSquared = distanceSquared;
            bestPrimaryDistance = primaryDistance;
            candidatePath = normalizedPath;
        }
    }

    return candidatePath;
}

QString ListView::findDirectionalBoxFocusCandidate(LinBox* box, const QString& focusedPath, FocusMoveDirection direction) const
{
    if (!box) {
        return QString();
    }

    const DesktopRootItemState* focusedState = focusedPath.isEmpty()
        ? nullptr
        : box->findItemStateByPath(focusedPath);
    if (!focusedState) {
        const QStringList orderedPaths = selectableBoxPaths(box);
        return orderedPaths.isEmpty() ? QString() : orderedPaths.first();
    }

    const QPoint currentCenter = focusedState->model.renderData.selectionContentRect.center();
    qint64 bestDistanceSquared = LLONG_MAX;
    int bestPrimaryDistance = INT_MAX;
    QString candidatePath;

    for (const auto& state : box->itemStates()) {
        if (!state.model.visible) {
            continue;
        }

        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (normalizedPath.isEmpty() || normalizedPath == normalizeDesktopItemPath(focusedPath)) {
            continue;
        }

        const QPoint candidateCenter = state.model.renderData.selectionContentRect.center();
        const int dx = candidateCenter.x() - currentCenter.x();
        const int dy = candidateCenter.y() - currentCenter.y();

        int primaryDistance = INT_MAX;
        switch (direction) {
        case FocusMoveDirection::Left:
            if (dx >= 0) continue;
            primaryDistance = -dx;
            break;
        case FocusMoveDirection::Right:
            if (dx <= 0) continue;
            primaryDistance = dx;
            break;
        case FocusMoveDirection::Up:
            if (dy >= 0) continue;
            primaryDistance = -dy;
            break;
        case FocusMoveDirection::Down:
            if (dy <= 0) continue;
            primaryDistance = dy;
            break;
        }

        const qint64 distanceSquared = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (distanceSquared < bestDistanceSquared ||
            (distanceSquared == bestDistanceSquared && primaryDistance < bestPrimaryDistance)) {
            bestDistanceSquared = distanceSquared;
            bestPrimaryDistance = primaryDistance;
            candidatePath = normalizedPath;
        }
    }

    return candidatePath;
}

void ListView::setDesktopItemSelected(DesktopRootItemState* state, const QString& normalizedPath, bool selected, QRegion* dirtyRegion)
{
    if (!state || normalizedPath.isEmpty()) {
        return;
    }

    if (selected) {
        if (!m_selectedDesktopPathSet.contains(normalizedPath)) {
            m_selectedDesktopPaths.push_back(normalizedPath);
        }
        m_selectedDesktopPathSet.insert(normalizedPath);
    } else {
        m_selectedDesktopPathSet.remove(normalizedPath);
        m_selectedDesktopPaths.removeOne(normalizedPath);
    }

    if (state->model.isSelected == selected && state->model.renderData.selected == selected) {
        return;
    }

    state->model.isSelected = selected;
    state->model.renderData.selected = selected;
    const QRect dirty = desktopRootItemHighlightDirtyRect(state->model.renderData);
    if (dirtyRegion) {
        *dirtyRegion += dirty;
        return;
    }
    update(dirty);
}

void ListView::setDesktopItemSelected(const QString& filePath, bool selected, QRegion* dirtyRegion)
{
    DesktopRootItemState* state = findDesktopStateByPath(filePath);
    if (!state) {
        return;
    }

    const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
    setDesktopItemSelected(state, normalizedPath, selected, dirtyRegion);
}

void ListView::clearDesktopSelection(QRegion* dirtyRegion)
{
    const QStringList previousSelection = selectedDesktopPaths();
    for (const QString& path : previousSelection) {
        clearItemExpandedState(path);
        setDesktopItemSelected(path, false, dirtyRegion);
    }
    m_selectedDesktopPaths.clear();
    m_selectedDesktopPathSet.clear();
}

void ListView::applyDesktopSelection(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    QRegion dirtyRegion;
    clearDesktopSelection(&dirtyRegion);

    QStringList filteredPaths;
    for (const QString& path : filePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty() || filteredPaths.contains(normalizedPath)) {
            continue;
        }
        if (!findDesktopStateByPath(normalizedPath)) {
            continue;
        }
        filteredPaths.append(normalizedPath);
        setDesktopItemSelected(normalizedPath, true, &dirtyRegion);
    }

    if (!dirtyRegion.isEmpty()) {
        update(dirtyRegion);
    }

    m_focusedDesktopPath = filteredPaths.isEmpty()
        ? QString()
        : normalizeDesktopItemPath(focusedPath.isEmpty() ? filteredPaths.last() : focusedPath);
    if (m_focusedDesktopPath.isEmpty() && !filteredPaths.isEmpty()) {
        m_focusedDesktopPath = filteredPaths.last();
    }

    if (expandSingle && filteredPaths.size() == 1) {
        setItemFullNameExpanded(filteredPaths.first(), true);
    }
}

void ListView::removeItemPathsBatch(const QStringList& paths, bool refresh)
{
    if (paths.isEmpty()) {
        return;
    }

    QSet<QString> seenPaths;
    QSet<LinBox*> sourceBoxes;
    bool removedAny = false;

    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(rawPath);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath)) {
            continue;
        }
        seenPaths.insert(normalizedPath);

        if (findDesktopStateByPath(normalizedPath)) {
            removeDesktopStateByPath(normalizedPath);
            removedAny = true;
            continue;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (box->removeItemState(normalizedPath, false)) {
                sourceBoxes.insert(box);
                removedAny = true;
            }
        }
    }

    if (!removedAny) {
        return;
    }

    for (LinBox* box : sourceBoxes) {
        if (!box) {
            continue;
        }
        box->refreshLayout();
        box->update();
    }

    if (refresh) {
        if (m_iconShowRule.autoArrange) {
            applyAutoArrangeLayout(false, false);
        } else {
            rebuildSpatialIndex();
            update();
            repaint();
        }
    }
}

void ListView::setItemPathsTransientHidden(const QStringList& paths, bool hidden)
{
    if (paths.isEmpty()) {
        return;
    }

    QSet<LinBox*> dirtyBoxes;
    bool desktopDirty = false;
    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(rawPath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            state->model.visible = !hidden;
            desktopDirty = true;
            continue;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (DesktopRootItemState* state = box->findItemStateByPath(normalizedPath)) {
                state->model.visible = !hidden;
                dirtyBoxes.insert(box);
            }
        }
    }

    for (LinBox* box : dirtyBoxes) {
        if (!box) {
            continue;
        }
        box->refreshLayout();
        box->update();
    }

    if (desktopDirty) {
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
    }
}

void ListView::trackPendingExternalRemovalPaths(const QStringList& paths)
{
    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopPath(rawPath);
        if (!normalizedPath.isEmpty()) {
            m_pendingExternalRemovalPaths.insert(normalizedPath);
        }
    }
}

void ListView::removeDesktopStateByPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    for (auto it = m_itemStates.begin(); it != m_itemStates.end(); ) {
        if (normalizeDesktopItemPath(it->model.filePath) == normalizedPath) {
            const QRect dirty = desktopRootItemHighlightDirtyRect(it->model.renderData);
            it = m_itemStates.erase(it);
            markDesktopStateIndexDirty();
            markSpatialIndexDirty();
            if (m_hoveredDesktopPath == normalizedPath) {
                m_hoveredDesktopPath.clear();
            }
            m_selectedDesktopPathSet.remove(normalizedPath);
            m_selectedDesktopPaths.removeOne(normalizedPath);
            if (m_focusedDesktopPath == normalizedPath) {
                m_focusedDesktopPath.clear();
            }
            if (m_anchorDesktopPath == normalizedPath) {
                m_anchorDesktopPath.clear();
            }
            if (m_lastClickedDesktopPath == normalizedPath) {
                m_lastClickedDesktopPath.clear();
            }
            if (m_draggedDesktopPath == normalizedPath) {
                m_draggedDesktopPath.clear();
            }
            update(dirty);
        } else {
            ++it;
        }
    }
}

bool ListView::shouldPaintDesktopModelState(const DesktopRootItemState& state) const
{
    if (!m_iconShowRule.showDesktopIcons) {
        return false;
    }
    if (!state.model.isReady) {
        return false;
    }
    if (!state.model.visible) {
        return false;
    }
    if (!state.model.boxId.isEmpty()) {
        return false;
    }
    return true;
}

DesktopRootItemState* ListView::findDesktopItemAt(const QPoint& pos)
{
    return findDesktopStateAt(pos);
}

const DesktopRootItemState* ListView::findDesktopItemAt(const QPoint& pos) const
{
    return findDesktopStateAt(pos);
}

QRect ListView::desktopSelectionRectForItem(const DesktopRootItemState& state) const
{
    return state.model.renderData.selectionContentRect;
}

void ListView::updateHoveredDesktopItem(const QPoint& pos)
{
    if (m_isSelecting) {
        return;
    }

    DesktopRootItemState* nextHoveredState = findDesktopStateAt(pos);
    const QString nextHoveredPath = nextHoveredState
        ? normalizeDesktopItemPath(nextHoveredState->model.filePath)
        : QString();
    if (m_hoveredDesktopPath == nextHoveredPath) {
        return;
    }

    auto updateHoverState = [this](const QString& filePath, bool hovered) {
        DesktopRootItemState* state = findDesktopStateByPath(filePath);
        if (!state) {
            return;
        }

        const QRect dirty = desktopRootItemHighlightDirtyRect(state->model.renderData);
        state->model.isHovered = hovered;
        state->model.renderData.hovered = hovered && !state->model.isSelected;
        update(dirty);
    };

    updateHoverState(m_hoveredDesktopPath, false);
    m_hoveredDesktopPath = nextHoveredPath;
    updateHoverState(m_hoveredDesktopPath, true);
}

void ListView::clearHoveredDesktopItem()
{
    if (m_hoveredDesktopPath.isEmpty()) {
        return;
    }

    updateHoveredDesktopItem(QPoint(-1, -1));
    m_hoveredDesktopPath.clear();
}

void ListView::updateAllItemRenderData()
{
    const DesktopRenderParams params = currentRenderParams();
    const QString focusedDesktopPath = effectiveFocusedDesktopPath();
    const QString focusedBoxPath = effectiveFocusedBoxPath();
    bool rebuiltAny = false;

    for (auto& state : m_itemStates) {
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        state.model.focused =
            (!m_focusedDesktopPath.isEmpty() && normalizedPath == focusedDesktopPath) ||
            (!m_focusedBoxPath.isEmpty() && normalizedPath == focusedBoxPath);

        if (!state.renderDataDirty) {
            continue;
        }

        state.model.renderData = buildRenderDataFromModel(state.model, params);
        state.renderDataDirty = false;
        rebuiltAny = true;
    }

    if (rebuiltAny) {
        markSpatialIndexDirty();
    }
}

int ListView::scalePx(int logicalPx) const
{
    return DpiScaleHelper::scalePx(this, logicalPx);
}

QPoint ListView::remapPositionForViewMetrics(const QPoint& pos,
                                             int oldIconWidth,
                                             int oldIconHeight,
                                             int oldRowSpacing,
                                             int oldColSpacing,
                                             int oldMarginTop,
                                             int oldMarginLeft) const
{
    if (pos == QPoint(0, 0)) {
        return pos;
    }

    const int oldStrideX = qMax(1, oldIconWidth + oldColSpacing);
    const int oldStrideY = qMax(1, oldIconHeight + oldRowSpacing);
    const int newStrideX = qMax(1, m_iconWidth + m_colSpacing);
    const int newStrideY = qMax(1, desktopItemFootprintHeight() + m_rowSpacing);

    QPoint mappedPos = pos;
    if (m_iconShowRule.gridAlignment) {
        const QPoint gridCell = pointToGridCell(pos, oldMarginLeft, oldMarginTop, oldStrideX, oldStrideY);
        mappedPos = gridCellToPoint(gridCell.x(), gridCell.y(), desktopLayoutLeft(), desktopLayoutTop(), newStrideX, newStrideY);
    } else {
        const double logicalX = static_cast<double>(pos.x() - oldMarginLeft) / oldStrideX;
        const double logicalY = static_cast<double>(pos.y() - oldMarginTop) / oldStrideY;
        mappedPos.setX(desktopLayoutLeft() + qRound(logicalX * newStrideX));
        mappedPos.setY(desktopLayoutTop() + qRound(logicalY * newStrideY));
    }

    mappedPos.setX(qMax(desktopLayoutLeft(), qMin(mappedPos.x(), desktopUsableRight() - m_iconWidth + 1)));
    mappedPos.setY(qMax(desktopLayoutTop(), qMin(mappedPos.y(), desktopUsableBottom() - desktopItemFootprintHeight() + 1)));
    return mappedPos;
}

void ListView::applyViewModeMetricsChange(int oldIconWidth,
                                          int oldIconHeight,
                                          int oldRowSpacing,
                                          int oldColSpacing,
                                          int oldMarginTop,
                                          int oldMarginLeft)
{
    if (oldIconWidth == m_iconWidth &&
        oldIconHeight == m_iconHeight &&
        oldRowSpacing == m_rowSpacing &&
        oldColSpacing == m_colSpacing &&
        oldMarginTop == m_marginTop &&
        oldMarginLeft == m_marginLeft) {
        return;
    }

    for (auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty()) {
            continue;
        }

        const QPoint currentPos = state.model.position;
        if (currentPos == QPoint(0, 0)) {
            continue;
        }

        state.model.position = remapPositionForViewMetrics(
            currentPos,
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
    }

    for (auto it = m_savedIconPositions.begin(); it != m_savedIconPositions.end(); ++it) {
        if (!it.value().boxId.isEmpty()) {
            continue;
        }

        const QPoint currentPos(it.value().x, it.value().y);
        const QPoint mappedPos = remapPositionForViewMetrics(
            currentPos,
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
        it.value().x = mappedPos.x();
        it.value().y = mappedPos.y();
    }

    for (auto it = m_pendingCreatedItemPositions.begin(); it != m_pendingCreatedItemPositions.end(); ++it) {
        it.value() = remapPositionForViewMetrics(
            it.value(),
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
    }
    markAllDesktopRenderDataDirty();
    updateAllItemRenderData();
    return;
}

void ListView::refreshForDpiChange()
{
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    const int oldMarginTop = m_marginTop;
    const int oldMarginLeft = m_marginLeft;

    syncViewModeSizes();
    IconCacheManager::instance().clear();

    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        applyViewModeMetricsChange(
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
        loadIconLayer();
    }

    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) {
            box->refreshLayout();
            box->update();
        }
    }

    update();
}

void ListView::setDebugGridSpacing(int rowSpacing, int colSpacing)
{
    m_rowSpacing = rowSpacing;
    m_colSpacing = colSpacing;
    loadIconLayer();
    update();
}

bool ListView::applyIconViewMode(ViewMode mode)
{
    if (m_iconShowRule.viewMode == mode) {
        return false;
    }

    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    const int oldMarginTop = m_marginTop;
    const int oldMarginLeft = m_marginLeft;

    m_iconShowRule.viewMode = mode;
    syncViewModeSizes();
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing, oldMarginTop, oldMarginLeft);
        loadIconLayer();
    }
    update();
    requestSaveBoxes();
    return true;
}

bool ListView::stepIconViewMode(int direction)
{
    if (direction == 0) {
        return false;
    }

    static const QVector<ViewMode> modes = {
        ViewMode::SmallIcon,
        ViewMode::SmallMediumIcon,
        ViewMode::MediumIcon,
        ViewMode::MediumLargeIcon,
        ViewMode::LargeIcon,
        ViewMode::HugeIcon
    };

    int currentIndex = modes.indexOf(m_iconShowRule.viewMode);
    if (currentIndex < 0) {
        currentIndex = modes.indexOf(ViewMode::MediumIcon);
    }

    const int nextIndex = qBound(0, currentIndex + (direction > 0 ? 1 : -1), modes.size() - 1);
    return applyIconViewMode(modes[nextIndex]);
}

void ListView::onMenuLargeIcons() {
    qDebug() << "ListView: Large Icons triggered";
    applyIconViewMode(ViewMode::LargeIcon);
}

void ListView::onMenuMediumIcons() {
    qDebug() << "ListView: Medium Icons triggered";
    applyIconViewMode(ViewMode::MediumIcon);
}

void ListView::onMenuSmallIcons() {
    qDebug() << "ListView: Small Icons triggered";
    applyIconViewMode(ViewMode::SmallIcon);
}

void ListView::onMenuAutoArrangeIcons() {
    qDebug() << "ListView: Auto Arrange Icons triggered";
    const bool enableAutoArrange = rightMenu->isAutoArrangeIconsChecked();
    m_iconShowRule.autoArrange = enableAutoArrange;

    if (enableAutoArrange) {
        if (m_iconShowRule.sortType != SortOrderType::SortByCustom) {
            m_iconShowRule.sortTypeBeforeAutoArrange = m_iconShowRule.sortType;
            QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
            sortAutoArrangeStates(states, m_iconShowRule.sortType);
            assignCustomSortOrders(states);
        }
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
        updateRightMenuState();
        applyAutoArrangeLayout(true, true);
    } else {
        m_iconShowRule.sortType = m_iconShowRule.sortTypeBeforeAutoArrange;
        m_iconShowRule.gridAlignment = rightMenu->isAlignIconsToGridChecked();
        m_iconShowRule.iconPosMode = m_iconShowRule.gridAlignment
            ? IconPosMode::AlignIconToGrid
            : IconPosMode::UnAlignIconToGrid;
        clearDesktopCustomSortOrders();
        for (auto it = m_savedIconPositions.begin(); it != m_savedIconPositions.end(); ++it) {
            it.value().x = 0;
            it.value().y = 0;
        }
        updateRightMenuState();
        update();
        requestSaveBoxes();
    }
}

void ListView::onMenuAlignIconsToGrid() {
    qDebug() << "ListView: Align Icons to Grid triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
        updateRightMenuState();
        applyAutoArrangeLayout(true, true);
        return;
    }

    m_iconShowRule.gridAlignment = rightMenu->isAlignIconsToGridChecked();
    m_iconShowRule.iconPosMode = m_iconShowRule.gridAlignment
        ? IconPosMode::AlignIconToGrid
        : IconPosMode::UnAlignIconToGrid;
    
    if (m_iconShowRule.gridAlignment) {
        loadGridLayout();
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuShowDesktopIcons() {
    qDebug() << "ListView: Show Desktop Icons triggered";
    if(m_iconShowRule.showDesktopIcons) {
        m_iconShowRule.showDesktopIcons = false;
        this->rightMenu->setShowDesktopIconsChecked(false);
    } else {
        m_iconShowRule.showDesktopIcons = true;
        this->rightMenu->setShowDesktopIconsChecked(true);
    }
    syncBoxVisibilityWithDesktopIcons();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    if (m_snakeGameController) {
        m_snakeGameController->onDesktopIconsVisibilityChanged();
    }
    requestSaveBoxes();
}

void ListView::onMenuDoubleClickHideIcons() {
    qDebug() << "ListView: Double Click To Hide Icons triggered";
    m_iconShowRule.doubleClickHideIcons = !m_iconShowRule.doubleClickHideIcons;
    this->rightMenu->setDoubleClickedHideIconsChecked(m_iconShowRule.doubleClickHideIcons);
    requestSaveBoxes();
}

void ListView::setDoubleClickHideIconsEnabled(bool enabled) {
    m_iconShowRule.doubleClickHideIcons = enabled;
    requestSaveBoxes();
}

bool ListView::isDoubleClickHideIconsEnabled() const {
    return m_iconShowRule.doubleClickHideIcons;
}

void ListView::onMenuLinDesk() {
    showSettings(nullptr);
}

void ListView::showSettings(LinBox* target) {
    if (m_settingsWindow) {
        m_settingsWindow->setTargetBox(target);
        m_settingsWindow->show();
        m_settingsWindow->raise();
        m_settingsWindow->activateWindow();
        return;
    }
    QVector<LinBox*> all = boxes();
    m_settingsWindow = new SettingUI(target, all);
    m_settingsWindow->show();
}

void ListView::onMenuSortByName() {
    qDebug() << "ListView: Sort By Name triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByName_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByName_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortByName_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortBySize() {
    qDebug() << "ListView: Sort By Size triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortBySize_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortBySize_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortBySize_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortByType() {
    qDebug() << "ListView: Sort By Type triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByType_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByType_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortByType_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortByDate() {
    qDebug() << "ListView: Sort By Date triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByDateModified_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByDateModified_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortByDateModified_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuRefresh() {
    syncBoxVisibilityWithDesktopIcons();

    const QVector<QPointer<LinBox>> flashBoxes = BoxManager::instance()->allBoxes();
    m_hideDesktopItemsForRefresh = true;
    update();
    for (const auto& box : flashBoxes) {
        if (box) {
            box->hide();
        }
    }

    QTimer::singleShot(80, this, [this, flashBoxes]() {
        m_hideDesktopItemsForRefresh = false;
        for (const auto& box : flashBoxes) {
            if (box) {
                box->show();
            }
        }
        syncBoxVisibilityWithDesktopIcons();
        this->update();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        QTimer::singleShot(0, this, [this]() {
            const QVector<QPointer<LinBox>> allBoxes = BoxManager::instance()->allBoxes();
            this->setUpdatesEnabled(false);
            for (const auto& box : allBoxes) {
                if (box) {
                    box->setUpdatesEnabled(false);
                }
            }

            // --- Original Refresh Logic ---
            if (m_enablePersistence) {
                for (const auto& state : m_itemStates) {
                    const QString normPath = normalizeDesktopItemPath(state.model.filePath);
                    if (normPath.isEmpty()) {
                        continue;
                    }
                    if (state.model.position != QPoint(0, 0)) {
                        m_savedIconPositions[normPath] = {
                            state.model.position.x(),
                            state.model.position.y(),
                            state.model.boxId
                        };
                    }
                }
            }

            qDebug() << "Comprehensive State Sync (Mirroring Initialize)...";
            
            if (monitor) {
                monitor->refreshIconsState();
                sysIconRegisterInfo = monitor->getCurrentIcons();
            }

            DesktopItemInitContext context = currentDesktopItemInitContext();
            for (auto& state : m_itemStates) {
                if (state.model.filePath.isEmpty()) {
                    continue;
                }

                const ListItemData data = DesktopItemLoader().loadItem(state.model.filePath);
                DesktopRootItemModel updatedModel;
                initDesktopRootItemModel(updatedModel, data, context);
                updatedModel.layoutMode = state.model.layoutMode;
                updatedModel.position = state.model.position;
                updatedModel.baseSize = QSize(m_iconWidth, m_iconHeight);
                updatedModel.effectiveSize = state.model.effectiveSize.isValid()
                    ? state.model.effectiveSize
                    : QSize(m_iconWidth, m_iconHeight);
                updatedModel.visibleTextHeight = state.model.visibleTextHeight;
                updatedModel.isSelected = state.model.isSelected;
                updatedModel.isHovered = state.model.isHovered;
                updatedModel.focused = state.model.focused;
                updatedModel.isCutMarked = state.model.isCutMarked;
                updatedModel.isFullNameExpanded = state.model.isFullNameExpanded;
                updatedModel.customSortOrder = state.model.customSortOrder;
                updatedModel.expandedPreviewText = state.model.expandedPreviewText;
                updatedModel.isRenaming = state.model.isRenaming;
                updatedModel.isReady = true;
                state.model = updatedModel;
                markDesktopRenderDataDirty(&state);
            }

            updateAllItemRenderData();
            if (m_iconShowRule.autoArrange) {
                applyAutoArrangeAfterDesktopMutation(false, false);
            } else {
                rebuildSpatialIndex();
                update();
            }

            // Refresh icons for items in boxes and keep box visibility consistent with desktop icon visibility.
            for (const auto& box : allBoxes) {
                if (box) {
                    box->refreshLayout();
                }
            }
            syncBoxVisibilityWithDesktopIcons();

            for (const auto& box : allBoxes) {
                if (box) {
                    box->setUpdatesEnabled(true);
                    box->update();
                }
            }
            this->setUpdatesEnabled(true);
            syncBoxVisibilityWithDesktopIcons();
            this->update();
            QTimer::singleShot(0, this, [this]() {
                syncBoxVisibilityWithDesktopIcons();
                syncLinWidgetLayer();
            });
        });
    });
}

void ListView::onMenuPaste() {
    const QStringList filePaths = ListViewFileOpsHelper::clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        QStringList savedVirtualPaths;
        const int savedCount = ListViewFileOpsHelper::pasteVirtualClipboardFilesToDirectory(m_desktopDir, &savedVirtualPaths);
        qDebug() << "ListView: Virtual paste triggered" << "saved:" << savedCount;
        if (savedCount <= 0) {
            qDebug() << "ListView: Paste skipped, clipboard has no local files";
        }
        return;
    }

    const bool isCutOperation = ListViewFileOpsHelper::clipboardContainsCutItems();
    bool ok = true;

    if (isCutOperation) {
        std::vector<std::wstring> srcPaths;
        srcPaths.reserve(filePaths.size());
        for (const QString& filePath : filePaths) {
            srcPaths.push_back(QDir::toNativeSeparators(filePath).toStdWString());
        }

        const std::wstring desktopDir = QDir::toNativeSeparators(m_desktopDir).toStdWString();
        ok = CSystemApi::MoveFiles(srcPaths, desktopDir);
    } else {
        ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(filePaths, m_desktopDir);
    }

    qDebug() << "ListView: Paste triggered" << (isCutOperation ? "[move]" : "[copy]") << "result:" << ok;
    if (ok) {
        if (isCutOperation) {
            clearCutMarks();
            QGuiApplication::clipboard()->clear();
        }
    }
}

void ListView::onMenuPasteShortcut() {
    const QStringList filePaths = ListViewFileOpsHelper::clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        qDebug() << "ListView: Paste Shortcut skipped, clipboard has no local files";
        return;
    }

    int createdCount = 0;
    for (const QString& filePath : filePaths) {
        const QString shortcutPath = ListViewFileOpsHelper::uniqueShortcutPathForTarget(filePath, m_desktopDir);
        if (ListViewFileOpsHelper::createShellShortcut(filePath, shortcutPath)) {
            ++createdCount;
        }
    }

    qDebug() << "ListView: Paste Shortcut triggered, created:" << createdCount << "of" << filePaths.size();
}

void ListView::onFileSystemChanged(const FileSystemEventInfo& eventInfo)
{
    // 闁荤喐鐟ョ€氼垳鈧灚鐗犲畷鐘诲冀椤掑倻鍞撮悗鍨緲鐎氬摜妲愬▎鎴犵＜闁绘柨澧庨閬嶆煟椤剙濡肩€殿喕绮欏顒€顭ㄩ崟顒佺ギ闂佹寧绋戦懟顖炵嵁閹捐绠冲璺侯儐濮圭偤鎮樿箛鏇炰壕闁哄鏅犲鍫曟偑閸涱垳顦?
    auto normalizePath = [](const QString& p) -> QString {
        QString s = QDir::toNativeSeparators(p);
        while (s.endsWith('\\') || s.endsWith('/'))
            s.chop(1);
        return s;
    };

    const QString newPath = normalizePath(eventInfo.newPath);
    const QString oldPath = normalizePath(eventInfo.oldPath);

    qDebug() << "[onFileSystemChanged]"
             << "type:" << (int)eventInfo.eventType
             << "isDir:" << eventInfo.isDirectory
             << "old:" << oldPath
             << "new:" << newPath;

    // 闂備焦褰冪粔鐢稿蓟婵犲嫭鏆滈柛鎰╁妿濠€鐗堫殽閻愯埖纭鹃柛鈺傜洴瀵?
    auto relayout = [this]() {
        if (m_iconShowRule.autoArrange) {
            applyAutoArrangeLayout(false, false);
        } else {
            loadIconLayer();
            update();
            repaint();
        }
    };

    // 闂佸湱顭堥ˇ顖滅礊鐎ｎ喖绀堢€广儱妫楃粭鎾诲箹閺夋垹绠烘い顐㈩樀瀹曟艾鈻庨幘铏仧闂?
    auto resort = [this]() {
        this->performSort(m_iconShowRule.sortType);
    };

    auto removeTrackedPath = [this](const QString& path) -> bool {
        if (path.isEmpty()) {
            return false;
        }

        if (findDesktopStateByPath(path)) {
            removeDesktopStateByPath(path);
            requestSaveBoxes();
            return true;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(path)) {
            if (box->removeItemState(path, true)) {
                requestSaveBoxes();
                return true;
            }
        }

        return false;
    };

    switch (eventInfo.eventType) {
    case FileSystemEvent::FileCreated:
    case FileSystemEvent::DirectoryCreated:
    {
        if (newPath.isEmpty()) break;
        if (findAnyItemStateByPath(newPath)) break;
        
        qDebug() << "[onFileSystemChanged] Creating item:" << newPath;
        QPointer<LinBox> pendingTargetBox = m_pendingCreatedItemBoxes.value(newPath);
        if (pendingTargetBox.isNull()) {
            pendingTargetBox = m_pendingShellNewTargetBox;
        }
        addDesktopModelItemFromPath(newPath);

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            const bool hasPendingInsertIndex = m_pendingCreatedItemInsertIndices.contains(newPath);
            const int insertIndex = hasPendingInsertIndex ? m_pendingCreatedItemInsertIndices.take(newPath) : -1;
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false, insertIndex);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                } else {
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(newPath);
        }
        
        // 婵烇絽娲︾换鍌炴偤閵娾晛绠板ù锝夘棑閻ｄ粙鏌涢弽銊у闁哄鍟粋鎺旀媼閸︻厾顦柣鐘辩劍濠㈡绱炲澶婃闁绘鐗婇悗顔济归悩铏瀯缂傚秴鎳愮槐?
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileDeleted:
    case FileSystemEvent::DirectoryDeleted:
    {
        const QString& deletePath = !oldPath.isEmpty() ? oldPath : newPath;
        if (deletePath.isEmpty()) break;
        if (consumePendingExternalRemoval(deletePath)) break;
        
            // 婵烇絽娲︾换鍌炴偤閵娾晛绠板ù锝夘棑閻ｄ粙鏌涢弽銊у闁哄鍟粋鎺旀媼閸︻厾顦紓浣割槸椤嘲鈻嶆惔鈽嗗晠闁圭粯甯掗悘鈺呮⒒閸曗晛鈧牠寮搁崘鈺冾浄闁诡垶鍋婇崬鍓佹喐?
        removeTrackedPath(deletePath);
        break;
    }
    case FileSystemEvent::FileRenamed:
    case FileSystemEvent::DirectoryRenamed:
    {
        if (oldPath.isEmpty() || newPath.isEmpty()) break;
        if (consumePendingExternalRemoval(oldPath)) break;

        QPointer<LinBox> pendingTargetBox = m_pendingCreatedItemBoxes.value(newPath);
        if (pendingTargetBox.isNull()) {
            pendingTargetBox = m_pendingShellNewTargetBox;
        }
        if (findDesktopStateByPath(oldPath)) {
            const QString oldDir = QFileInfo(oldPath).absolutePath();
            const QString newDir = QFileInfo(newPath).absolutePath();

            if (oldDir == newDir) {
                if (refreshDesktopModelItemPath(oldPath, newPath)) {
                    if (eventInfo.eventType == FileSystemEvent::DirectoryRenamed) {
                        invalidateFolderItemIconCache(oldPath);
                        invalidateFolderItemIconCache(newPath);
                    }
                    const DesktopRootItemState* updatedState = findDesktopStateByPath(newPath);
                    if (updatedState && !updatedState->model.visible) {
                        refreshVisibilityForWindowsConfig();
                        requestSaveBoxes();
                        break;
                    }
                    resort();
                    if (!m_iconShowRule.autoArrange) {
                        relayout();
                    }
                }
            } else {
                removeDesktopStateByPath(oldPath);
            }
        } else if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(oldPath)) {
            const QString oldDir = QFileInfo(oldPath).absolutePath();
            const QString newDir = QFileInfo(newPath).absolutePath();

            if (oldDir == newDir) {
                if (eventInfo.eventType == FileSystemEvent::DirectoryRenamed) {
                    invalidateFolderItemIconCache(oldPath);
                    invalidateFolderItemIconCache(newPath);
                }
                refreshBoxModelItemPath(sourceBox, oldPath, newPath);
            } else {
                sourceBox->removeItemState(oldPath, true);
            }
        } else if (!findAnyItemStateByPath(newPath)) {
            addDesktopModelItemFromPath(newPath);
        }

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            const bool hasPendingInsertIndex = m_pendingCreatedItemInsertIndices.contains(newPath);
            const int insertIndex = hasPendingInsertIndex ? m_pendingCreatedItemInsertIndices.take(newPath) : -1;
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false, insertIndex);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                } else {
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(newPath);
        }
        
        // 婵烇絽娲︾换鍌炴偤閵娾晛绠板ù锝夘棑閻ｄ粙鏌涢弽銊у闁哄鍟粋鎺旀媼閸︻厾顦梺鍝勬搐閻°劑寮绘繝鍥ㄧ厒鐎广儱鎳忛崵鎺楁煕濮橆剛校闁哄鍟粋鎺楀箚闁箑鏁归悷?
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileUpdated:
    case FileSystemEvent::FileAttributesChanged:
    case FileSystemEvent::DirectoryUpdated:
    {
        const QString& updatePath = !newPath.isEmpty() ? newPath : oldPath;
        if (updatePath.isEmpty()) break;

        if (!QFileInfo::exists(updatePath)) {
            removeTrackedPath(updatePath);
            break;
        }

        if (eventInfo.eventType == FileSystemEvent::DirectoryUpdated) {
            refreshFolderItemByPath(updatePath);
            break;
        }

        if (findDesktopStateByPath(updatePath)) {
            refreshDesktopModelItemPath(updatePath, updatePath);
            if (eventInfo.eventType == FileSystemEvent::FileAttributesChanged) {
                update();
            }
        } else if (LinBox* box = BoxManager::instance()->findBoxContainingItem(updatePath)) {
            refreshBoxModelItemPath(box, updatePath, updatePath);
        }
        break;
    }
    }
    
    // Ensure all LinBoxes stay above desktop items
    for (const auto& box : BoxManager::instance()->allBoxes()) {
        if (box) box->raise();
    }
}

void ListView::onMenuQuitApp() {
    qDebug() << "LinDesk: Global Quit Requested...";
    qApp->setProperty("LinDeskForceQuit", true);
    
    // Close the settings window if open
    if (m_settingsWindow) {
        m_settingsWindow->close();
    }

    // Close all remaining windows (boxes, etc.)
    QApplication::closeAllWindows();
    
    // Final quit
    qApp->quit();
}

void ListView::onMenuCreateBox()
{
    BoxManager* manager = BoxManager::instance();
	LinBox* box = manager->createBox(m_lastRightClickPos, this);
    
    // Connect box signals to ListView
    manager->setupConnections(box, this);
}

void ListView::onMenuMapFolder()
{
    const QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Folder to Map"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedDir.isEmpty()) {
        return;
    }

    const QString normalizedPath = normalizeMappedFolderPathForLookup(selectedDir);
    if (findMappedFolderBox(normalizedPath)) {
        showTransientTip(tr("This folder is already on the desktop."), 2600);
        return;
    }

    BoxManager* manager = BoxManager::instance();
	LinBox* box = manager->createBox(m_lastRightClickPos, this);
    manager->setupConnections(box, this);

    box->setBoxTitle(QString());
    box->setMappedFolderPath(normalizedPath);
    loadMappedFolderItemsForBox(box);
    box->refreshLayout();
    box->raise();
    requestSaveBoxes();
}

void ListView::onMenuOrganizeDesktop()
{
    if (!m_enablePersistence) {
        m_enablePersistence = true;
    }

    m_iconShowRule.autoArrange = true;
    m_iconShowRule.gridAlignment = true;
    m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
    m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByName_ASC;
    m_iconShowRule.sortType = SortOrderType::SortByCustom;

    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, SortOrderType::SortByName_ASC);
    assignCustomSortOrders(states);
    const QVector<QPoint> positions = layoutAutoArrangeItems(states);
    finalizeAutoArrangeLayout(states, positions, true, false);
    updateRightMenuState();
}

void ListView::onMenuOrganizeDesktopByNameSort()
{
    onMenuSortByName();
}

void ListView::createRecentModifiedItemsBox()
{
    BoxManager* manager = BoxManager::instance();
    if (!manager) {
        return;
    }

    const QPoint boxPos(desktopLayoutLeft() + 24, desktopLayoutTop() + 24);
    LinBox* box = manager->createBox(boxPos, this);
    if (!box) {
        return;
    }
    manager->setupConnections(box, this);
    box->setBoxTitle(QStringLiteral("Recent Changes"));

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    QStringList recentPaths;
    for (const DesktopRootItemState& state : m_itemStates) {
        const DesktopRootItemModel& model = state.model;
        if (!model.boxId.isEmpty() || !model.visible || model.filePath.isEmpty() || model.filePath.startsWith(QStringLiteral("::"))) {
            continue;
        }

        QFileInfo fileInfo(model.filePath);
        if (!fileInfo.exists() || fileInfo.fileName().compare(QStringLiteral("desktop.ini"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        if (fileInfo.lastModified() >= cutoff) {
            recentPaths.append(model.filePath);
        }
    }

    if (!recentPaths.isEmpty()) {
        moveItemsToBox(recentPaths, box, -1, false);
    }

    box->refreshLayout();
    box->update();
    box->raise();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    requestSaveBoxes();
}

QVector<LinBox*> ListView::boxes() const {
    QVector<LinBox*> result;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) result.append(box.data());
    }
    return result;
}

bool ListView::desktopIconsVisible() const
{
    return m_iconShowRule.showDesktopIcons;
}

QVector<DesktopSnakeGameIcon> ListView::desktopSnakeIcons() const
{
    QVector<DesktopSnakeGameIcon> icons;

    auto appendIcon = [&icons](const DesktopRootItemState& state) {
        QPixmap pixmap = state.model.renderData.iconPixmap;
        if (pixmap.isNull() && !state.model.sourceIcon.isNull()) {
            pixmap = state.model.sourceIcon.pixmap(QSize(64, 64));
        }
        if (!pixmap.isNull()) {
            DesktopSnakeGameIcon icon;
            icon.pixmap = pixmap;
            icon.displayName = state.model.displayName.isEmpty()
                ? QFileInfo(state.model.filePath).completeBaseName()
                : state.model.displayName;
            icons.append(icon);
        }
    };

    for (const DesktopRootItemState& state : m_itemStates) {
        if (state.model.isReady) {
            appendIcon(state);
        }
    }

    for (const QPointer<LinBox>& boxPtr : BoxManager::instance()->allBoxes()) {
        const LinBox* box = boxPtr.data();
        if (!box) {
            continue;
        }
        for (const DesktopRootItemState& state : box->itemStates()) {
            if (state.model.isReady) {
                appendIcon(state);
            }
        }
    }

    return icons;
}

	// 闂佺绻愰悧濠囧蓟閸パ屽殨闁逞屽墰閹?item 闂佹眹鍔岀€氼厽鏅跺澶婂珘濠㈣泛顦换鍡涙煕濞嗘垶鐒跨紒缁樼洴楠炴帡濡烽婊咁槷闂傚倸鍟鍫曨敆濞戙垺鐓傜€广儱鎷嬪Σ濠氬级閳哄啫浠﹂悽?
void ListView::resetRenameClickState()
{
    m_lastClickedDesktopPath.clear();
    m_lastClickedBoxPath.clear();
    m_wasSelectedBeforeClick = false;
    m_clickTimer.invalidate();
}

bool ListView::startDesktopItemDrag(const QPoint& clickPos)
{
    const QStringList dragPaths = selectedDesktopPaths();
    if (dragPaths.isEmpty()) {
        return false;
    }

    QList<QUrl> urls;
    urls.reserve(dragPaths.size());
    for (const QString& path : dragPaths) {
        if (!path.isEmpty()) {
            urls.append(QUrl::fromLocalFile(path));
        }
    }
    if (urls.isEmpty()) {
        return false;
    }

    auto* mimeData = new WindowsDragMimeData();
    mimeData->setUrls(urls);
    mimeData->setDropDescriptionForAction(Qt::MoveAction, QCoreApplication::translate("ListView", "Desktop"));

    QVector<DesktopRootItemRenderData> ghostItems;
    QRect ghostBounds;
    for (const QString& path : dragPaths) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (!state) {
            continue;
        }

        const DesktopRootItemRenderData& renderData = state->model.renderData;
        if (renderData.itemRect.isNull() || !renderData.itemRect.size().isValid()) {
            continue;
        }

        ghostItems.append(renderData);
        ghostBounds = ghostBounds.isNull()
            ? renderData.itemRect
            : ghostBounds.united(renderData.itemRect);
    }

    QPixmap dragPixmap;
    QPoint hotSpot;
    if (!ghostItems.isEmpty() && ghostBounds.isValid()) {
        dragPixmap = QPixmap(ghostBounds.size());
        dragPixmap.fill(Qt::transparent);

        QPainter painter(&dragPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.translate(-ghostBounds.topLeft());
        for (const DesktopRootItemRenderData& renderData : ghostItems) {
            paintDesktopRootItemGhost(painter, renderData);
        }
        painter.end();

        hotSpot = clickPos - ghostBounds.topLeft();
        hotSpot.setX(qBound(0, hotSpot.x(), qMax(0, dragPixmap.width() - 1)));
        hotSpot.setY(qBound(0, hotSpot.y(), qMax(0, dragPixmap.height() - 1)));
    }

    for (GhostInfo& ghost : m_ghosts) {
        if (ghost.widget) {
            ghost.widget->hide();
            ghost.widget->deleteLater();
        }
    }
    m_ghosts.clear();

    QDrag drag(this);
    drag.setMimeData(mimeData);
    if (!dragPixmap.isNull()) {
        drag.setPixmap(dragPixmap);
        drag.setHotSpot(hotSpot);
    }

    const Qt::DropAction resultAction = drag.exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::MoveAction);
    if (resultAction == Qt::MoveAction) {
        QStringList removedSourcePaths;
        for (const QString& path : dragPaths) {
            if (!path.isEmpty() && !QFileInfo::exists(path)) {
                removedSourcePaths.append(path);
            }
        }
        finalizeMovedModelSourcesAfterFileOp(removedSourcePaths);
    }

    clearDesktopAutoArrangeReorderPreview();
    m_isDragging = false;
    m_draggedDesktopPath.clear();
    m_draggedBoxPath.clear();
    return true;
}

void ListView::finalizeActiveDrag(LinBox* box, QMouseEvent* event)
{
	if (m_isDragging) {
        for (LinBox* existingBox : boxes()) {
            if (existingBox) {
                existingBox->clearDropPreview();
            }
        }
        if (m_dragTip) {
            m_dragTip->hide();
        }
        LinBox* targetBox = box;
        DesktopRootItemState* dropTargetState = nullptr;
        const QPoint releasePos = event ? event->pos() : QPoint();
        if (!targetBox && event) {
            const QPoint globalPos = event->globalPos();
            const QVector<LinBox*> allBoxes = boxes();
            for (LinBox* candidateBox : allBoxes) {
                if (!candidateBox) {
                    continue;
                }
                const QRect candidateGlobalRect(candidateBox->mapToGlobal(QPoint(0, 0)),
                                                candidateBox->size());
                if (candidateGlobalRect.contains(globalPos)) {
                    targetBox = candidateBox;
                    break;
                }
            }
            if (!targetBox) {
                dropTargetState = findDesktopStateAt(releasePos);
                if (dropTargetState && isDesktopPathSelected(dropTargetState->model.filePath)) {
                    dropTargetState = nullptr;
                }
            }
        }

        QStringList draggedPaths;
        for (const auto& ghost : m_ghosts) {
            if (!ghost.filePath.isEmpty() && !draggedPaths.contains(ghost.filePath)) {
                draggedPaths.append(ghost.filePath);
            }
        }

        const Qt::KeyboardModifiers modifiers = event ? event->modifiers() : QApplication::keyboardModifiers();
        const bool copyMode = (modifiers & Qt::ControlModifier);
        const bool preferShortcut = shouldPreferShortcutForDrop(draggedPaths, modifiers, false);
        const bool desktopBackgroundFileOperation = !targetBox && !dropTargetState && !draggedPaths.isEmpty() &&
            (copyMode || preferShortcut);

        if (!targetBox && m_iconShowRule.autoArrange && !desktopBackgroundFileOperation) {
            const QStringList activeDesktopDragPaths = orderedDraggedPaths(draggedPaths);
            if (m_iconShowRule.sortType == SortOrderType::SortByCustom && !activeDesktopDragPaths.isEmpty()) {
                const int customInsertIndex = m_desktopReorderInsertIndex >= 0
                    ? m_desktopReorderInsertIndex
                    : desktopCustomInsertionIndexForPos(releasePos, activeDesktopDragPaths);

                QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
                sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);

                QSet<QString> draggedPathSet;
                QVector<DesktopRootItemState*> orderedDraggedStates;
                for (const QString& path : activeDesktopDragPaths) {
                    draggedPathSet.insert(normalizeDesktopItemPath(path));
                }
                for (DesktopRootItemState* state : orderedStates) {
                    if (state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath))) {
                        orderedDraggedStates.append(state);
                    }
                }
                orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
                    return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
                }), orderedStates.end());

                const int clampedIndex = qBound(0, customInsertIndex, orderedStates.size());
                for (int i = 0; i < orderedDraggedStates.size(); ++i) {
                    orderedStates.insert(clampedIndex + i, orderedDraggedStates[i]);
                }
                assignCustomSortOrders(orderedStates);
            }

            for (auto& ghost : m_ghosts) {
                if (ghost.widget) {
                    ghost.widget->hide();
                    ghost.widget->deleteLater();
                }
            }
            m_ghosts.clear();
            m_isDragging = false;
            m_draggedDesktopPath.clear();
            m_draggedBoxPath.clear();
            clearDesktopAutoArrangeReorderPreview();
            applyAutoArrangeLayout(true, false);
            return;
        }

		bool hasCollision = false;
        bool desktopPositionsChanged = false;
        QMap<QString, QPoint> resolvedDesktopPositions;

        if (!targetBox && !dropTargetState && m_iconShowRule.gridAlignment && !draggedPaths.isEmpty() &&
            !desktopBackgroundFileOperation) {
            if (applyGridAlignedDesktopDropLayout(draggedPaths, releasePos)) {
                for (auto& ghost : m_ghosts) {
                    if (ghost.widget) {
                        ghost.widget->hide();
                        ghost.widget->deleteLater();
                    }
                }
                m_ghosts.clear();
                m_isDragging = false;
                m_draggedDesktopPath.clear();
                m_draggedBoxPath.clear();
                clearDesktopAutoArrangeReorderPreview();
                return;
            }
        }

        if (!targetBox && !dropTargetState && !desktopBackgroundFileOperation && !draggedPaths.isEmpty()) {
            resolvedDesktopPositions = resolveDesktopDragFinalPositions(draggedPaths, releasePos);
            hasCollision = resolvedDesktopPositions.isEmpty();
        }

		// 2. Apply movement
		for (auto& ghost : m_ghosts) {
            DesktopRootItemState* draggedState = ghost.filePath.isEmpty()
                ? nullptr
                : findDesktopStateByPath(ghost.filePath);
			if (ghost.widget && draggedState) {
				if (!hasCollision) {
					QPoint finalPos = this->mapFromGlobal(ghost.widget->pos());
                    const QString normalizedPath = normalizeDesktopItemPath(ghost.filePath);
                    if (!normalizedPath.isEmpty() && resolvedDesktopPositions.contains(normalizedPath)) {
                        finalPos = resolvedDesktopPositions.value(normalizedPath);
                    }
					
					// Only move if it's a desktop icon (parent is ListView)
					// Items in box stay where they are (refreshLayout handles them)
					if (!targetBox && !dropTargetState && !desktopBackgroundFileOperation) {
                        if (draggedState) {
                            if (draggedState->model.position != finalPos) {
                                draggedState->model.position = finalPos;
                                draggedState->model.renderData.itemRect.moveTopLeft(finalPos);
                                markDesktopRenderDataDirty(draggedState);
                                desktopPositionsChanged = true;
                            }
                        }
					}
				}
				ghost.widget->hide();
				ghost.widget->deleteLater();
			}
		}

		m_ghosts.clear();
		m_isDragging = false;
        const QString draggedDesktopPath = m_draggedDesktopPath;
        m_draggedDesktopPath.clear();
        m_draggedBoxPath.clear();
        if (dropTargetState && !draggedPaths.isEmpty()) {
            if (handleDroppedPathsOnDesktopTarget(
                    draggedPaths,
                    dropTargetState->model,
                    copyMode,
                    preferShortcut,
                    true)) {
                clearDesktopAutoArrangeReorderPreview();
                requestSaveBoxes();
                return;
            }

            if (!draggedDesktopPath.isEmpty()) {
                m_draggedDesktopPath = draggedDesktopPath;
            }
            updateAllItemRenderData();
            update();
        } else if (desktopBackgroundFileOperation) {
            if (!m_iconShowRule.autoArrange) {
                for (const QString& localPath : draggedPaths) {
                    if (localPath.isEmpty()) {
                        continue;
                    }
                    const QString reservedPath = preferShortcut
                        ? ListViewFileOpsHelper::uniqueShortcutPathForTarget(localPath, m_desktopDir)
                        : ListViewFileOpsHelper::uniquePasteDestinationPath(localPath, m_desktopDir);
                    reserveCreatedItemPosition(reservedPath, releasePos);
                }
            }

            bool ok = false;
            if (preferShortcut) {
                ok = createShortcutsInFolder(draggedPaths, m_desktopDir);
            } else {
                ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
            }

            if (ok) {
                applyAutoArrangeAfterDesktopMutation(false, false);
            } else {
                updateAllItemRenderData();
                update();
            }
        } else if (targetBox && !draggedPaths.isEmpty()) {
            if (targetBox->isFolderMapped()) {
                const QString targetFolder = normalizeMappedFolderPathForLookup(targetBox->mappedFolderPath());
                const bool canWriteMappedFolder =
                    !targetFolder.isEmpty() &&
                    targetBox->mappedFolderState() == LinBox::MappedFolderState::Normal;

                std::vector<std::wstring> srcPaths;
                if (canWriteMappedFolder) {
                    for (const QString& draggedPath : draggedPaths) {
                        if (!draggedPath.isEmpty()) {
                            srcPaths.push_back(QDir::toNativeSeparators(draggedPath).toStdWString());
                        }
                    }
                }

                bool ok = false;
                if (canWriteMappedFolder && !srcPaths.empty()) {
                    if (preferShortcut) {
                        ok = createShortcutsInFolder(draggedPaths, targetFolder);
                    } else if (copyMode) {
                        ok = CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetFolder).toStdWString());
                    } else {
                        ok = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetFolder).toStdWString());
                    }
                }

                if (ok) {
                    if (!copyMode && !preferShortcut) {
                        finalizeMovedModelSourcesAfterFileOp(draggedPaths);
                    }
                    reloadMappedFolderBox(targetBox);
                    targetBox->refreshLayout();
                    targetBox->update();
                    requestSaveBoxes();
                } else {
                    updateAllItemRenderData();
                    update();
                }
            } else {
                moveItemsToBox(draggedPaths, targetBox, -1, true);
            }
        } else if (!targetBox && (desktopPositionsChanged || hasCollision)) {
            updateAllItemRenderData();
            update();
        }
        clearDesktopAutoArrangeReorderPreview();
		
		// Snap back to grid in LinBox if applicable
		if (targetBox) {
			targetBox->refreshLayout();
		}
		
		// 婵烇絽娲︾换鍌炴偤閵娾晛鐐婇柣鎰靛墰閸ㄧ厧霉閿濆懐肖闁汇倕妫濆畷姘閺夋垵璧嬬紓鍌氬枤閸犳寮搁崘鈺冾浄?
        if (targetBox || dropTargetState || desktopBackgroundFileOperation || desktopPositionsChanged) {
		    requestSaveBoxes();
        }
	}
}

void ListView::createDragGhosts(const QPoint& clickPos)
{
	// 濠电偞鎸搁幊鎰板箖婵犲洤绫嶇憸蹇撯枔?ghost
	for (auto& ghost : m_ghosts) {
		if (ghost.widget) ghost.widget->deleteLater();
	}
	m_ghosts.clear();

    for (const QString& desktopPath : selectedDesktopPaths()) {
        bool alreadyTracked = false;
        for (const auto& ghost : m_ghosts) {
            if (ghost.filePath == desktopPath) {
                alreadyTracked = true;
                break;
            }
        }
        if (alreadyTracked) {
            continue;
        }

        DesktopRootItemState* state = findDesktopStateByPath(desktopPath);
        if (!state) {
            continue;
        }

        const DesktopRootItemRenderData& renderData = state->model.renderData;
        const QSize ghostSize = renderData.itemRect.size();
        if (!ghostSize.isValid()) {
            continue;
        }

        QLabel* ghostWidget = new QLabel(nullptr);
        ghostWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput);
        ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
        ghostWidget->setAttribute(Qt::WA_NoSystemBackground);
        ghostWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
        ghostWidget->setStyleSheet("background: transparent;");

        QPixmap pixmap(ghostSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(-renderData.itemRect.topLeft());
        paintDesktopRootItemGhost(painter, renderData);
        painter.end();

        ghostWidget->setPixmap(pixmap);
        ghostWidget->setFixedSize(ghostSize);

        QPoint globalItemPos = this->mapToGlobal(renderData.itemRect.topLeft());
        QPoint globalClickPos = this->mapToGlobal(clickPos);
        QPoint offset = globalItemPos - globalClickPos;

        ghostWidget->move(QCursor::pos() + offset);
        ghostWidget->show();
        if (HWND hwnd = reinterpret_cast<HWND>(ghostWidget->winId())) {
            SetWindowPos(
                hwnd,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
            );
        }

        m_ghosts.append({ ghostWidget, desktopPath, offset });
    }
}


void ListView::ensureDragTip() {
    if (!m_dragTip) {
        m_dragTip = new QLabel(this);
        m_dragTip->setTextFormat(Qt::RichText);
        m_dragTip->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background-color: rgba(255, 255, 255, 235);"
            "  color: #202020;"
            "  border: 1px solid rgba(0, 0, 0, 40);"
            "  border-radius: 8px;"
            "  padding: 10px 16px;"
            "  font-size: 13px;"
            "  font-family: %1;"
            "}"
        ).arg(AppFonts::uiCssFamily()));
        m_dragTip->setAttribute(Qt::WA_TransparentForMouseEvents);
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_dragTip);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setOffset(0, 4);
        m_dragTip->setGraphicsEffect(shadow);
        m_dragTip->hide();
    }
}

void ListView::ensureInfoTip()
{
    if (!m_infoTip) {
        m_infoTip = new FluentInfoTip(this);
        m_infoTip->hide();
    }
}

void ListView::showTransientTip(const QString& message, int autoHideMs)
{
    ensureInfoTip();
    m_infoTip->setText(message);
    const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
    m_infoTip->showCentered(globalRect, autoHideMs);
}

bool ListView::createNewFolderInHost(LinBox* targetBox)
{
    if (RenameManager::instance()->isRenaming()) {
        return false;
    }

    const bool createInMappedFolder = targetBox && targetBox->isFolderMapped();
    if (createInMappedFolder && targetBox->mappedFolderState() != LinBox::MappedFolderState::Normal) {
        return false;
    }

    const QString targetDirectory = createInMappedFolder
        ? normalizeMappedFolderPathForLookup(targetBox->mappedFolderPath())
        : normalizeDesktopPath(m_desktopDir);
    if (targetDirectory.isEmpty()) {
        return false;
    }

    const QString newFolderPath = ListViewFileOpsHelper::uniqueNewFolderPath(targetDirectory);
    if (newFolderPath.isEmpty()) {
        return false;
    }

    const bool reserveDesktopPosition = !targetBox;
    if (targetBox && !createInMappedFolder) {
        m_pendingCreatedItemBoxes[newFolderPath] = targetBox;
        m_pendingCreatedItemInsertIndices.remove(newFolderPath);
    } else if (reserveDesktopPosition) {
        if (!m_iconShowRule.autoArrange) {
            const DesktopRootItemState* focusedDesktopState = m_focusedDesktopPath.isEmpty()
                ? nullptr
                : findDesktopStateByPath(m_focusedDesktopPath);
            const QPoint requestedPos = focusedDesktopState
                ? focusedDesktopState->model.position
                : generateItemPosition();
            reserveCreatedItemPosition(newFolderPath, requestedPos);
        }
    }

    if (!ListViewFileOpsHelper::createDirectoryPath(newFolderPath)) {
        m_pendingCreatedItemBoxes.remove(newFolderPath);
        m_pendingCreatedItemInsertIndices.remove(newFolderPath);
        if (reserveDesktopPosition) {
            m_pendingCreatedItemPositions.remove(normalizeDesktopPath(newFolderPath));
        }
        return false;
    }

    if (createInMappedFolder && targetBox) {
        QTimer::singleShot(0, this, [this, targetBox]() {
            if (!targetBox) {
                return;
            }
            reloadMappedFolderBox(targetBox);
            targetBox->refreshLayout();
            targetBox->raise();
            update();
        });
    }

    return true;
}

bool ListView::createNewTextDocumentInHost()
{
    if (RenameManager::instance()->isRenaming()) {
        return false;
    }

    const QString targetDirectory = normalizeDesktopPath(m_desktopDir);
    if (targetDirectory.isEmpty()) {
        return false;
    }

    const QString templatePath = QDir(targetDirectory).filePath(tr("New Text Document.txt"));
    const QString newTextPath = ListViewFileOpsHelper::uniquePasteDestinationPath(templatePath, targetDirectory);
    if (newTextPath.isEmpty()) {
        return false;
    }

    if (!m_iconShowRule.autoArrange) {
        reserveCreatedItemPosition(newTextPath, generateItemPosition());
    }

    QFile file(newTextPath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_pendingCreatedItemPositions.remove(normalizeDesktopPath(newTextPath));
        return false;
    }

    file.close();
    return true;
}

void ListView::onMenuDelete(bool permanently)
{
    std::vector<std::wstring> paths;
    QStringList desktopPathsToDelete;
    QStringList boxPathsToDelete;
    QSet<QString> seenDeletePaths;

    for (const QString& path : selectedDesktopPaths()) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (!state) {
            continue;
        }
        if (state->model.itemData.isRealSpecialFolderIcon()) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty() || seenDeletePaths.contains(normalizedPath)) {
            continue;
        }
        seenDeletePaths.insert(normalizedPath);
        desktopPathsToDelete.append(normalizedPath);
        paths.push_back(QDir::toNativeSeparators(state->model.filePath).toStdWString());
    }

    for (const QString& path : selectedBoxPaths()) {
        const DesktopRootItemState* state = findAnyItemStateByPath(path);
        if (!state || state->model.itemData.isRealSpecialFolderIcon()) continue;
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty() || seenDeletePaths.contains(normalizedPath)) {
            continue;
        }
        seenDeletePaths.insert(normalizedPath);
        boxPathsToDelete.append(normalizedPath);
        paths.push_back(QDir::toNativeSeparators(state->model.filePath).toStdWString());
    }

    if (paths.empty()) return;

    const bool deleteOk = CSystemApi::DeleteFiles(paths, !permanently, true);
    if (!deleteOk) {
        return;
    }

    trackPendingExternalRemovalPaths(desktopPathsToDelete);
    removeItemPathsBatch(desktopPathsToDelete);
    for (const QString& path : desktopPathsToDelete) {
        m_pendingExternalRemovalPaths.insert(normalizeDesktopPath(path));
        removeDesktopStateByPath(path);
    }
    for (const QString& path : boxPathsToDelete) {
        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(path)) {
            box->removeItemState(path, true);
        }
    }
    requestSaveBoxes();
}

void ListView::saveBoxes() {
    if (m_isLoadingPersistence) {
        return;
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.preAutoSort = (int)m_iconShowRule.sortTypeBeforeAutoArrange;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;
    state.iconPositions = persistenceService.collectIconPositions(
        m_itemStates,
        BoxManager::instance()->allBoxes()
    );
    m_savedIconPositions = persistenceService.toSavedIconPositions(state.iconPositions);
    persistenceService.save(m_persistencePath, state);
}

void ListView::requestSaveBoxes() {
    if (m_isLoadingPersistence) {
        return;
    }
    BoxManager::instance()->requestSave();
}



void ListView::applyBoxAssignmentsFromConfig(const QVector<QMap<QString, QVariant>>& boxData) {
    qDebug() << "[applyBoxAssignmentsFromConfig] Processing" << boxData.size() << "boxes";
    QSet<LinBox*> boxesToRefresh;
    
    for (const auto& data : boxData) {
        const QString boxId = data.value("id").toString();
        LinBox* targetBox = BoxManager::instance()->findBoxById(boxId);
        if (!targetBox) {
            qWarning() << "[applyBoxAssignmentsFromConfig] Box not found:" << boxId;
            continue;
        }

        if (targetBox->isFolderMapped()) {
            continue;
        }
        
        const QStringList files = data.value("files").toStringList();
        qDebug() << "[applyBoxAssignmentsFromConfig] Box" << boxId << "has" << files.size() << "files";
        
        for (const QString& path : files) {
            DesktopRootItemState* state = findDesktopStateByPath(path);
            if (!state) {
                qWarning() << "[applyBoxAssignmentsFromConfig] Item not found:" << path;
                continue;
            }

            qDebug() << "[applyBoxAssignmentsFromConfig] Moving model item to box:" << state->model.displayName
                     << "path:" << state->model.filePath
                     << "to box:" << targetBox;
            DesktopRootItemState stateCopy = *state;
            stateCopy.model.boxId = boxId;
            stateCopy.model.visible = true;
            targetBox->addItemState(stateCopy, false);
            removeDesktopStateByPath(path);
            boxesToRefresh.insert(targetBox);
            
            // 闁荤姳绀佹晶浠嬫偪?boxId 濡ょ姷鍋犲▔娑㈡儍?item 闁诲骸婀遍崑銈咁瀶椤栨搴ｆ嫚閹绘帩娼遍梺?box 婵?
            // 婵炶揪缍€濞夋洟寮?addListItem 闂佸搫顦崕鏌ュ锤婵犲洤绀夐柣娆忔噽缁€澶愭倵閻熸澘鏆曠紒杈ㄦ緲椤垽鏁愰崶锝傚亾濡吋濯奸柛鎾楀懏鐎梺缁樼墪婢у酣鎮伴妷鈺佺煑闁挎繂瀚粈?m_contentArea
        }
    }

    for (LinBox* box : boxesToRefresh) {
        if (!box) continue;
        box->refreshLayout();
        box->raise();
    }
}

QStringList ListView::mappedFolderEntryPaths(const QString& folderPath) const {
    return ListViewMappedBoxHelper::mappedFolderEntryPaths(folderPath, m_showHiddenFiles);
}

void ListView::clearMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::clearMappedFolderItemsForBox(*this, box);
}

void ListView::loadMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::loadMappedFolderItemsForBox(*this, box);
}

void ListView::applySavedPositionsToItems(const QMap<QString, QMap<QString, QVariant>>& iconPositions) {
    for (auto it = iconPositions.begin(); it != iconPositions.end(); ++it) {
        const QMap<QString, QVariant>& posInfo = it.value();
        const QString boxIdFromConfig = posInfo.value("boxId").toString();
        const QPoint savedPos(posInfo.value("x").toInt(), posInfo.value("y").toInt());

        if (DesktopRootItemState* state = findDesktopStateByPath(it.key())) {
            state->model.boxId = boxIdFromConfig;
            state->model.customSortOrder = posInfo.value("ord").toInt();
            state->model.position = savedPos;
        } else if (LinBox* box = BoxManager::instance()->findBoxContainingItem(it.key())) {
            if (DesktopRootItemState* boxState = box->findItemStateByPath(it.key())) {
                boxState->model.boxId = boxIdFromConfig;
                boxState->model.customSortOrder = posInfo.value("ord").toInt();
                boxState->model.position = savedPos;
            }
        } else {
            qWarning() << "[ListView] No desktop item matched config path:" << it.key();
        }
    }
}

void ListView::loadBoxes(bool autoUpdateState) {
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);

    m_isLoadingPersistence = true;
    m_forceConfigLayout = false;
    m_hasLoadedConfigLayout = false;

    QVector<LinBox*> existingBoxes;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) existingBoxes.append(box.data());
    }
    for (auto* box : existingBoxes) {
        if (box) {
            if (box->isFolderMapped()) {
                clearMappedFolderItemsForBox(box);
            }
            for (const DesktopRootItemState& boxedState : box->itemStates()) {
                if (!boxedState.model.filePath.isEmpty()) {
                    DesktopRootItemState restoredState = boxedState;
                    resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
                    m_itemStates.push_back(restoredState);
                    markDesktopStateIndexDirty();
                }
            }
            box->deleteLater();
        }
    }
    BoxManager::instance()->clearBoxes();
    m_mappedBoxFilePaths.clear();
    for (auto& state : m_itemStates) {
        state.model.boxId.clear();
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.preAutoSort = (int)m_iconShowRule.sortTypeBeforeAutoArrange;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;

    persistenceService.load(m_persistencePath, autoUpdateState, state);

    m_enablePersistence = state.enablePersistence;
    m_iconShowRule.viewMode = (ViewMode)state.viewMode;
    if (state.viewMode < (int)ViewMode::LargeIcon || state.viewMode > (int)ViewMode::HugeIcon) {
        m_iconShowRule.viewMode = ViewMode::MediumIcon;
    }
    m_iconShowRule.sortType = (SortOrderType)state.sortType;
    m_iconShowRule.sortTypeBeforeAutoArrange = (SortOrderType)state.preAutoSort;
    m_iconShowRule.iconPosMode = (IconPosMode)state.iconPosMode;
    m_iconShowRule.autoArrange = state.autoArrange;
    m_iconShowRule.gridAlignment = state.gridAlignment;
    m_iconShowRule.showDesktopIcons = state.showDesktopIcons;
    m_iconShowRule.doubleClickHideIcons = state.doubleClickHideIcons;
    m_iconShowRule.showShortCutArrow = state.showShortCutArrow;
    if (m_iconShowRule.autoArrange) {
        if (m_iconShowRule.sortType != SortOrderType::SortByCustom) {
            m_iconShowRule.sortTypeBeforeAutoArrange = m_iconShowRule.sortType;
            m_iconShowRule.sortType = SortOrderType::SortByCustom;
        }
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
    }

    QVector<QMap<QString, QVariant>>& boxData = state.boxData;
    QMap<QString, QMap<QString, QVariant>>& iconPositions = state.iconPositions;

    m_forceConfigLayout = !boxData.isEmpty() || !iconPositions.isEmpty();
    m_hasLoadedConfigLayout = m_forceConfigLayout;

    syncViewModeSizes();
    updateRightMenuState();

    for (const auto& data : boxData) {
        createBoxFromSavedData(
            data.value("title").toString(),
            QRect(data.value("x").toInt(), data.value("y").toInt(), 
                  data.value("w").toInt(), data.value("h").toInt()),
            data.value("collapsed").toBool(),
            data.value("h").toInt(),
            data.value("showType").toInt(),
            data.value("files").toStringList(),
            QColor(data.value("color").toString()),
            normalizedSavedBoxOpacity(data.value("opacity")),
            data.value("glass").toBool(),
            data.value("id").toString(),
            data.value("mappedFolderPath").toString(),
            false
        );
    }
    applyBoxAssignmentsFromConfig(boxData);
    applySavedPositionsToItems(iconPositions);

    m_savedIconPositions = persistenceService.toSavedIconPositions(iconPositions);
    loadIconLayer();
    m_forceConfigLayout = false;
    update();
    m_isLoadingPersistence = false;
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    selectionSnapshot.restore(*this, false);

    qDebug() << "Loaded. Persistence:" << m_enablePersistence;
}

void ListView::createBoxFromSavedData(const QString& title, const QRect& geom, bool collapsed, int origH, int showType, const QStringList& files, const QColor& color, float opacity, bool glass, const QString& explicitId, const QString& mappedFolderPath, bool refresh) {
    LinBox* box = new LinBox(this);
    const QRect safeGeometry = clampedRestoredBoxGeometry(geom);
    const float safeOpacity = clampedRestoredBoxOpacity(opacity);
    const LinBox::BoxShowType safeShowType = restoredBoxShowType(showType);
    if (explicitId.isEmpty()) {
        box->setBoxId(QUuid::createUuid().toString());
    } else {
        box->setBoxId(explicitId);
    }
    
    if (!mappedFolderPath.isEmpty()) {
        if (title.isEmpty()) {
            box->setBoxTitle(QString());
        } else {
            box->setBoxTitle(title);
        }
        box->setMappedFolderPath(mappedFolderPath);
    } else if (!title.isEmpty()) {
        box->setBoxTitle(title);
    }
    
    if (color.isValid()) box->setBackgroundColor(color);
    box->setBackgroundOpacity(safeOpacity);
    box->setGlassEffect(glass);
    
    // Connect to BoxManager and ListView
    BoxManager::instance()->setupConnections(box, this);
    BoxManager::instance()->addBox(box);
    
    box->setGeometry(safeGeometry);
    box->setShowType(safeShowType);
    if (collapsed) box->toggleCollapse();

    box->show();
    box->setGlassEffect(glass); // Apply blur AFTER window is fully visible to prevent black screen
    if (box->isFolderMapped()) {
        loadMappedFolderItemsForBox(box);
    }
    box->refreshLayout(); // Force initial tight packing
    box->raise();

    if (refresh) {
        loadIconLayer();
        update();
        requestSaveBoxes();
    }
}

void ListView::onMenuDestroyBox(LinBox* box) {
    if (!box) return;

    clearDestroyedBoxRuntimeState(box);

    if (box->isFolderMapped()) {
        if (LinDirectoryMonitor* monitor = box->mappedFolderMonitor()) {
            monitor->stopListening();
            box->setMappedFolderMonitor(nullptr);
        }
        clearMappedFolderItemsForBox(box);
        BoxManager::instance()->destroyBox(box, this);
        update();
        return;
    }

    RenameManager::instance()->cancelRename();
    resetRenameClickState();
    const QVector<DesktopRootItemState> boxStates = box->itemStates();
    for (const DesktopRootItemState& boxedState : boxStates) {
        DesktopRootItemState restoredState = boxedState;
        resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
        if (!m_iconShowRule.autoArrange) {
            restoredState.model.position = generateItemPosition();
        }
        m_itemStates.push_back(restoredState);
        markDesktopStateIndexDirty();
    }
    
    BoxManager::instance()->destroyBox(box, this);
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeAfterDesktopMutation(true, false);
    } else {
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
        requestSaveBoxes();
    }
}

void ListView::clearDestroyedBoxRuntimeState(LinBox* box)
{
    if (!box) {
        return;
    }

    const QString boxId = box->boxId();
    const QVector<DesktopRootItemState> currentStates = box->itemStates();
    QStringList pathsToClear;
    auto rememberPath = [this, &pathsToClear](const QString& path) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (!normalizedPath.isEmpty() && !pathsToClear.contains(normalizedPath)) {
            pathsToClear.append(normalizedPath);
        }
    };

    if (LinDirectoryMonitor* monitor = box->mappedFolderMonitor()) {
        monitor->stopListening();
        box->setMappedFolderMonitor(nullptr);
    }

    if (m_pendingShellNewTargetBox == box) {
        clearPendingShellNewTargetBox();
    }

    if (!boxId.isEmpty()) {
        const QStringList mappedPaths = m_mappedBoxFilePaths.value(boxId);
        for (const QString& mappedPath : mappedPaths) {
            rememberPath(mappedPath);
        }
        m_mappedBoxLoadRequestIds.remove(boxId);
        m_mappedBoxFilePaths.remove(boxId);
    }

    for (auto it = m_pendingCreatedItemBoxes.begin(); it != m_pendingCreatedItemBoxes.end(); ) {
        if (it.value() == box) {
            m_pendingCreatedItemInsertIndices.remove(it.key());
            it = m_pendingCreatedItemBoxes.erase(it);
            continue;
        }
        ++it;
    }

    for (const DesktopRootItemState& state : currentStates) {
        rememberPath(state.model.filePath);
    }

    for (const QString& normalizedPath : pathsToClear) {
        m_selectedBoxPaths.removeAll(normalizedPath);
        if (m_focusedBoxPath == normalizedPath) {
            m_focusedBoxPath.clear();
        }
        if (m_anchorBoxPath == normalizedPath) {
            m_anchorBoxPath.clear();
        }
        if (m_lastClickedBoxPath == normalizedPath) {
            m_lastClickedBoxPath.clear();
        }
        if (m_draggedBoxPath == normalizedPath) {
            m_draggedBoxPath.clear();
        }
    }
}

void ListView::onMenuCustomBox(LinBox* box) {
    // Placeholder for color customizations
    qDebug() << "Customize box requested:" << box->boxTitle();
}

void ListView::moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh, int insertIndex) {
    if (!targetBox || filePath.isEmpty()) {
        return;
    }

    LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(filePath);

    DesktopRootItemState stateToMove;
    bool hasStateToMove = false;

    if (DesktopRootItemState* desktopState = findDesktopStateByPath(filePath)) {
        stateToMove = *desktopState;
        stateToMove.model.boxId = targetBox->boxId();
        stateToMove.model.visible = true;
        hasStateToMove = true;
        removeDesktopStateByPath(filePath);
    } else if (sourceBox) {
        DesktopRootItemState boxedState;
        if (sourceBox->takeItemState(filePath, boxedState, false)) {
            stateToMove = boxedState;
            stateToMove.model.boxId = targetBox->boxId();
            stateToMove.model.visible = true;
            hasStateToMove = true;
        }
    }

    if (!hasStateToMove) {
        return;
    }

    if (insertIndex >= 0) {
        targetBox->insertItemState(stateToMove, insertIndex, false);
    } else {
        targetBox->addItemState(stateToMove, false);
    }

    if (sourceBox && sourceBox != targetBox) {
        sourceBox->refreshLayout();
        sourceBox->update();
    }

    if (targetBox) {
        targetBox->refreshLayout();
        targetBox->update();
        targetBox->raise();
    }
    
    if (refresh) {
        updateAllItemRenderData();
        update();
        requestSaveBoxes();
    }
    
    qDebug() << "Moved item to box (re-parented):" << filePath << "->" << targetBox->boxTitle() << "index=" << insertIndex;
}

void ListView::moveItemsToBox(const QStringList& filePaths, LinBox* targetBox, int insertIndex, bool refresh)
{
    if (!targetBox) {
        return;
    }

    const QStringList orderedPaths = orderedDraggedPaths(filePaths);
    const QStringList pathsToMove = orderedPaths.isEmpty() ? filePaths : orderedPaths;
    for (int i = 0; i < pathsToMove.size(); ++i) {
        const int targetIndex = insertIndex >= 0 ? insertIndex + i : -1;
        moveItemToBox(pathsToMove.at(i), targetBox, false, targetIndex);
    }

    targetBox->refreshLayout();
    targetBox->update();
    targetBox->raise();

    if (refresh) {
        update();
        requestSaveBoxes();
    }
}

QStringList ListView::orderedDraggedPaths(const QStringList& paths) const
{
    QStringList resolvedPaths;
    QSet<QString> seenPaths;
    for (const QString& path : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath) || !findAnyItemStateByPath(normalizedPath)) {
            continue;
        }
        seenPaths.insert(normalizedPath);
        resolvedPaths.append(normalizedPath);
    }

    if (resolvedPaths.isEmpty()) {
        return resolvedPaths;
    }

    QSet<QString> sourceBoxIds;
    bool hasDesktopItems = false;
    for (const QString& path : resolvedPaths) {
        if (const DesktopRootItemState* state = findAnyItemStateByPath(path)) {
            if (state->model.boxId.isEmpty()) {
                hasDesktopItems = true;
            } else {
                sourceBoxIds.insert(state->model.boxId);
            }
        }
    }

    QStringList orderedPaths;
    if (sourceBoxIds.size() == 1 && !hasDesktopItems) {
        if (LinBox* sourceBox = BoxManager::instance()->findBoxById(*sourceBoxIds.begin())) {
            for (const DesktopRootItemState& state : sourceBox->itemStates()) {
                const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
                if (seenPaths.contains(normalizedPath)) {
                    orderedPaths.append(normalizedPath);
                }
            }
        }
    } else if (sourceBoxIds.isEmpty() && hasDesktopItems) {
        QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
        for (DesktopRootItemState* state : orderedStates) {
            if (!state) {
                continue;
            }
            const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
            if (seenPaths.contains(normalizedPath)) {
                orderedPaths.append(normalizedPath);
            }
        }
    }

    if (!orderedPaths.isEmpty()) {
        return orderedPaths;
    }
    return resolvedPaths;
}

bool ListView::importPathsToBox(const QStringList& sourcePaths, LinBox* targetBox, bool copyMode, int insertIndex, bool preferShortcut)
{
    if (sourcePaths.isEmpty() || !targetBox) {
        return false;
    }

    if (targetBox->isFolderMapped()) {
        if (targetBox->mappedFolderState() != LinBox::MappedFolderState::Normal) {
            return false;
        }

        const QString targetFolder = normalizeMappedFolderPathForLookup(targetBox->mappedFolderPath());
        if (targetFolder.isEmpty()) {
            return false;
        }

        std::vector<std::wstring> srcPaths;
        for (const QString& sourcePath : sourcePaths) {
            if (!sourcePath.isEmpty()) {
                srcPaths.push_back(QDir::toNativeSeparators(sourcePath).toStdWString());
            }
        }
        if (srcPaths.empty()) {
            return false;
        }

        if (preferShortcut) {
            return createShortcutsInFolder(sourcePaths, targetFolder);
        }
        if (copyMode) {
            return CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetFolder).toStdWString());
        }
        return CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetFolder).toStdWString());
    }

    bool hasMappedSource = false;
    for (const QString& sourcePath : sourcePaths) {
        if (isPathFromMappedBox(sourcePath)) {
            hasMappedSource = true;
            break;
        }
    }

    if (hasMappedSource && !copyMode && !preferShortcut) {
        QStringList movedSourcePaths;
        QStringList createdPaths;
        int nextInsertIndex = insertIndex;
        for (const QString& sourcePath : sourcePaths) {
            if (sourcePath.isEmpty()) {
                continue;
            }

            const QString destinationPath =
                ListViewFileOpsHelper::uniquePasteDestinationPath(sourcePath, m_desktopDir);
            const QString normalizedDestination = normalizeDesktopPath(destinationPath);
            if (normalizedDestination.isEmpty()) {
                continue;
            }

            if (!ListViewFileOpsHelper::movePathToExactDestination(sourcePath, destinationPath)) {
                continue;
            }

            if (!addItemNoDelete(normalizedDestination)) {
                continue;
            }

            moveItemToBox(normalizedDestination, targetBox, false, nextInsertIndex);
            movedSourcePaths.append(sourcePath);
            createdPaths.append(normalizedDestination);
            if (nextInsertIndex >= 0) {
                ++nextInsertIndex;
            }
        }

        if (movedSourcePaths.isEmpty()) {
            return false;
        }

        finalizeMovedModelSourcesAfterFileOp(movedSourcePaths);
        targetBox->refreshLayout();
        targetBox->update();
        targetBox->raise();
        requestSaveBoxes();
        return !createdPaths.isEmpty();
    }

    bool ok = true;
    QStringList createdPaths;

    int nextInsertIndex = insertIndex;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }

        DesktopRootItemModel sourceModel;
        const bool hasSourceModel = resolveItemModelByPath(sourcePath, sourceModel);
        const bool isShortcutOnlySource =
            hasSourceModel &&
            (sourceModel.itemData.isRealSpecialFolderIcon() ||
             sourceModel.objectType == ObjectType::DiskLetter ||
             sourceModel.filePath.startsWith(QStringLiteral("::")));

        if (copyMode && !preferShortcut && isShortcutOnlySource) {
            ok = false;
            continue;
        }

        const QString destinationPath = preferShortcut
            ? ListViewFileOpsHelper::uniqueShortcutPathForTarget(sourcePath, m_desktopDir)
            : ListViewFileOpsHelper::uniquePasteDestinationPath(sourcePath, m_desktopDir);
        const QString normalizedDestination = normalizeDesktopPath(destinationPath);

        bool singleOk = false;
        if (preferShortcut) {
            singleOk = ListViewFileOpsHelper::createShellShortcut(sourcePath, destinationPath);
        } else if (copyMode) {
            singleOk = ListViewFileOpsHelper::copyPathToExactDestination(sourcePath, destinationPath);
        } else {
            singleOk = ListViewFileOpsHelper::movePathToExactDestination(sourcePath, destinationPath);
        }

        if (singleOk) {
            createdPaths.append(normalizedDestination);
            m_pendingCreatedItemBoxes[normalizedDestination] = targetBox;
            if (nextInsertIndex >= 0) {
                m_pendingCreatedItemInsertIndices[normalizedDestination] = nextInsertIndex++;
            }
        } else {
            ok = false;
        }
    }

    return ok && !createdPaths.isEmpty();
}

QStringList ListView::resolveDraggedLocalPaths(const QMimeData* mimeData) const
{
    return ListViewFileOpsHelper::draggedLocalFilePaths(mimeData);
}

bool ListView::resolveItemModelByPath(const QString& filePath, DesktopRootItemModel& modelOut) const
{
    if (const DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        modelOut = state->model;
        return true;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (const DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            modelOut = state->model;
            return true;
        }
    }

    return false;
}

QRect ListView::globalRenameEditorRectForPath(const QString& filePath) const
{
    if (const DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        return QRect(mapToGlobal(state->model.renderData.renameEditorRect.topLeft()),
                     state->model.renderData.renameEditorRect.size());
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return box->globalRenameEditorRectForPath(filePath);
    }

    return QRect();
}

bool ListView::sendPreviewRequestByPath(const QString& filePath)
{
    QString errorMessage;
    const bool ok = SpaceLookIpcClient::sendPreviewRequest(filePath, &errorMessage);
    if (!ok) {
        qWarning() << "Failed to send SpaceLook preview request:" << errorMessage;
    }
    return ok;
}

bool ListView::openItemByPath(const QString& filePath)
{
    DesktopRootItemModel model;
    if (!resolveItemModelByPath(filePath, model)) {
        return false;
    }
    clearCutMarks();
    return openDesktopRootItem(model);
}

bool ListView::showPropertiesForPath(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(filePath);
    CSystemApi::File_RightMenuOperations(reinterpret_cast<const TCHAR*>(nativePath.utf16()), Z_PROPERTIES);
    return true;
}

bool ListView::handleDroppedPathsOnModel(const QStringList& sourcePaths, const DesktopRootItemModel& targetModel, bool copyMode, bool preferShortcut)
{
    if (sourcePaths.isEmpty()) {
        return false;
    }

    const QString targetPath = targetModel.filePath;
    const QString clsid = targetModel.itemData.getCLSID();

    if (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS) {
        return false;
    }

    std::vector<std::wstring> srcPaths;
    QStringList args;
    for (const QString& localPath : sourcePaths) {
        if (!localPath.isEmpty()) {
            const QString nativePath = QDir::toNativeSeparators(localPath);
            args << "\"" + nativePath + "\"";
            srcPaths.push_back(nativePath.toStdWString());
        }
    }

    if (srcPaths.empty()) {
        return false;
    }

    const bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
    if (isRecycleBin) {
        const QStringList orderedPaths = orderedDraggedPaths(sourcePaths);
        if (!orderedPaths.isEmpty()) {
            setItemPathsTransientHidden(orderedPaths, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const bool deleteOk = CSystemApi::DeleteFiles(srcPaths, true, true);
        if (!deleteOk) {
            if (!orderedPaths.isEmpty()) {
                setItemPathsTransientHidden(orderedPaths, false);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        }

        if (!orderedPaths.isEmpty()) {
            trackPendingExternalRemovalPaths(orderedPaths);
            removeItemPathsBatch(orderedPaths);
            requestSaveBoxes();
        }
        return true;
    }

    if (targetModel.objectType == ObjectType::Folder_ObjectType ||
        targetModel.objectType == ObjectType::SpecialFolder ||
        targetModel.objectType == ObjectType::DiskLetter) {
        if (preferShortcut) {
            return createShortcutsInFolder(sourcePaths, targetPath);
        }
        if (copyMode) {
            return CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetPath).toStdWString());
        }
        return CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetPath).toStdWString());
    }

    std::wstring wApp = QDir::toNativeSeparators(targetPath).toStdWString();
    std::wstring wArgs = args.join(" ").toStdWString();
    HINSTANCE result = ShellExecuteW(NULL, L"open", wApp.c_str(), wArgs.c_str(), NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

void ListView::moveDesktopGhostsForPreview(const QPoint& globalPos)
{
    if (m_ghosts.isEmpty()) {
        return;
    }

    auto moveGhostsRaw = [&]() {
        for (GhostInfo& ghost : m_ghosts) {
            if (ghost.widget) {
                ghost.widget->move(globalPos + ghost.offset);
            }
        }
    };

    if (m_iconShowRule.autoArrange) {
        moveGhostsRaw();
        return;
    }

    const QPoint localPos = mapFromGlobal(globalPos);
    const DesktopRootItemState* hoveredState = findDesktopStateAt(localPos);
    if (hoveredState && !isDesktopPathSelected(hoveredState->model.filePath)) {
        moveGhostsRaw();
        return;
    }

    for (LinBox* box : boxes()) {
        if (!box) {
            continue;
        }
        const QRect boxGlobalRect(box->mapToGlobal(QPoint(0, 0)), box->size());
        if (boxGlobalRect.contains(globalPos)) {
            moveGhostsRaw();
            return;
        }
    }

    QStringList draggedPaths;
    QHash<QString, QPoint> previewPositions;
    bool overlapsComponent = false;
    const int itemHeight = desktopItemFootprintHeight();
    const QSize itemSize(m_iconWidth, itemHeight);

    for (const GhostInfo& ghost : m_ghosts) {
        if (!ghost.widget || ghost.filePath.isEmpty()) {
            continue;
        }
        if (!draggedPaths.contains(ghost.filePath)) {
            draggedPaths.append(ghost.filePath);
        }

        const QPoint ghostLocalPos = mapFromGlobal(globalPos + ghost.offset);
        const QString normalizedPath = normalizeDesktopItemPath(ghost.filePath);
        if (!normalizedPath.isEmpty()) {
            previewPositions.insert(normalizedPath, ghostLocalPos);
        }

        const QRect ghostRect(ghostLocalPos, itemSize);
        if (rectIntersectsDesktopBlockedArea(ghostRect)) {
            overlapsComponent = true;
            continue;
        }

        if (m_iconShowRule.gridAlignment) {
            const int strideX = qMax(1, m_iconWidth + m_colSpacing);
            const int strideY = qMax(1, itemHeight + m_rowSpacing);
            const int layoutLeft = desktopLayoutLeft();
            const int layoutTop = desktopLayoutTop();
            const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
            const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));
            const QPoint cell = pointToGridCell(ghostLocalPos, layoutLeft, layoutTop, strideX, strideY);
            const int cellX = qBound(0, cell.x(), maxCols - 1);
            const int cellY = qBound(0, cell.y(), maxRows - 1);
            if (desktopBlockedOccupiedCells().contains(packGridCell(cellX, cellY))) {
                overlapsComponent = true;
            }
        }
    }

    if (!overlapsComponent || draggedPaths.isEmpty()) {
        moveGhostsRaw();
        return;
    }

    const QMap<QString, QPoint> resolvedPositions = resolveDesktopDragFinalPositions(draggedPaths, localPos, previewPositions);
    if (resolvedPositions.isEmpty()) {
        moveGhostsRaw();
        return;
    }

    for (GhostInfo& ghost : m_ghosts) {
        if (!ghost.widget || ghost.filePath.isEmpty()) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(ghost.filePath);
        if (!resolvedPositions.contains(normalizedPath)) {
            continue;
        }
        ghost.widget->move(mapToGlobal(resolvedPositions.value(normalizedPath)));
    }
}

void ListView::updateGhostDragHoverPreview(const QPoint& globalPos)
{
    const QStringList draggedPaths = orderedDraggedPaths(selectedDesktopPaths());
    if (draggedPaths.isEmpty()) {
        for (LinBox* box : boxes()) {
            if (box) {
                box->clearDropPreview();
            }
        }
        if (m_dragTip) {
            m_dragTip->hide();
        }
        return;
    }

    LinBox* hoveredBox = nullptr;
    for (LinBox* box : boxes()) {
        if (!box) {
            continue;
        }

        const QRect boxGlobalRect(box->mapToGlobal(QPoint(0, 0)), box->size());
        if (boxGlobalRect.contains(globalPos)) {
            hoveredBox = box;
            const QPoint localPos = box->mapFromGlobal(globalPos);
            box->previewGhostDropAt(localPos, draggedPaths, QApplication::keyboardModifiers());
        } else {
            box->clearDropPreview();
        }
    }

    if (hoveredBox) {
        if (m_dragTip) {
            m_dragTip->hide();
        }
        return;
    }

    for (LinBox* box : boxes()) {
        if (box) {
            box->clearDropPreview();
        }
    }

    const QPoint localPos = mapFromGlobal(globalPos);
    if (!rect().contains(localPos)) {
        if (m_dragTip) {
            m_dragTip->hide();
        }
        return;
    }

    QMimeData mimeData;
    QList<QUrl> urls;
    for (const QString& path : draggedPaths) {
        if (!path.isEmpty()) {
            urls.append(QUrl::fromLocalFile(path));
        }
    }
    mimeData.setUrls(urls);

    QDragMoveEvent previewEvent(localPos, Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, &mimeData, Qt::LeftButton, QApplication::keyboardModifiers());
    dragMoveEvent(&previewEvent);
}

bool ListView::finalizeMovedModelSourcesAfterFileOp(const QStringList& sourcePaths)
{
    if (sourcePaths.isEmpty()) {
        return false;
    }

    QSet<LinBox*> touchedMappedBoxes;
    bool removedAny = false;
    for (const QString& sourcePath : sourcePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(sourcePath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (findDesktopStateByPath(normalizedPath)) {
            removeDesktopStateByPath(normalizedPath);
            removedAny = true;
            continue;
        }

        LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(normalizedPath);
        if (!sourceBox) {
            continue;
        }

        if (sourceBox->isFolderMapped()) {
            touchedMappedBoxes.insert(sourceBox);
            continue;
        }

        if (sourceBox->removeItemState(normalizedPath, false)) {
            sourceBox->refreshLayout();
            sourceBox->update();
            removedAny = true;
        }
    }

    for (LinBox* mappedBox : touchedMappedBoxes) {
        if (!mappedBox) {
            continue;
        }
        reloadMappedFolderBox(mappedBox);
        mappedBox->refreshLayout();
        mappedBox->update();
        removedAny = true;
    }

    if (removedAny) {
        rebuildSpatialIndex();
        update();
        requestSaveBoxes();
    }
    return removedAny;
}

bool ListView::handleDroppedPathsOnItemTarget(const QStringList& sourcePaths,
                                              const DesktopRootItemModel& targetModel,
                                              const DropInteractionOptions& options)
{
    if (sourcePaths.isEmpty()) {
        return false;
    }

    const QString clsid = targetModel.itemData.getCLSID();
    if (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS) {
        return false;
    }

    const bool targetConsumesSources =
        !options.copyMode &&
        !options.preferShortcut &&
        (clsid == APP_RECYCLE_BIN_CLS ||
         targetModel.objectType == ObjectType::Folder_ObjectType ||
         targetModel.objectType == ObjectType::SpecialFolder ||
         targetModel.objectType == ObjectType::DiskLetter);

    const QStringList orderedPaths = options.preHideDesktopSources
        ? orderedDraggedPaths(sourcePaths)
        : QStringList();

    if (clsid == APP_RECYCLE_BIN_CLS) {
        if (options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            setItemPathsTransientHidden(orderedPaths, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const bool ok = handleDroppedPathsOnModel(sourcePaths, targetModel, options.copyMode, options.preferShortcut);
        if (!ok) {
            if (options.preHideDesktopSources && !orderedPaths.isEmpty()) {
                setItemPathsTransientHidden(orderedPaths, false);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        }

        if (options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            trackPendingExternalRemovalPaths(orderedPaths);
            removeItemPathsBatch(orderedPaths);
            requestSaveBoxes();
        } else if (options.finalizeMovedSourcesAfterSuccess && targetConsumesSources) {
            finalizeMovedModelSourcesAfterFileOp(sourcePaths);
        }
        return true;
    }

    if (targetModel.objectType == ObjectType::Folder_ObjectType ||
        targetModel.objectType == ObjectType::SpecialFolder ||
        targetModel.objectType == ObjectType::DiskLetter) {
        if (targetConsumesSources && options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            setItemPathsTransientHidden(orderedPaths, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const bool ok = handleDroppedPathsOnModel(sourcePaths, targetModel, options.copyMode, options.preferShortcut);
        if (!ok) {
            if (targetConsumesSources && options.preHideDesktopSources && !orderedPaths.isEmpty()) {
                setItemPathsTransientHidden(orderedPaths, false);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        }

        if (targetConsumesSources && options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            trackPendingExternalRemovalPaths(orderedPaths);
            removeItemPathsBatch(orderedPaths);
            requestSaveBoxes();
        } else if (options.finalizeMovedSourcesAfterSuccess && targetConsumesSources) {
            finalizeMovedModelSourcesAfterFileOp(sourcePaths);
        }
        return true;
    }

    return handleDroppedPathsOnModel(sourcePaths, targetModel, options.copyMode, options.preferShortcut);
}

bool ListView::handleDroppedPathsOnDesktopTarget(const QStringList& sourcePaths,
                                                 const DesktopRootItemModel& targetModel,
                                                 bool copyMode,
                                                 bool preferShortcut,
                                                 bool preHideDesktopSources)
{
    DropInteractionOptions options;
    options.copyMode = copyMode;
    options.preferShortcut = preferShortcut;
    options.preHideDesktopSources = preHideDesktopSources;
    return handleDroppedPathsOnItemTarget(sourcePaths, targetModel, options);
}

bool ListView::shouldCreateShortcutForExternalDrop(const QStringList& sourcePaths) const
{
    for (const QString& sourcePath : sourcePaths) {
        if (ListViewFileOpsHelper::shouldCreateShortcutForExternalPath(sourcePath)) {
            return true;
        }
    }
    return false;
}

bool ListView::shouldPreferShortcutForDrop(const QStringList& sourcePaths,
                                           Qt::KeyboardModifiers modifiers,
                                           bool isExternalSource) const
{
    if (modifiers & Qt::AltModifier) {
        return true;
    }

    if (!isExternalSource) {
        return false;
    }

    return shouldCreateShortcutForExternalDrop(sourcePaths);
}

bool ListView::createShortcutsInFolder(const QStringList& sourcePaths, const QString& targetFolder) const
{
    if (sourcePaths.isEmpty() || targetFolder.isEmpty()) {
        return false;
    }

    bool ok = true;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }
        const QString shortcutPath = ListViewFileOpsHelper::uniqueShortcutPathForTarget(sourcePath, targetFolder);
        ok = ListViewFileOpsHelper::createShellShortcut(sourcePath, shortcutPath) && ok;
    }

    return ok;
}

void ListView::reloadMappedFolderBox(LinBox* box)
{
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);
    loadMappedFolderItemsForBox(box);
    selectionSnapshot.restore(*this, false);
}

void ListView::clearMappedFolderBox(LinBox* box)
{
    clearMappedFolderItemsForBox(box);
}

bool ListView::isItemInAnyBox(const QString& filePath) const {
    return BoxManager::instance()->isItemInAnyBox(filePath);
}

QString ListView::getBoxIdForItem(const QString& filePath) const {
    return BoxManager::instance()->getBoxIdForItem(filePath);
}

LinBox* ListView::findMappedFolderBox(const QString& folderPath, const LinBox* excludeBox) const
{
    return ListViewMappedBoxHelper::findMappedFolderBox(*this, folderPath, excludeBox);
}

QString ListView::dumpAllItemsJson() const
{
    return DesktopItemLoader::dumpDesktopRootStatesJson(m_itemStates);
}

QString ListView::dumpVisibleItemsJson() const
{
    QVector<DesktopRootItemState> visibleStates;
    for (const auto& state : m_itemStates) {
        if (shouldPaintDesktopModelState(state)) {
            visibleStates.push_back(state);
        }
    }
    return DesktopItemLoader::dumpDesktopRootStatesJson(visibleStates);
}

QString ListView::dumpHiddenItemsJson() const
{
    QVector<DesktopRootItemState> hiddenStates;
    for (const auto& state : m_itemStates) {
        if (!state.model.visible || !state.model.boxId.isEmpty()) {
            hiddenStates.push_back(state);
        }
    }
    return DesktopItemLoader::dumpDesktopRootStatesJson(hiddenStates);
}

void ListView::setShortcutArrowVisible(bool visible) {
    m_iconShowRule.showShortCutArrow = visible;
    
    markAllDesktopRenderDataDirty();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    requestSaveBoxes();
}

void ListView::loadPersistentLayout() {
    const bool useConfigLayout = m_isLoadingPersistence || m_forceConfigLayout;
    {
        QSet<LinBox*> boxesToRefresh;

        if (useConfigLayout) {
            for (auto& state : m_itemStates) {
                if (!state.model.boxId.isEmpty()) {
                    continue;
                }

                const QString normalizedPath = normalizeDesktopPath(state.model.filePath);
                if (!m_savedIconPositions.contains(normalizedPath)) {
                    state.model.position = QPoint(0, 0);
                }
            }
        }

        for (int i = 0; i < m_itemStates.size();) {
            DesktopRootItemState& state = m_itemStates[i];
            const QString savedBoxId = state.model.boxId;
            if (!savedBoxId.isEmpty()) {
                if (LinBox* targetBox = BoxManager::instance()->findBoxById(savedBoxId)) {
                    DesktopRootItemState boxedState = state;
                    boxedState.model.boxId = savedBoxId;
                    boxedState.model.visible = true;
                    targetBox->addItemState(boxedState, false);
                    m_itemStates.removeAt(i);
                    markDesktopStateIndexDirty();
                    boxesToRefresh.insert(targetBox);
                    continue;
                }
            }

            const QPoint currentPos = state.model.position;
            const QString normalizedPath = normalizeDesktopPath(state.model.filePath);
            const bool hasPersistedPosition = !useConfigLayout || m_savedIconPositions.contains(normalizedPath);
            const bool hasValidPosition = hasPersistedPosition && (currentPos != QPoint(0, 0));

            if (!hasValidPosition) {
                QPoint pos = useConfigLayout ? generateItemPosition() : currentPos;
                if (pos == QPoint(0, 0)) {
                    pos = generateItemPosition();
                }
                state.model.position = pos;
                ++i;
                continue;
            }

            const QPoint savedPos = state.model.position;
            if (savedPos.x() <= 0 || savedPos.y() <= 0) {
                state.model.position = generateItemPosition();
            } else {
                state.model.position = savedPos;
            }
            ++i;
        }

        for (LinBox* box : boxesToRefresh) {
            if (!box) {
                continue;
            }
            box->refreshLayout();
            box->raise();
        }

        markAllDesktopRenderDataDirty();
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
        return;
    }
}
void ListView::setPersistenceEnabled(bool enabled) {
    if (m_enablePersistence == enabled) return;
    m_enablePersistence = enabled;
    
    if (m_enablePersistence) {
        loadBoxes(false); // Don't let the file turn us back off!
        loadIconLayer();
    } else {
        m_hasLoadedConfigLayout = false;
        // Destroy all boxes and restore items to desktop
        for (auto box : BoxManager::instance()->allBoxes()) {
            if (!box) continue;
            const QVector<DesktopRootItemState> boxStates = box->itemStates();
            for (const DesktopRootItemState& boxedState : boxStates) {
                DesktopRootItemState restoredState = boxedState;
                resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
                m_itemStates.push_back(restoredState);
                markDesktopStateIndexDirty();
            }
            box->deleteLater();
        }
        BoxManager::instance()->clearBoxes();
        loadIconLayer();
    }
    
    // Always commit state change to file
    saveBoxes();

    if (!enabled) {
        FluentDialog* dlg = new FluentDialog(nullptr);
        dlg->setDialogTitle(tr("Desktop Organization"));
        dlg->setMessage(tr("Do you want to organize the current desktop icon layout now?"));
        dlg->addActionButton(tr("Organize Now"), FluentDialog::ActionType::Primary, [this]() {
            this->setPersistenceEnabled(true);
            this->onMenuOrganizeDesktop();
        });
        dlg->addActionButton(tr("Cancel"), FluentDialog::ActionType::Close);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    }
}

void ListView::updateRightMenuState() {
    if (!rightMenu) return;
    rightMenu->setLargeIconsChecked(m_iconShowRule.viewMode == ViewMode::LargeIcon);
    rightMenu->setMediumIconsChecked(m_iconShowRule.viewMode == ViewMode::MediumIcon);
    rightMenu->setSmallIconsChecked(m_iconShowRule.viewMode == ViewMode::SmallIcon);
    
    rightMenu->setShowDesktopIconsChecked(m_iconShowRule.showDesktopIcons);
    rightMenu->setAutoArrangeIconsChecked(m_iconShowRule.autoArrange);
    rightMenu->setAlignIconsToGridChecked(m_iconShowRule.gridAlignment);
    rightMenu->setDoubleClickedHideIconsChecked(m_iconShowRule.doubleClickHideIcons);
    
    const SortOrderType visibleSortType =
        (m_iconShowRule.autoArrange && m_iconShowRule.sortType == SortOrderType::SortByCustom)
            ? m_iconShowRule.sortTypeBeforeAutoArrange
            : m_iconShowRule.sortType;

    if (visibleSortType == SortOrderType::Default) {
        rightMenu->setSortByNameChecked(false);
        rightMenu->setSortBySizeChecked(false);
        rightMenu->setSortByTypeChecked(false);
        rightMenu->setSortByDateChecked(false);
    } else {
        rightMenu->setSortByNameChecked(visibleSortType == SortOrderType::SortByName_ASC || visibleSortType == SortOrderType::SortByName_DESC);
        rightMenu->setSortBySizeChecked(visibleSortType == SortOrderType::SortBySize_ASC || visibleSortType == SortOrderType::SortBySize_DESC);
        rightMenu->setSortByTypeChecked(visibleSortType == SortOrderType::SortByType_ASC || visibleSortType == SortOrderType::SortByType_DESC);
        rightMenu->setSortByDateChecked(visibleSortType == SortOrderType::SortByDateModified_ASC || visibleSortType == SortOrderType::SortByDateModified_DESC);
    }
}
