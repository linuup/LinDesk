#pragma once

#include "box/LinBox.h"
#include <QGraphicsOpacityEffect>
#include "services/AppPaths.h"
#include <QSettings>
#include "services/IconCacheManager.h"
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
#include <QGuiApplication>
#include <QScreen>
#include <QElapsedTimer>
#include <QTimer>
#include <QWindow>
#include <QStyleFactory>
#include <QFileInfo>
#include <QClipboard>
#include <QCursor>
#include <QMimeData>
#include <QDrag>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QtMath>
#include <QCoreApplication>
#include <QHash>
#include <limits>
#include <climits>
#include <vector>
#include "ui/SettingUI.h"
#include "desktop/ListView.h"
#include "desktop/LinDesktop.h"
#include "services/SysInfoUtil.h"
#include "desktop/FilenameEditor.h"
#include "desktop/DesktopRootItemState.h"
#include "app/AppFonts.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "box/BoxManager.h"
#include "desktop/RightMenu.h"
#include "ui/Fluent/FluentDialog.h"
#include "services/DpiScaleHelper.h"
#include "services/DpiCoordinateHelper.h"
#include "services/ShellMenuExclusionStore.h"
#include "services/WinBlurHelper.h"
#include "ui/Fluent/FluentDragTip.h"
#include "monitor/LinDirectoryMonitor.h"
#include "SystemApi/SystemApi.h"
#include <ShellContextMenu.h>
#include <QtWinExtras/QtWin>
#include <QSvgRenderer>

namespace {

int scaledPx(const QWidget* widget, int value)
{
    return DpiScaleHelper::scalePx(widget, value);
}

QString boxScrollAreaStyle(const QWidget* widget)
{
    const int scrollBarWidth = scaledPx(widget, 4);
    const int scrollBarRadius = qMax(1, scaledPx(widget, 2));
    const int scrollBarHandleMinHeight = scaledPx(widget, 20);
    return QString(
        "QScrollArea { background: transparent; border: none; } "
        "QScrollBar:vertical { width: %1px; background: transparent; margin: 0px 0px 0px 0px; } "
        "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 120); border-radius: %2px; min-height: %3px; } "
        "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 200); } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; } "
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; } ")
        .arg(scrollBarWidth)
        .arg(scrollBarRadius)
        .arg(scrollBarHandleMinHeight);
}

QString normalizeMappedFolderPath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path.trimmed());
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QPoint nativeCursorPosition()
{
    POINT cursor = {};
    GetCursorPos(&cursor);
    return QPoint(cursor.x, cursor.y);
}

QRect nativeWindowRect(HWND hwnd)
{
    RECT rect = {};
    if (!hwnd || !GetWindowRect(hwnd, &rect)) {
        return {};
    }

    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

void logBoxDpiTrace(const char* source, QWidget* widget, const QPoint& globalPos)
{
    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (!screen) {
        screen = widget ? widget->screen() : QApplication::primaryScreen();
    }

    const HWND hwnd = widget ? reinterpret_cast<HWND>(widget->winId()) : nullptr;
    qDebug() << "[DpiTrace][LinBox]"
             << "source=" << source
             << "qtGlobal=" << globalPos
             << "nativeGlobal=" << DpiCoordinateHelper::qtGlobalPointToNativePoint(hwnd, globalPos)
             << "qtCursor=" << QCursor::pos()
             << "nativeCursor=" << nativeCursorPosition()
             << "ownerHwnd=" << QString::number(reinterpret_cast<quintptr>(hwnd), 16)
             << "ownerRect=" << nativeWindowRect(hwnd)
             << "ownerDpi=" << (hwnd ? GetDpiForWindow(hwnd) : 0)
             << "widgetDpr=" << (widget ? widget->devicePixelRatioF() : 0.0)
             << "screenDpr=" << (screen ? screen->devicePixelRatio() : 0.0)
             << "screenGeometry=" << (screen ? screen->geometry() : QRect())
             << "screenAvailable=" << (screen ? screen->availableGeometry() : QRect());
}

QHash<QString, HBITMAP>& menuBitmapCache()
{
    static QHash<QString, HBITMAP> cache;
    return cache;
}

void clearMenuBitmapCache()
{
    QHash<QString, HBITMAP>& cache = menuBitmapCache();
    for (HBITMAP bitmap : cache) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
    }
    cache.clear();
}

