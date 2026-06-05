#pragma once
#include "desktop/ListView.h"
#include "desktop/ListViewSelectionHelper.h"
#include "desktop/ListViewSelectionController.h"
#include "desktop/ListViewDragDropHelper.h"
#include "desktop/LinDesktop.h"
#include "box/LinBox.h"
#include "box/BoxManager.h"
#include "app/AppFonts.h"
#include <QDir>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QDebug>
#include <QCursor>
#ifdef Q_OS_WIN
#include <Windows.h>
#endif


namespace {
constexpr double kLinWidgetCellBlockThreshold = 0.15;

QString normalizeDesktopPath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path);
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    int colonColonIndex = normalized.indexOf(QStringLiteral("::"));
    if (colonColonIndex > 0) {
        bool allSlashes = true;
        for (int i = 0; i < colonColonIndex; ++i) {
            if (normalized[i] != '\\' && normalized[i] != '/') {
                allSlashes = false;
                break;
            }
        }
        if (allSlashes) {
            normalized = normalized.mid(colonColonIndex);
        }
    }
    return normalized.toLower();
}

bool doesFileExistNoResolve(const QString& filePath)
{
    return CSystemApi::GetPathType(filePath) != InvalidPath;
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