HBITMAP cachedMenuBitmap(const QString& cacheKey, const QIcon& icon, const QWidget* widget)
{
    static bool cleanupRegistered = false;
    if (!cleanupRegistered) {
        qAddPostRoutine(clearMenuBitmapCache);
        cleanupRegistered = true;
    }

    QHash<QString, HBITMAP>& cache = menuBitmapCache();

    if (icon.isNull()) {
        return nullptr;
    }

    const QString key = cacheKey + QLatin1Char('|') + QString::number(scaledPx(widget, 16));
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }

    const int iconSize = scaledPx(widget, 16);
    const HBITMAP bitmap = QtWin::toHBITMAP(icon.pixmap(iconSize, iconSize), QtWin::HBitmapPremultipliedAlpha);
    cache.insert(key, bitmap);
    return bitmap;
}

constexpr int kBaseBoxBorderWidth = 8;
constexpr int kBaseBoxTitleHeight = 30;
constexpr int kBaseBoxMinWidth = 180;
constexpr int kBaseBoxMinHeight = 120;
constexpr int kBaseBoxDefaultWidth = 300;
constexpr int kBaseBoxDefaultHeight = 400;
constexpr int kBaseTitleLayoutLeft = 5;
constexpr int kBaseTitleLayoutRight = 5;
constexpr int kBaseTitleEditHeight = 22;
constexpr int kBaseTitleButtonSize = 24;
constexpr int kBaseTitleIconSize = 16;
constexpr int kBaseScrollBarWidth = 4;
constexpr int kBaseScrollBarHandleMinHeight = 20;
constexpr int kBaseItemWidth = 76;
constexpr int kBaseItemHeight = 78;
constexpr int kBaseItemListHeight = 30;
constexpr int kBaseItemMarginLeft = 5;
constexpr int kBaseItemMarginTop = 5;
constexpr int kBaseItemGap = 8;
constexpr int kBaseItemListPadding = 8;
constexpr int kBaseItemListRightPadding = 12;
constexpr int kBaseIndicatorLine = 2;
constexpr int kBaseIndicatorInset = 8;
constexpr int kBasePlaceholderInset = 16;
constexpr int kBaseDragTipOffset = 24;
constexpr int kBaseDragTipMargin = 8;

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

QString mappedFolderPlaceholderResourcePath(LinBox::MappedFolderState state, bool isEmpty)
{
    if (state == LinBox::MappedFolderState::Missing || state == LinBox::MappedFolderState::NoAccess) {
        return QStringLiteral(":/Assets/accessDeny-Cat.svg");
    }
    if (isEmpty) {
        return QStringLiteral(":/Assets/empty_Cat.svg");
    }
    return QString();
}

bool draggedPathsContainMappedSource(const QStringList& paths)
{
    for (const QString& path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(path)) {
            if (sourceBox->isFolderMapped()) {
                return true;
            }
        }
    }
    return false;
}

bool isDesktopOrBoxDragSource(const QObject* source)
{
    return qobject_cast<const ListView*>(source) != nullptr ||
        qobject_cast<const LinBox*>(source) != nullptr;
}

bool isRealSystemIconModel(const DesktopRootItemModel& model)
{
    return model.itemData.isRealSpecialFolderIcon();
}

bool isForbiddenDropTarget(const DesktopRootItemModel& model)
{
    const QString clsid = model.itemData.getCLSID();
    return clsid == APP_MY_COMPUTER_CLS ||
        clsid == APP_NETWORK_CLS ||
        clsid == APP_CONTROL_PANEL_CLS;
}

bool isRecycleBinDropTarget(const DesktopRootItemModel& model)
{
    return model.itemData.getCLSID() == APP_RECYCLE_BIN_CLS;
}

bool isFolderDropTarget(const DesktopRootItemModel& model)
{
    return model.objectType == ObjectType::Folder_ObjectType ||
        model.objectType == ObjectType::SpecialFolder ||
        model.objectType == ObjectType::DiskLetter;
}

bool shouldCreateShortcutForMappedFolderDrop(ListView* lv,
                                             bool isInternalDrag,
                                             const QStringList& orderedPaths,
                                             const QStringList& paths,
                                             Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::AltModifier) {
        return true;
    }

    if (isInternalDrag) {
        if (lv) {
            for (const QString& path : orderedPaths) {
                DesktopRootItemModel model;
                if (lv->resolveItemModelByPath(path, model) && isRealSystemIconModel(model)) {
                    return true;
                }
            }

            DesktopRootItemModel draggedModel;
            const QString draggedPath = lv->draggedItemPath();
            return !draggedPath.isEmpty() && lv->resolveItemModelByPath(draggedPath, draggedModel) && isRealSystemIconModel(draggedModel);
        }
        return false;
    }

    return lv && lv->shouldPreferShortcutForDrop(paths, modifiers, true);
}

Qt::KeyboardModifiers effectiveDragModifiers(Qt::KeyboardModifiers modifiers)
{
    Qt::KeyboardModifiers effective = modifiers | QGuiApplication::keyboardModifiers();

    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        effective |= Qt::ControlModifier;
    }
    if (GetAsyncKeyState(VK_MENU) & 0x8000) {
        effective |= Qt::AltModifier;
    }
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        effective |= Qt::ShiftModifier;
    }

    return effective;
}

bool areAllPathsDirectChildrenOfFolder(const QStringList& paths, const QString& folderPath)
{
    const QString normalizedFolder = QDir::cleanPath(QDir::fromNativeSeparators(folderPath));
    if (normalizedFolder.isEmpty() || paths.isEmpty()) {
        return false;
    }

    for (const QString& path : paths) {
        if (path.isEmpty()) {
            return false;
        }

        const QFileInfo info(path);
        const QString parentFolder = QDir::cleanPath(QDir::fromNativeSeparators(info.absolutePath()));
        if (parentFolder.compare(normalizedFolder, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }

    return true;
}

QString normalizeBoxItemPath(const QString& filePath)
{
    return ListView::normalizeDesktopItemPath(filePath);
}

DesktopRenderParams buildBoxRenderParams(const LinBox* box, LinBox::BoxShowType showType, int itemWidth, int itemHeight)
{
    DesktopRenderParams params;
    params.iconWidth = itemWidth;
    params.iconHeight = itemHeight;
    params.largeIconW = scaledPx(box, 100);
    params.smallIconW = scaledPx(box, 62);
    params.iconTextGap = (showType == LinBox::BoxShowType::List_ShowType) ? scaledPx(box, 4) : scaledPx(box, 8);
    params.dpiScale = DpiScaleHelper::uiScaleForWidget(box);
    params.devicePixelRatio = box ? qMax<qreal>(1.0, box->devicePixelRatioF()) : 1.0;
    params.labelFont = AppFonts::uiFont(9);
    params.showShortcutArrow = true;
    return params;
}

void translateRenderData(DesktopRootItemRenderData& renderData, const QPoint& delta)
{
    renderData.itemRect.translate(delta);
    renderData.iconRect.translate(delta);
    renderData.textRect.translate(delta);
    renderData.renameEditorRect.translate(delta);
    renderData.shortcutArrowRect.translate(delta);
    renderData.staticContentRect.translate(delta);
    renderData.selectionContentRect.translate(delta);
    renderData.outerHighlightRect.translate(delta);
    renderData.innerHighlightRect.translate(delta);
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

QStringList toNativeQStringListPaths(const QStringList& paths)
{
    QStringList nativePaths;
    nativePaths.reserve(paths.size());
    for (const QString& path : paths) {
        if (!path.isEmpty()) {
            nativePaths.append(QDir::toNativeSeparators(path));
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

    constexpr int kBaseDownsample = 5;
    constexpr int kWideDownsample = 8;
    constexpr int kBaseBlurRadius = 9;
    constexpr int kWideBlurRadius = 14;
    constexpr qreal kBaseBlendOpacity = 0.84;
    constexpr qreal kWideBlendOpacity = 0.24;
    constexpr int kExclusionAlpha = 10;
    constexpr int kScreenAlpha = 32;
    constexpr int kNoiseAlpha = 20;

    const qreal dpr = source.devicePixelRatio();
    const QImage sourceImage = source.toImage();
    const QSize originalSize = sourceImage.size();

    // Acrylic recipe from Microsoft guidance:
    // background, blur, exclusion blend, tint overlay, noise.
    // This local fallback keeps that ordering and adds a very light brightening pass
    // to approximate the transient Desktop Acrylic look.
    const QSize baseSampleSize(qMax(1, originalSize.width() / kBaseDownsample),
                               qMax(1, originalSize.height() / kBaseDownsample));
    QImage baseSample = sourceImage.scaled(baseSampleSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    baseSample = gaussianBlurImage(baseSample, kBaseBlurRadius);
    const QImage baseRestored = baseSample.scaled(originalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const QSize wideSampleSize(qMax(1, originalSize.width() / kWideDownsample),
                               qMax(1, originalSize.height() / kWideDownsample));
    QImage wideSample = sourceImage.scaled(wideSampleSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    wideSample = gaussianBlurImage(wideSample, kWideBlurRadius);
    QImage restored = wideSample.scaled(originalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPainter blurBlendPainter(&restored);
    blurBlendPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    blurBlendPainter.setOpacity(kBaseBlendOpacity);
    blurBlendPainter.drawImage(QPoint(0, 0), baseRestored);
    blurBlendPainter.setOpacity(kWideBlendOpacity);
    blurBlendPainter.drawImage(QPoint(0, 0), restored);
    blurBlendPainter.setCompositionMode(QPainter::CompositionMode_Screen);
    blurBlendPainter.setOpacity(0.18);
    blurBlendPainter.fillRect(restored.rect(), QColor(255, 255, 255, 20));
    blurBlendPainter.end();

    QPainter overlayPainter(&restored);
    overlayPainter.setRenderHint(QPainter::Antialiasing);

    overlayPainter.setCompositionMode(QPainter::CompositionMode_Exclusion);
    overlayPainter.fillRect(restored.rect(), QColor(244, 244, 244, kExclusionAlpha));

    overlayPainter.setCompositionMode(QPainter::CompositionMode_Screen);
    overlayPainter.fillRect(restored.rect(), QColor(255, 255, 255, kScreenAlpha));

    // Inference from the transient Desktop Acrylic appearance:
    // a subtle cool highlight keeps the material bright and translucent.
    QLinearGradient acrylicSheen(0, 0, 0, restored.height());
    acrylicSheen.setColorAt(0.0, QColor(236, 246, 255, 34));
    acrylicSheen.setColorAt(0.30, QColor(244, 249, 255, 16));
    acrylicSheen.setColorAt(1.0, QColor(255, 255, 255, 6));
    overlayPainter.fillRect(restored.rect(), acrylicSheen);

    overlayPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QColor softenedTint = blendTowardWhite(tintColor, 0.18);
    const int tintAlpha = qBound(0, qRound(qBound(0.0f, tintOpacity, 1.0f) * 56.0f), 56);
    overlayPainter.fillRect(restored.rect(), QColor(softenedTint.red(), softenedTint.green(), softenedTint.blue(), tintAlpha));
    overlayPainter.end();

    applyNoiseLayer(restored, kNoiseAlpha);

    if (dpr > 1.0) {
        restored.setDevicePixelRatio(dpr);
    }
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
