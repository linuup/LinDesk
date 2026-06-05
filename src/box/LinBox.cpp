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
#include <QGuiApplication>
#include <QScreen>
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
constexpr int kBaseTitleLayoutLeft = 10;
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
    QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(filePath));
    while (normalized.endsWith('/') || normalized.endsWith('\\')) {
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

DesktopRenderParams buildBoxRenderParams(const LinBox* box, LinBox::BoxShowType showType, int itemWidth, int itemHeight)
{
    DesktopRenderParams params;
    params.iconWidth = itemWidth;
    params.iconHeight = itemHeight;
    params.largeIconW = scaledPx(box, 100);
    params.smallIconW = scaledPx(box, 62);
    params.iconTextGap = (showType == LinBox::BoxShowType::List_ShowType) ? scaledPx(box, 4) : scaledPx(box, 8);
    params.dpiScale = DpiScaleHelper::uiScaleForWidget(box);
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

    const QSize originalSize = source.size();
    const QImage sourceImage = source.toImage();

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
    // 濠电偠鎻紞鈧繛澶嬫礋瀵偊藟缁?:SubWindow濠电偠鎻徊鍧楀棘缁涚磯闂備胶鎳撻悺銊╁礉閹烘梹瀚婚柣鎴犵亣stView闂備焦鐪归崝宀€鈧凹鍠栭敃銏ゅ灳閹颁焦妗ㄩ梺鎸庣箓閹虫劙骞楀鍥╃＜闁归偊鍙庡▓鏃傛喐閻楀牏娲撮柡浣哥Ф娴狅箓鎳栭埡鍐╁枓濠电偞娼欓崥瀣┍濞差亗鈧懘顢曢敂钘夊壆闂佸搫璇為崘銊х炕濠电偛顕慨鐢告偋閻愮儤瀚冮悗锝庡亞椤?
    setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("LinBox { background: transparent; }");
    setMouseTracking(true);
    
    updateDpiMetrics();
    setMinimumSize(m_minBoxWidth, m_minBoxHeight);
    resize(scaledPx(this, kBaseBoxDefaultWidth), scaledPx(this, kBaseBoxDefaultHeight));
    setAcceptDrops(true);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    m_mainLayout->setSpacing(0);

    this->m_title = tr("New Box");
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(m_titleHeight);
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setAttribute(Qt::WA_StyledBackground); 
    m_titleBar->setMouseTracking(true);
    
    QHBoxLayout* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(scaledPx(this, 10), 0, scaledPx(this, 5), 0);

    m_titleEdit = new QLineEdit(m_titleBar);
    m_titleEdit->setMouseTracking(true);
    m_titleEdit->setVisible(false);
    m_titleEdit->setText(m_title);
    m_titleEdit->setAlignment(Qt::AlignCenter);
    m_titleEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_titleEdit->setFixedHeight(scaledPx(this, 22));
    m_titleEdit->setStyleSheet(QString("background: white; color: black; border-radius: %1px; padding: %2px;")
        .arg(scaledPx(this, 4))
        .arg(scaledPx(this, 2)));

    m_viewSwitchBtn = new QPushButton(m_titleBar);
    m_viewSwitchBtn->setMouseTracking(true);
    m_viewSwitchBtn->setFixedSize(scaledPx(this, 24), scaledPx(this, 24));
    m_viewSwitchBtn->setIconSize(QSize(scaledPx(this, 16), scaledPx(this, 16)));
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
    iconFont.setPixelSize(scaledPx(this, 14));
    m_viewSwitchBtn->setStyleSheet("QPushButton { background: transparent; color: white; border: none; border-radius: 4px; } "
                                   "QPushButton:hover { background: rgba(255, 255, 255, 40); } "
                                   "QPushButton:pressed { background: rgba(255, 255, 255, 20); }");

    m_collapseBtn = new QPushButton(m_titleBar);
    m_collapseBtn->setMouseTracking(true);
    m_collapseBtn->setFixedSize(scaledPx(this, 24), scaledPx(this, 24));
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
    m_scrollArea->setMouseTracking(true);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(boxScrollAreaStyle(this));

    m_contentArea = new QWidget(m_scrollArea);
    m_contentArea->setObjectName("contentArea");
    m_contentArea->setAttribute(Qt::WA_StyledBackground);
    m_contentArea->setMouseTracking(true);
    m_scrollArea->setWidget(m_contentArea);
    if (m_scrollArea->viewport()) {
        m_scrollArea->viewport()->setMouseTracking(true);
    }

    m_mainLayout->addWidget(m_titleBar);
    m_mainLayout->addWidget(m_scrollArea);

    m_blurRefreshTimer = new QTimer(this);
    m_blurRefreshTimer->setSingleShot(true);
    m_blurRefreshTimer->setInterval(16);
    connect(m_blurRefreshTimer, &QTimer::timeout, this, &LinBox::updateBlurCache);
    m_mappedFolderStateTimer = new QTimer(this);
    m_mappedFolderStateTimer->setInterval(1200);
    connect(m_mappedFolderStateTimer, &QTimer::timeout, this, &LinBox::checkMappedFolderStateAndRefresh);
    m_mappedFolderStateTimer->start();

    updateDpiMetrics();

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
    m_titleEdit->installEventFilter(this);
    m_viewSwitchBtn->installEventFilter(this);
    m_collapseBtn->installEventFilter(this);
    m_scrollArea->installEventFilter(this);
    if (m_scrollArea->viewport()) {
        m_scrollArea->viewport()->installEventFilter(this);
    }
    m_contentArea->installEventFilter(this);
    raise(); 
}

void LinBox::updateDpiMetrics()
{
    m_dpiScale = DpiScaleHelper::uiScaleForWidget(this);
    m_borderWidth = scaledPx(this, kBaseBoxBorderWidth);
    m_titleHeight = scaledPx(this, kBaseBoxTitleHeight);
    m_minBoxWidth = scaledPx(this, kBaseBoxMinWidth);
    m_minBoxHeight = scaledPx(this, kBaseBoxMinHeight);

    if (m_mainLayout) {
        m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    }
    if (m_titleBar) {
        m_titleBar->setFixedHeight(m_titleHeight);
    }
    if (m_titleEdit) {
        m_titleEdit->setFixedHeight(scaledPx(this, kBaseTitleEditHeight));
    }
    if (m_viewSwitchBtn) {
        m_viewSwitchBtn->setFixedSize(scaledPx(this, kBaseTitleButtonSize), scaledPx(this, kBaseTitleButtonSize));
        m_viewSwitchBtn->setIconSize(QSize(scaledPx(this, kBaseTitleIconSize), scaledPx(this, kBaseTitleIconSize)));
    }
    if (m_collapseBtn) {
        m_collapseBtn->setFixedSize(scaledPx(this, kBaseTitleButtonSize), scaledPx(this, kBaseTitleButtonSize));
    }
    if (m_scrollArea) {
        m_scrollArea->setStyleSheet(boxScrollAreaStyle(this));
    }
}

LinBox::~LinBox() {
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
    }
}

void LinBox::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateDpiMetrics();
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
    markAllItemRenderDataDirty();
    refreshLayout();
    QTimer::singleShot(0, this, [this]() {
        m_lastLayoutContentWidth = -1;
        refreshLayout();
    });
    emit geometryChanged(this);
}

void LinBox::addItemState(const DesktopRootItemState& state, bool refresh)
{
    insertItemState(state, m_itemStates.size(), refresh);
}

void LinBox::insertItemState(const DesktopRootItemState& state, int index, bool refresh)
{
    if (state.model.filePath.isEmpty()) {
        return;
    }

    DesktopRootItemState stateCopy = state;
    stateCopy.model.boxId = m_id;
    stateCopy.model.visible = true;
    stateCopy.model.layoutMode = (m_showType == BoxShowType::List_ShowType)
        ? DesktopLayoutMode::ListMode
        : DesktopLayoutMode::IconMode;
    stateCopy.model.position = QPoint();
    stateCopy.model.baseSize = QSize();
    stateCopy.model.effectiveSize = QSize();
    stateCopy.model.visibleTextHeight = 0;
    stateCopy.model.renderData = DesktopRootItemRenderData();
    stateCopy.model.isHovered = false;
    stateCopy.renderDataDirty = true;

    removeItemState(stateCopy.model.filePath, false);
    const int clampedIndex = qBound(0, index, m_itemStates.size());
    m_itemStates.insert(clampedIndex, stateCopy);
    m_lastLayoutItemCount = -1;
    if (refresh) {
        refreshLayout();
        emit geometryChanged(this);
    }
}

bool LinBox::removeItemState(const QString& filePath, bool refresh)
{
    const QString normalizedPath = normalizeBoxItemPath(filePath);
    for (int i = 0; i < m_itemStates.size(); ++i) {
        if (normalizeBoxItemPath(m_itemStates[i].model.filePath) != normalizedPath) {
            continue;
        }
        m_itemStates.removeAt(i);
        if (refresh) {
            refreshLayout();
            emit geometryChanged(this);
        } else {
            m_lastLayoutItemCount = -1;
        }
        return true;
    }
    return false;
}

bool LinBox::takeItemState(const QString& filePath, DesktopRootItemState& stateOut, bool refresh)
{
    const QString normalizedPath = normalizeBoxItemPath(filePath);
    for (int i = 0; i < m_itemStates.size(); ++i) {
        if (normalizeBoxItemPath(m_itemStates[i].model.filePath) != normalizedPath) {
            continue;
        }
        stateOut = m_itemStates.takeAt(i);
        if (refresh) {
            refreshLayout();
            emit geometryChanged(this);
        } else {
            m_lastLayoutItemCount = -1;
        }
        return true;
    }
    return false;
}

QVector<QString> LinBox::itemPaths() const
{
    QVector<QString> paths;
    paths.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!state.model.filePath.isEmpty()) {
            paths.append(state.model.filePath);
        }
    }
    return paths;
}

bool LinBox::containsItemPath(const QString& filePath) const
{
    return findItemStateByPath(filePath) != nullptr;
}

QString LinBox::hoveredItemPath() const
{
    return m_hoveredItemPath;
}

void LinBox::markItemRenderDataDirty(DesktopRootItemState* state)
{
    if (!state) {
        return;
    }

    state->renderDataDirty = true;
}

void LinBox::markAllItemRenderDataDirty()
{
    for (DesktopRootItemState& state : m_itemStates) {
        state.renderDataDirty = true;
    }
}

bool LinBox::hasDirtyItemRenderData() const
{
    for (const DesktopRootItemState& state : m_itemStates) {
        if (state.renderDataDirty) {
            return true;
        }
    }
    return false;
}

quint64 LinBox::currentLayoutVisualSignature() const
{
    quint64 visualSignature = 1469598103934665603ULL;
    for (const DesktopRootItemState& state : m_itemStates) {
        const quint64 textHeight = static_cast<quint64>(qMax(0, state.model.visibleTextHeight));
        const quint64 expanded = state.model.isFullNameExpanded ? 0x9e3779b97f4a7c15ULL : 0ULL;
        const quint64 renaming = state.model.isRenaming ? 0xbf58476d1ce4e5b9ULL : 0ULL;
        const quint64 previewHash = static_cast<quint64>(qHash(state.model.expandedPreviewText));
        visualSignature ^= textHeight + expanded + renaming + previewHash + 0x9e3779b97f4a7c15ULL + (visualSignature << 6) + (visualSignature >> 2);
    }
    return visualSignature;
}

void LinBox::rebuildItemRenderData(DesktopRootItemState& state, const DesktopRenderParams& params)
{
    state.model.renderData = buildRenderDataFromModel(state.model, params);
    if (state.model.isRenaming) {
        const QString editorText = state.model.expandedPreviewText.isEmpty()
            ? state.model.displayName
            : state.model.expandedPreviewText;
        QRect editorRect = state.model.renderData.renameEditorRect;
        editorRect.setHeight(FilenameEditor::heightForText(
            editorText,
            state.model.renderData.labelFont,
            editorRect.width(),
            0));
        state.model.renderData.renameEditorRect = editorRect;
        state.model.renderData.selectionContentRect = editorRect;
        const qreal dpiScale = devicePixelRatioF();
        auto sp = [dpiScale](int value) {
            return qMax(1, qRound(value * dpiScale));
        };
        state.model.renderData.outerHighlightRect = editorRect.adjusted(-sp(3), -sp(3), sp(3), sp(3));
        state.model.renderData.innerHighlightRect = editorRect.adjusted(sp(1), sp(1), -sp(1), -sp(1));
        state.model.visibleTextHeight = QFontMetrics(state.model.renderData.labelFont).lineSpacing();
    } else {
        state.model.visibleTextHeight = qMax(1, state.model.renderData.selectionContentRect.height());
    }
    state.renderDataDirty = false;
}

void LinBox::refreshLayout() {
    if (m_isRefreshingLayout) return;
    const int itemW = scaledPx(this, kBaseItemWidth);
    const int itemH = scaledPx(this, kBaseItemHeight);
    const int colSpacing = scaledPx(this, kBaseItemGap);
    const int rowSpacing = scaledPx(this, kBaseItemGap);
    const int marginLeft = scaledPx(this, kBaseItemMarginLeft);
    const int marginTop = scaledPx(this, kBaseItemMarginTop);
    if (m_mainLayout) {
        m_mainLayout->activate();
    }
    if (m_scrollArea) {
        const bool renameActive = hasActiveItemRename();
        m_scrollArea->setVerticalScrollBarPolicy(renameActive ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
        if (QScrollBar* bar = m_scrollArea->verticalScrollBar()) {
            bar->setEnabled(!renameActive);
        }
        m_scrollArea->updateGeometry();
    }
    const int viewportW = (m_scrollArea && m_scrollArea->viewport()) ? m_scrollArea->viewport()->width() : 0;
    const int fallbackContentW = qMax(1, width() - (m_borderWidth * 2));
    const int contentW = qMax(1, viewportW > 0 ? viewportW : fallbackContentW);
    const quint64 visualSignature = currentLayoutVisualSignature();
    const bool layoutUnchanged =
        m_lastLayoutContentWidth == contentW &&
        m_lastLayoutItemCount == m_itemStates.size() &&
        m_lastLayoutShowType == m_showType &&
        m_lastLayoutVisualSignature == visualSignature;
    const bool hasDirtyRenderData = hasDirtyItemRenderData();

    if (m_itemStates.isEmpty()) {
        if (m_contentArea) {
            m_contentArea->setMinimumWidth(contentW);
            m_contentArea->setMinimumHeight(1);
            m_contentArea->resize(contentW, 1);
            m_contentArea->update();
        }
        m_lastLayoutContentWidth = contentW;
        m_lastLayoutItemCount = 0;
        m_lastLayoutShowType = m_showType;
        m_lastLayoutVisualSignature = visualSignature;
        return;
    }

    if (layoutUnchanged && !hasDirtyRenderData) {
        return;
    }

    m_isRefreshingLayout = true;
    bool needsFullLayout = !layoutUnchanged;

    if (m_showType == BoxShowType::Icon_ShowType) {
        DesktopRenderParams params = buildBoxRenderParams(this, m_showType, itemW, itemH);
        if (!needsFullLayout && hasDirtyRenderData) {
            for (DesktopRootItemState& state : m_itemStates) {
                if (!state.renderDataDirty) {
                    continue;
                }

                const int oldHeight = state.model.renderData.itemRect.height();
                const int oldVisibleTextHeight = state.model.visibleTextHeight;
                state.model.layoutMode = DesktopLayoutMode::IconMode;
                if (!state.model.effectiveSize.isValid()) {
                    state.model.effectiveSize = QSize(itemW, itemH);
                }
                rebuildItemRenderData(state, params);
                if (state.model.renderData.itemRect.height() != oldHeight ||
                    state.model.visibleTextHeight != oldVisibleTextHeight) {
                    needsFullLayout = true;
                    break;
                }
            }
        }

        const int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        const int rows = (m_itemStates.size() + cols - 1) / cols;
        QVector<int> rowHeights(rows, itemH);

        if (needsFullLayout) {
            for (int i = 0; i < m_itemStates.size(); ++i) {
                const int row = i / cols;
                DesktopRootItemState& state = m_itemStates[i];
                state.model.layoutMode = DesktopLayoutMode::IconMode;
                state.model.position = QPoint(0, 0);
                state.model.effectiveSize = QSize(itemW, itemH);
                rebuildItemRenderData(state, params);
                rowHeights[row] = qMax(rowHeights[row], state.model.renderData.itemRect.height());
            }

            QVector<int> rowTops(rows, marginTop);
            for (int row = 1; row < rows; ++row) {
                rowTops[row] = rowTops[row - 1] + rowHeights[row - 1] + rowSpacing;
            }

            for (int i = 0; i < m_itemStates.size(); ++i) {
                const int row = i / cols;
                const int col = i % cols;
                const int x = marginLeft + col * (itemW + colSpacing);
                const int y = rowTops[row];
                DesktopRootItemState& state = m_itemStates[i];
                const QPoint targetPos(x, y);
                state.model.position = targetPos;
                state.model.effectiveSize = QSize(itemW, qMax(itemH, rowHeights[row]));
                rebuildItemRenderData(state, params);
            }
        }
    } else {
        const int listItemH = scaledPx(this, kBaseItemListHeight);
        const int maxListItemW = qMax(1, contentW - 2 * marginLeft);
        DesktopRenderParams baseParams = buildBoxRenderParams(this, m_showType, maxListItemW, listItemH);
        const QFontMetrics listMetrics(baseParams.labelFont);
        const int leftPadding = scaledPx(this, kBaseItemListPadding);
        const int rightPadding = scaledPx(this, kBaseItemListRightPadding);
        const int iconSide = qMax(1, listItemH - scaledPx(this, 6));
        const int gapAfterIcon = qMax(1, baseParams.iconTextGap + 1);
        const int minListItemW = leftPadding + iconSide + gapAfterIcon + rightPadding + scaledPx(this, 24);
        for (int i = 0; i < m_itemStates.size(); ++i) {
            DesktopRootItemState& state = m_itemStates[i];
            if (!needsFullLayout && !state.renderDataDirty) {
                continue;
            }

            const QString renderText = state.model.expandedPreviewText.isEmpty()
                ? state.model.displayName
                : state.model.expandedPreviewText;
            const int textWidth = qMax(scaledPx(this, 10), listMetrics.horizontalAdvance(renderText));
            const int listItemW = qBound(
                minListItemW,
                leftPadding + iconSide + gapAfterIcon + textWidth + rightPadding,
                maxListItemW);
            const int x = marginLeft;
            const int y = marginTop + i * (listItemH + scaledPx(this, 2));
            const QPoint targetPos(x, y);
            DesktopRenderParams params = baseParams;
            params.iconWidth = listItemW;
            state.model.layoutMode = DesktopLayoutMode::ListMode;
            state.model.position = targetPos;
            state.model.effectiveSize = QSize(listItemW, listItemH);
            rebuildItemRenderData(state, params);
        }
    }
    
    // Update content area height to enable scroll area
    int totalHeight = 0;
    if (m_showType == BoxShowType::Icon_ShowType) {
        const int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        int rowHeightSum = 0;
        for (int rowStart = 0; rowStart < m_itemStates.size(); rowStart += cols) {
            int rowMaxHeight = itemH;
            const int rowEnd = qMin(rowStart + cols, m_itemStates.size());
            for (int i = rowStart; i < rowEnd; ++i) {
                rowMaxHeight = qMax(rowMaxHeight, m_itemStates[i].model.renderData.itemRect.height());
            }
            rowHeightSum += rowMaxHeight;
        }
        const int rows = (m_itemStates.size() + cols - 1) / cols;
        totalHeight = marginTop + rowHeightSum + qMax(0, rows - 1) * rowSpacing + scaledPx(this, 10);
    } else {
        totalHeight = marginTop + m_itemStates.size() * (scaledPx(this, kBaseItemListHeight) + scaledPx(this, 2)) + scaledPx(this, 10);
    }
    if (m_contentArea) {
        m_contentArea->setMinimumWidth(contentW);
        m_contentArea->setMinimumHeight(totalHeight);
        m_contentArea->resize(contentW, totalHeight);
        m_contentArea->update();
    }

    m_lastLayoutContentWidth = contentW;
    m_lastLayoutItemCount = m_itemStates.size();
    m_lastLayoutShowType = m_showType;
    m_lastLayoutVisualSignature = currentLayoutVisualSignature();
    m_isRefreshingLayout = false;
    update();
}

QRect LinBox::globalRenameEditorRectForPath(const QString& filePath) const
{
    if (!m_contentArea) {
        return QRect();
    }

    const DesktopRootItemState* state = findItemStateByPath(filePath);
    if (!state) {
        return QRect();
    }

    const QPoint globalTopLeft = m_contentArea->mapToGlobal(state->model.renderData.renameEditorRect.topLeft());
    return QRect(globalTopLeft, state->model.renderData.renameEditorRect.size());
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
    update();
    if (m_contentArea) {
        m_contentArea->update();
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
    update();
    if (m_contentArea) {
        m_contentArea->update();
    }
    emit geometryChanged(this);
}

void LinBox::setMappedFolderMonitor(LinDirectoryMonitor* monitor)
{
    m_mappedFolderMonitor.reset(monitor);
}

void LinBox::refreshSelectionOverlayColor()
{
    if (!m_selectionArea) {
        return;
    }

    m_selectionArea->reloadColorsFromSettings();
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

QRect LinBox::mappedTitleIconRect() const
{
    if (!isFolderMapped() || !m_titleBar) {
        return QRect();
    }

    const int iconSize = scaledPx(this, 20);
    const int iconY = (m_titleBar->height() - iconSize) / 2;
    const QFont titleFont = AppFonts::uiFont(9, QFont::Bold);
    const QFontMetrics fm(titleFont);
    const QString title = displayTitleText();
    const int textWidth = fm.horizontalAdvance(title);
    const int totalWidth = iconSize + scaledPx(this, 6) + textWidth;
    int startX = (m_titleBar->width() - totalWidth) / 2;
    startX = qMax(scaledPx(this, 35), startX);
    return QRect(startX, iconY, iconSize, iconSize);
}

QRect LinBox::mappedTitleTextRect() const
{
    if (!m_titleBar) {
        return QRect();
    }

    QRect textRect = m_titleBar->rect().adjusted(scaledPx(this, 35), 0, -scaledPx(this, 35), 0);
    if (!isFolderMapped()) {
        return textRect;
    }

    const QRect iconRect = mappedTitleIconRect();
    if (!iconRect.isNull()) {
        textRect = m_titleBar->rect().adjusted(iconRect.right() + scaledPx(this, 7), 0, -scaledPx(this, 35), 0);
    }
    return textRect;
}

bool LinBox::openMappedFolderInExplorer()
{
    if (!isFolderMapped() || m_mappedFolderPath.isEmpty()) {
        return false;
    }

    const QFileInfo mappedInfo(m_mappedFolderPath);
    if (!mappedInfo.exists() || !mappedInfo.isDir()) {
        return false;
    }

    return QDesktopServices::openUrl(QUrl::fromLocalFile(mappedInfo.absoluteFilePath()));
}

QRect LinBox::mappedPlaceholderRect() const
{
    const bool shouldDrawMappedPlaceholder =
        isFolderMapped() &&
        !m_isCollapsed &&
        m_scrollArea &&
        m_scrollArea->isVisible() &&
        (mappedFolderState() != MappedFolderState::Normal || m_itemStates.isEmpty());
    if (!shouldDrawMappedPlaceholder) {
        return QRect();
    }

    const QString resourcePath = mappedFolderPlaceholderResourcePath(mappedFolderState(), m_itemStates.isEmpty());
    if (resourcePath.isEmpty()) {
        return QRect();
    }

    QWidget* viewport = m_scrollArea->viewport();
    const QPoint topLeft = viewport ? viewport->mapTo(const_cast<LinBox*>(this), QPoint(0, 0)) : m_scrollArea->geometry().topLeft();
    const QSize areaSize = viewport ? viewport->size() : m_scrollArea->size();
    QRect placeholderArea(topLeft, areaSize);
    const int placeholderInset = scaledPx(this, kBasePlaceholderInset);
    placeholderArea = placeholderArea.adjusted(placeholderInset, placeholderInset, -placeholderInset, -placeholderInset);
    if (placeholderArea.width() <= 0 || placeholderArea.height() <= 0) {
        return QRect();
    }

    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid()) {
        return QRect();
    }

    QSize targetSize = renderer.defaultSize();
    if (!targetSize.isValid()) {
        targetSize = QSize(scaledPx(this, 220), scaledPx(this, 220));
    }

    const int maxWidth = qMax(scaledPx(this, 96), qRound(placeholderArea.width() * 0.58));
    const int maxHeight = qMax(scaledPx(this, 96), qRound(placeholderArea.height() * 0.58));
    targetSize.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
    return QRect(
        placeholderArea.center().x() - targetSize.width() / 2,
        placeholderArea.center().y() - targetSize.height() / 2,
        targetSize.width(),
        targetSize.height());
}

bool LinBox::tryRefreshMappedFolderFromPlaceholderClick(const QPoint& boxPos)
{
    const QRect placeholderRect = mappedPlaceholderRect();
    if (!placeholderRect.contains(boxPos)) {
        return false;
    }

    if (mappedFolderState() == MappedFolderState::Normal) {
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    lv->reloadMappedFolderBox(this);
    refreshLayout();
    raise();
    update();

    const QString currentMappedPath = mappedFolderDisplayText();
    if (mappedFolderState() == MappedFolderState::Missing) {
        bool shouldRemap = false;
        FluentDialog dlg(this);
        dlg.setDialogTitle(tr("Mapped Folder Missing"));
        dlg.setMessage(tr("The mapped folder \"%1\" is still missing.\n\nDo you want to re map this box to another folder?").arg(currentMappedPath));
        dlg.addActionButton(tr("ReMap Folder"), FluentDialog::ActionType::Primary, [&shouldRemap]() {
            shouldRemap = true;
        });
        dlg.addActionButton(tr("Cancel"), FluentDialog::ActionType::Close);
        dlg.exec();
        if (shouldRemap) {
            remapMappedFolderInteractively();
        }
    } else if (mappedFolderState() == MappedFolderState::NoAccess) {
        bool shouldRemap = false;
        FluentDialog dlg(this);
        dlg.setDialogTitle(tr("Mapped Folder Unavailable"));
        dlg.setMessage(tr("The mapped folder \"%1\" is still unavailable because access is denied.\n\nDo you want to re map this box to another folder?").arg(currentMappedPath));
        dlg.addActionButton(tr("ReMap Folder"), FluentDialog::ActionType::Primary, [&shouldRemap]() {
            shouldRemap = true;
        });
        dlg.addActionButton(tr("Cancel"), FluentDialog::ActionType::Close);
        dlg.exec();
        if (shouldRemap) {
            remapMappedFolderInteractively();
        }
    }

    return true;
}

bool LinBox::remapMappedFolderInteractively()
{
    if (!isFolderMapped()) {
        return false;
    }

    ListView* lv = ListView::instance();
    const QString initialDir = QFileInfo(m_mappedFolderPath).isDir() ? m_mappedFolderPath : QDir::homePath();
    const QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Folder to Map"),
        initialDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedDir.isEmpty()) {
        return false;
    }

    const QString normalizedPath = normalizeMappedFolderPath(selectedDir);
    if (normalizedPath.isEmpty() ||
        QString::compare(normalizedPath, normalizeMappedFolderPath(m_mappedFolderPath), Qt::CaseInsensitive) == 0) {
        return false;
    }

    if (lv && lv->findMappedFolderBox(normalizedPath, this)) {
        lv->showTransientTip(tr("This folder is already on the desktop."), 2600);
        return false;
    }

    setMappedFolderPath(normalizedPath);
    if (lv) {
        lv->reloadMappedFolderBox(this);
        lv->requestSaveBoxes();
    }
    refreshLayout();
    raise();
    update();
    return true;
}

void LinBox::checkMappedFolderStateAndRefresh()
{
    if (!isFolderMapped()) {
        return;
    }

    if (m_mappedFolderState != MappedFolderState::Normal) {
        return;
    }

    const MappedFolderState previousState = m_mappedFolderState;
    refreshMappedFolderState();
    const MappedFolderState currentState = m_mappedFolderState;

    const bool stateChanged = currentState != previousState;
    const bool invalidStateWithStaleItems =
        currentState != MappedFolderState::Normal &&
        !m_itemStates.isEmpty();
    const bool recoveredWithoutMonitor =
        currentState == MappedFolderState::Normal &&
        !mappedFolderMonitor();

    if (!stateChanged && !invalidStateWithStaleItems && !recoveredWithoutMonitor) {
        return;
    }

    if (ListView* lv = ListView::instance()) {
        lv->reloadMappedFolderBox(this);
        refreshLayout();
        raise();
        update();
    } else {
        refreshLayout();
        update();
    }
}

bool LinBox::eventFilter(QObject* watched, QEvent* event) {
    const bool isTrackedCursorWidget =
        watched == m_titleBar ||
        watched == m_titleEdit ||
        watched == m_viewSwitchBtn ||
        watched == m_collapseBtn ||
        watched == m_scrollArea ||
        watched == m_contentArea ||
        (m_scrollArea && watched == m_scrollArea->viewport());

    if (isTrackedCursorWidget) {
        if (ListView* lv = ListView::instance()) {
            if (lv->isBackgroundSelectionActive()) {
                if (event->type() == QEvent::Enter ||
                    event->type() == QEvent::Leave ||
                    event->type() == QEvent::HoverEnter ||
                    event->type() == QEvent::HoverLeave ||
                    event->type() == QEvent::HoverMove ||
                    event->type() == QEvent::MouseMove ||
                    event->type() == QEvent::MouseButtonPress ||
                    event->type() == QEvent::MouseButtonRelease ||
                    event->type() == QEvent::MouseButtonDblClick ||
                    event->type() == QEvent::Wheel) {
                    return true;
                }
            }
        }
        if (hasActiveItemRename() && event->type() == QEvent::Wheel) {
            return true;
        }
    }

    if (watched == m_titleBar && event->type() == QEvent::Paint) {
        if (!m_titleEdit->isVisible()) {
            QPainter p(m_titleBar);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::white);
            p.setFont(AppFonts::uiFont(9, QFont::Bold));
            
            // Adjust rect to avoid buttons: collapseBtn(30px) and viewSwitchBtn(right)
            // Left gap for collapse button + padding
            QRect textRect = mappedTitleTextRect();
            
            if (isFolderMapped()) {
                const QRect iconRect = mappedTitleIconRect();
                const QString title = displayTitleText();
                bool iconDrawn = false;
                QSvgRenderer renderer(mappedFolderTitleIconResourcePath(m_mappedFolderPath));
                if (renderer.isValid()) {
                    renderer.render(&p, iconRect);
                    iconDrawn = true;
                } else if (isDriveRootPath(m_mappedFolderPath)) {
                    QSvgRenderer fallbackRenderer(QStringLiteral(":/Assets/folder-share.svg"));
                    if (fallbackRenderer.isValid()) {
                        fallbackRenderer.render(&p, iconRect);
                        iconDrawn = true;
                    }
                }

                if (iconDrawn) {
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

    if (isTrackedCursorWidget && (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)) {
        if (!m_isSelectingItems && !m_isResizing && !m_isMoving) {
            const QPoint mappedPos = mapFromGlobal(QCursor::pos());
            updateCursorShape(mappedPos);
        }
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
            if (mouseEvent->button() == Qt::LeftButton || mouseEvent->button() == Qt::RightButton) {
                BoxManager::instance()->bringToFront(this);
                activateForKeyboardFocus();
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
                const QPoint mappedPos = watched == this
                    ? mouseEvent->pos()
                    : sourceWidget->mapTo(this, mouseEvent->pos());
                if (watched == m_contentArea && handleItemMousePress(mouseEvent, mappedPos)) {
                    return true;
                }
                if (tryRefreshMappedFolderFromPlaceholderClick(mappedPos)) {
                    return true;
                }
                m_resizeDir = getResizeDir(mappedPos);

                if (m_resizeDir != None && m_resizeDir != Move) {
                    m_isResizing = true;
                    m_dragStartPos = mouseEvent->globalPos();
                    m_dragStartGeometry = geometry();
                    m_geometryChangedDuringInteraction = false;
                    return true;
                }

                if (watched == m_contentArea) {
                    if (!hitItemStateAt(mappedPos) && m_resizeDir == None) {
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
                const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
                const QPoint mappedPos = watched == this
                    ? mouseEvent->pos()
                    : sourceWidget->mapTo(this, mouseEvent->pos());
                if (watched == m_contentArea && handleItemContextMenu(mouseEvent->globalPos(), mappedPos)) {
                    return true;
                }
                const bool extendedVerbs = (mouseEvent->modifiers() & Qt::ShiftModifier) != 0;
                showCustomContextMenu(mouseEvent->globalPos(), extendedVerbs);
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
            const QPoint mappedPos = watched == this
                ? mouseEvent->pos()
                : sourceWidget->mapTo(this, mouseEvent->pos());

            if (watched == m_contentArea && !m_isSelectingItems) {
                updateHoveredItem(mappedPos);
            }
            if (watched == m_contentArea && handleItemMouseMove(mouseEvent, mappedPos)) {
                return true;
            }
            if (watched == m_contentArea && m_isSelectingItems) {
                updateItemSelection(mouseEvent->pos());
                return true;
            }
            if (!m_isSelectingItems && !m_isResizing && !m_isMoving) {
                updateCursorShape(mappedPos);
            }
        } else if (watched == m_contentArea && event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
            const QPoint mappedPos = watched == this
                ? mouseEvent->pos()
                : sourceWidget->mapTo(this, mouseEvent->pos());
            if (handleItemMouseRelease(mouseEvent, mappedPos)) {
                return true;
            }
            if (m_isSelectingItems) {
                finishItemSelection();
                return true;
            }
        } else if (watched == m_contentArea && event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
            const QPoint mappedPos = watched == this
                ? mouseEvent->pos()
                : sourceWidget->mapTo(this, mouseEvent->pos());
            if (handleItemMouseDoubleClick(mouseEvent, mappedPos)) {
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick && watched == m_titleBar) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (isFolderMapped() && mappedTitleIconRect().contains(mouseEvent->pos())) {
                openMappedFolderInExplorer();
                return true;
            }
            if (mappedTitleTextRect().contains(mouseEvent->pos())) {
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
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        BoxManager::instance()->bringToFront(this);
        activateForKeyboardFocus();
    }
    if (event->button() == Qt::LeftButton) {
        if (handleItemMousePress(event, event->pos())) {
            event->accept();
            return;
        }
        if (tryRefreshMappedFolderFromPlaceholderClick(event->pos())) {
            event->accept();
            return;
        }
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
        if (handleItemContextMenu(event->globalPos(), event->pos())) {
            event->accept();
            return;
        }
        QWidget* child = childAt(event->pos());
        if (m_titleBar->geometry().contains(event->pos()) || !child || child == m_contentArea || child == this) {
            const bool extendedVerbs = (event->modifiers() & Qt::ShiftModifier) != 0;
            showCustomContextMenu(event->globalPos(), extendedVerbs);
        }
    }
}

void LinBox::activateForKeyboardFocus()
{
    raise();
    activateWindow();
    setFocus(Qt::MouseFocusReason);
}

void LinBox::mouseMoveEvent(QMouseEvent* event) {
    if (handleItemMouseMove(event, event->pos())) {
        event->accept();
        return;
    }
    if (m_isResizing) {
        QPoint delta = event->globalPos() - m_dragStartPos;
        QRect newGeom = m_dragStartGeometry;
        if (m_resizeDir & Left) newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
        if (m_resizeDir & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());
        if (m_resizeDir & Top) newGeom.setTop(m_dragStartGeometry.top() + delta.y());
        if (m_resizeDir & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());
        const int minResizeSize = scaledPx(this, 100);
        if (newGeom.width() >= minResizeSize && newGeom.height() >= minResizeSize) {
            if (geometry() != newGeom) {
                setGeometry(newGeom);
                m_geometryChangedDuringInteraction = true;
                update();
            }
        }
    } else if (m_isMoving) {
        // 闂佽楠稿﹢閬嶅磻閻愬樊娓婚柛灞剧矋瀹曞鏌ｉ弬鍨倯妞ゆ挾鎳撻～妤勵槾濠殿喚鏁婚幃銉╂晲閸モ晝鐓嬮柣鐘叉处瑜板啳顤?
        QPoint delta = event->globalPos() - m_dragStartPos;
        QPoint newPos = m_dragStartGeometry.topLeft() + delta;
        
        // 缂傚倷绀侀ˇ鎶筋敋瑜庨幈銊╁煛閸涱厾顦梺缁橆殔閻楀﹪鎮￠埀顒佺箾鏉堝墽鍒扮紒澶婄埣閹?
        if (pos() != newPos) {
            move(newPos);
            m_geometryChangedDuringInteraction = true;
            update();
        }
        
        // 闂備礁鎲￠悷顖涚濠靛枹娲锤濡も偓濡﹢鏌ｈ濡剧闂備礁鎼悧婊堝礈濠靛顥婇柍鍝勬噺閺咁剛鈧厜鍋撻柛鏇ㄤ簽缁辨岸姊虹粙璺ㄧ缂佸鎸抽幆鍥箥椤旂偓锛忛悷婊冪Х閵囨劖寰勯幇顒傤槺?
    } else {
        updateCursorShape(event->pos());
    }
}

void LinBox::mouseReleaseEvent(QMouseEvent* event) {
    if (handleItemMouseRelease(event, event->pos())) {
        event->accept();
        return;
    }
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

bool LinBox::handleItemMousePress(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }

    DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    if (!hitState) {
        m_pressedItemPath.clear();
        m_itemPressArmed = false;
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    const QString itemPath = hitState->model.filePath;
    const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
    const bool wasAlreadySelected = lv->selectedItemPaths().contains(itemPath);
    activateForKeyboardFocus();

    if (modifiers & Qt::ControlModifier) {
        lv->toggleItemSelection(itemPath);
    } else if (modifiers & Qt::ShiftModifier) {
        lv->selectRangeToPath(itemPath, this);
    } else if (wasAlreadySelected) {
        lv->setFocusedItemPath(itemPath);
        lv->setItemFullNameExpanded(itemPath, true);
    } else {
        lv->setSelectedItemPaths({ itemPath }, itemPath, true);
    }

    m_pressedItemPath = itemPath;
    m_pressedItemBoxPos = boxPos;
    m_itemPressArmed = true;
    m_itemClickTimer.restart();
    return true;
}

bool LinBox::startItemDrag(const QPoint& boxPos)
{
    Q_UNUSED(boxPos);
    if (!m_itemPressArmed || m_pressedItemPath.isEmpty()) {
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    QStringList dragPaths = lv->selectedItemPaths();
    if (!dragPaths.contains(m_pressedItemPath)) {
        dragPaths.clear();
        dragPaths.append(m_pressedItemPath);
        lv->setSelectedItemPaths(dragPaths, m_pressedItemPath, true);
    }

    QList<QUrl> urls;
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
    mimeData->setDropDescriptionForAction(Qt::MoveAction, boxTitle());

    lv->setDraggedItemPath(m_pressedItemPath, true);
    QDrag drag(this);
    drag.setMimeData(mimeData);

    QSet<QString> dragPathSet;
    for (const QString& path : dragPaths) {
        dragPathSet.insert(normalizeBoxItemPath(path));
    }

    QVector<DesktopRootItemRenderData> ghostItems;
    QRect ghostBounds;
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!dragPathSet.contains(normalizeBoxItemPath(state.model.filePath))) {
            continue;
        }

        const DesktopRootItemRenderData& renderData = state.model.renderData;
        if (renderData.itemRect.isNull() || !renderData.itemRect.size().isValid()) {
            continue;
        }

        ghostItems.append(renderData);
        ghostBounds = ghostBounds.isNull()
            ? renderData.itemRect
            : ghostBounds.united(renderData.itemRect);
    }

    if (ghostItems.isEmpty()) {
        const DesktopRootItemState* dragState = findItemStateByPath(m_pressedItemPath);
        if (dragState && !dragState->model.renderData.itemRect.isNull() && dragState->model.renderData.itemRect.size().isValid()) {
            ghostItems.append(dragState->model.renderData);
            ghostBounds = dragState->model.renderData.itemRect;
        }
    }

    if (!ghostItems.isEmpty() && ghostBounds.isValid()) {
        QPixmap ghostPixmap(ghostBounds.size());
        ghostPixmap.fill(Qt::transparent);

        QPainter painter(&ghostPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.translate(-ghostBounds.topLeft());
        for (const DesktopRootItemRenderData& renderData : ghostItems) {
            paintDesktopRootItemGhost(painter, renderData);
        }
        painter.end();

        QPoint contentPressPos = m_contentArea
            ? m_contentArea->mapFrom(this, m_pressedItemBoxPos)
            : m_pressedItemBoxPos;
        QPoint hotSpot = contentPressPos - ghostBounds.topLeft();
        hotSpot.setX(qBound(0, hotSpot.x(), qMax(0, ghostPixmap.width() - 1)));
        hotSpot.setY(qBound(0, hotSpot.y(), qMax(0, ghostPixmap.height() - 1)));

        drag.setPixmap(ghostPixmap);
        drag.setHotSpot(hotSpot);
    }

    const Qt::DropAction resultAction = drag.exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::MoveAction);
    if (resultAction == Qt::MoveAction) {
        QStringList removedSourcePaths;
        for (const QString& path : dragPaths) {
            const QString normPath = normalizeBoxItemPath(path);
            if (!path.isEmpty() && !normPath.startsWith(QLatin1String("::")) && CSystemApi::GetPathType(path) == PathType::InvalidPath) {
                removedSourcePaths.append(path);
            }
        }
        lv->finalizeMovedModelSourcesAfterFileOp(removedSourcePaths);
    }
    lv->clearDraggedItemPath(true);
    return true;
}

bool LinBox::handleItemMouseMove(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || !(event->buttons() & Qt::LeftButton) || !m_itemPressArmed || m_pressedItemPath.isEmpty()) {
        return false;
    }

    if ((boxPos - m_pressedItemBoxPos).manhattanLength() < QApplication::startDragDistance()) {
        return false;
    }

    const bool started = startItemDrag(boxPos);
    m_itemPressArmed = false;
    m_pressedItemPath.clear();
    return started;
}

bool LinBox::handleItemMouseRelease(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || event->button() != Qt::LeftButton || !m_itemPressArmed) {
        return false;
    }

    m_itemPressArmed = false;
    m_pressedItemPath.clear();
    updateHoveredItem(boxPos);
    return hitItemStateAt(boxPos) != nullptr;
}

bool LinBox::handleItemMouseDoubleClick(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }

    const DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    if (!hitState) {
        return false;
    }

    if (ListView* lv = ListView::instance()) {
        lv->clearCutMarks();
        lv->openItemByPath(hitState->model.filePath);
    }
    m_itemPressArmed = false;
    m_pressedItemPath.clear();
    return true;
}

bool LinBox::handleItemContextMenu(const QPoint& globalPos, const QPoint& boxPos)
{
    const DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    if (!hitState) {
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    const QString itemPath = hitState->model.filePath;
    if (!lv->selectedItemPaths().contains(itemPath)) {
        lv->setSelectedItemPaths({ itemPath }, itemPath, true);
    } else {
        lv->setFocusedItemPath(itemPath);
    }

    lv->requestSystemRightMenu(hitState->model.objectType, globalPos, lv->selectedItemPaths());
    return true;
}

void LinBox::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRegion dirtyRegion = event->region();

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

    const QRect placeholderRect = mappedPlaceholderRect();
    if (!placeholderRect.isNull()) {
        const QString resourcePath = mappedFolderPlaceholderResourcePath(mappedFolderState(), m_itemStates.isEmpty());
        if (!resourcePath.isEmpty()) {
            QSvgRenderer renderer(resourcePath);
            if (renderer.isValid()) {
                renderer.render(&p, placeholderRect);
            }
        }
    }

    if (m_contentArea && m_scrollArea && m_scrollArea->viewport()) {
        const QPoint contentOrigin = m_contentArea->mapTo(this, QPoint(0, 0));
        p.save();
        p.setClipRect(m_scrollArea->geometry());
        for (const DesktopRootItemState& state : m_itemStates) {
            DesktopRootItemRenderData renderData = state.model.renderData;
            translateRenderData(renderData, contentOrigin);
            if (!dirtyRegion.intersects(renderData.itemRect)) {
                continue;
            }
            paintDesktopRootItemDynamic(p, renderData);
            paintDesktopRootItemStatic(p, renderData);
        }
        p.restore();
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
    m_selectionArea->syncToRect(m_contentArea->rect());
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
        m_selectionArea->syncToRect(m_contentArea->rect());
        m_selectionArea->setSelectionRect(selectionRect);
    }

    QStringList selectedPaths;
    selectedPaths.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!selectionRect.intersects(state.model.renderData.selectionContentRect)) {
            continue;
        }
        selectedPaths.push_back(state.model.filePath);
    }

    if (ListView* listView = ListView::instance()) {
        const QString focusedPath = selectedPaths.isEmpty() ? QString() : selectedPaths.last();
        const QStringList currentSelectedPaths = listView->selectedItemPaths();
        const QString currentFocusedPath = listView->focusedItemPath();

        if (selectedPaths.isEmpty()) {
            if (currentSelectedPaths.isEmpty() && currentFocusedPath.isEmpty()) {
                return;
            }
            listView->clearCheckedItem();
            return;
        }

        if (currentSelectedPaths == selectedPaths &&
            currentFocusedPath == focusedPath) {
            return;
        }

        listView->setSelectedItemPaths(selectedPaths, focusedPath, false);
    }
}

void LinBox::clearSelectionOverlay()
{
    if (!m_selectionArea) {
        return;
    }

    const QRect previousRect = m_selectionArea->selectionRect();
    m_selectionArea->clearAndHide();
    if (m_contentArea && !previousRect.isNull()) {
        m_contentArea->update(previousRect.adjusted(-2, -2, 2, 2));
    }
}

void LinBox::finishItemSelection()
{
    m_isSelectingItems = false;
    clearSelectionOverlay();
}

void LinBox::updateHoveredItem(const QPoint& boxPos)
{
    const DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    const QString nextHoveredPath = hitState ? normalizeBoxItemPath(hitState->model.filePath) : QString();
    if (m_hoveredItemPath == nextHoveredPath) {
        return;
    }

    bool dirty = false;
    for (DesktopRootItemState& state : m_itemStates) {
        const QString statePath = normalizeBoxItemPath(state.model.filePath);
        const bool hovered = !nextHoveredPath.isEmpty() && statePath == nextHoveredPath;
        if (state.model.isHovered != hovered || state.model.renderData.hovered != (hovered && !state.model.isSelected)) {
            state.model.isHovered = hovered;
            state.model.renderData.hovered = hovered && !state.model.isSelected;
            dirty = true;
        }
    }

    m_hoveredItemPath = nextHoveredPath;
    if (dirty) {
        update();
    }
}

void LinBox::clearHoveredItem()
{
    if (m_hoveredItemPath.isEmpty()) {
        return;
    }

    bool dirty = false;
    for (DesktopRootItemState& state : m_itemStates) {
        if (!state.model.isHovered && !state.model.renderData.hovered) {
            continue;
        }
        state.model.isHovered = false;
        state.model.renderData.hovered = false;
        dirty = true;
    }

    m_hoveredItemPath.clear();
    if (dirty) {
        update();
    }
}

void LinBox::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
}

void LinBox::leaveEvent(QEvent* event) {
    clearHoveredItem();
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

LinBox::ResizeDir LinBox::getResizeDir(const QPoint& pos) {
    const int x = pos.x();
    const int y = pos.y();
    const int w = width();
    const int h = height();
    const int resizeMargin = qMax(scaledPx(this, 4), m_borderWidth);
    const int cornerMargin = qMax(resizeMargin * 2, scaledPx(this, 16));

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
    if (msg->message == WM_DPICHANGED) {
        updateDpiMetrics();
        if (msg->lParam) {
            const RECT* suggested = reinterpret_cast<const RECT*>(msg->lParam);
            const QRect suggestedGeometry(
                QPoint(suggested->left, suggested->top),
                QSize(suggested->right - suggested->left, suggested->bottom - suggested->top));
            if (suggestedGeometry.isValid()) {
                setGeometry(suggestedGeometry);
            }
        }
        refreshLayout();
        update();
        if (m_glassEffect) {
            scheduleBlurRefresh();
        }
        if (result) {
            *result = 0;
        }
        return true;
    }

    if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) {
        if (ListView* lv = ListView::instance()) {
            if (msg->wParam == 'N' &&
                !RenameManager::instance()->isRenaming() &&
                (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                lv->createNewFolderInHost(this);
                if (result) {
                    *result = 0;
                }
                return true;
            }

            if (msg->wParam == VK_DELETE && !RenameManager::instance()->isRenaming()) {
                const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                lv->onMenuDelete(shiftPressed);
                if (result) {
                    *result = 0;
                }
                return true;
            }
        }
    }
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
                    const QPoint contentPos = m_contentArea->mapFrom(this, localPos);
                    const bool hitPaintedItem = hitItemStateAt(localPos) != nullptr;
                    
                    // If it's a scrollbar or a child of contentArea, it should be solid
                    if ((child && child == m_scrollArea->verticalScrollBar()) ||
                        m_contentArea->childAt(contentPos) ||
                        hitPaintedItem) {
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


DesktopRootItemState* LinBox::hitItemStateAt(const QPoint& boxPos)
{
    return const_cast<DesktopRootItemState*>(
        static_cast<const LinBox*>(this)->hitItemStateAt(boxPos));
}

const DesktopRootItemState* LinBox::hitItemStateAt(const QPoint& boxPos) const
{
    if (!m_contentArea) {
        return nullptr;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    for (int i = m_itemStates.size() - 1; i >= 0; --i) {
        const DesktopRootItemState& state = m_itemStates[i];
        if (state.model.renderData.selectionContentRect.contains(contentPos) ||
            state.model.renderData.itemRect.contains(contentPos)) {
            return &state;
        }
    }
    return nullptr;
}

const DesktopRootItemState* LinBox::dropTargetItemStateAt(const QPoint& boxPos) const
{
    if (!m_contentArea) {
        return nullptr;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    for (int i = m_itemStates.size() - 1; i >= 0; --i) {
        const DesktopRootItemState& state = m_itemStates[i];
        QRect targetRect = state.model.renderData.selectionContentRect;
        if (targetRect.isNull() || !targetRect.isValid()) {
            targetRect = state.model.renderData.staticContentRect;
        } else if (!state.model.renderData.staticContentRect.isNull() &&
                   state.model.renderData.staticContentRect.isValid()) {
            targetRect = targetRect.united(state.model.renderData.staticContentRect);
        }
        if (targetRect.isNull() || !targetRect.isValid()) {
            targetRect = state.model.renderData.itemRect;
        }
        if (targetRect.contains(contentPos)) {
            return &state;
        }
    }
    return nullptr;
}

bool LinBox::hasActiveItemRename() const
{
    for (const DesktopRootItemState& state : m_itemStates) {
        if (state.model.isRenaming) {
            return true;
        }
    }
    return false;
}

DesktopRootItemState* LinBox::findItemStateByPath(const QString& filePath)
{
    return const_cast<DesktopRootItemState*>(
        static_cast<const LinBox*>(this)->findItemStateByPath(filePath));
}

const DesktopRootItemState* LinBox::findItemStateByPath(const QString& filePath) const
{
    const QString normalized = normalizeBoxItemPath(filePath);
    for (const DesktopRootItemState& state : m_itemStates) {
        if (normalizeBoxItemPath(state.model.filePath) == normalized) {
            return &state;
        }
    }
    return nullptr;
}

int LinBox::insertionIndexForPos(const QPoint& boxPos, const QStringList& draggedPaths, bool* alignAfterPrev) const
{
    if (m_itemStates.isEmpty()) {
        if (alignAfterPrev) *alignAfterPrev = false;
        return 0;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeBoxItemPath(path));
    }

    QVector<const DesktopRootItemState*> orderedItems;
    orderedItems.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!draggedPathSet.contains(normalizeBoxItemPath(state.model.filePath))) {
            orderedItems.append(&state);
        }
    }

    if (orderedItems.isEmpty()) {
        if (alignAfterPrev) *alignAfterPrev = false;
        return 0;
    }

    if (m_showType == BoxShowType::List_ShowType) {
        if (alignAfterPrev) *alignAfterPrev = false;
        for (int i = 0; i < orderedItems.size(); ++i) {
            const QRect rect = orderedItems[i]->model.renderData.itemRect;
            if (contentPos.y() < rect.center().y()) {
                return i;
            }
        }
        return orderedItems.size();
    }

    QVector<QVector<int>> rows;
    for (int i = 0; i < orderedItems.size(); ++i) {
        const QRect rect = orderedItems[i]->model.renderData.itemRect;
        bool placed = false;
        for (QVector<int>& row : rows) {
            const QRect firstRect = orderedItems[row.first()]->model.renderData.itemRect;
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
        QRect rowRect = orderedItems[row.first()]->model.renderData.itemRect;
        for (int idx : row) {
            rowRect = rowRect.united(orderedItems[idx]->model.renderData.itemRect);
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
    const QRect firstRect = orderedItems[row.first()]->model.renderData.itemRect;
    const QRect lastRect = orderedItems[row.last()]->model.renderData.itemRect;

    struct InsertionLane {
        int insertIndex = 0;
        bool afterPrev = false;
        int x = 0;
    };

    QVector<InsertionLane> lanes;
    lanes.reserve(row.size() + 1);
    lanes.append({row.first(), false, firstRect.left()});
    for (int i = 0; i < row.size() - 1; ++i) {
        const QRect currentRect = orderedItems[row[i]]->model.renderData.itemRect;
        const QRect nextRect = orderedItems[row[i + 1]]->model.renderData.itemRect;
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

    QSet<QString> draggedPathSet;
    for (const QString& path : m_reorderDragPaths) {
        draggedPathSet.insert(normalizeBoxItemPath(path));
    }
    QVector<const DesktopRootItemState*> orderedItems;
    orderedItems.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!draggedPathSet.contains(normalizeBoxItemPath(state.model.filePath))) {
            orderedItems.append(&state);
        }
    }

    auto itemBorderRectInBox = [this](const DesktopRootItemState* state) -> QRect {
        if (!state) {
            return QRect();
        }
        const QRect contentRect = state->model.renderData.itemRect;
        const QPoint boxTopLeft = m_contentArea->mapTo(this, contentRect.topLeft());
        return QRect(boxTopLeft, contentRect.size());
    };

    if (m_showType == BoxShowType::List_ShowType) {
        if (orderedItems.isEmpty()) {
            return QRect(scaledPx(this, kBaseItemMarginLeft), m_titleHeight + scaledPx(this, kBaseItemMarginTop), qMax(scaledPx(this, 20), width() - scaledPx(this, 10)), scaledPx(this, kBaseIndicatorLine));
        }

        if (m_reorderInsertIndex >= orderedItems.size()) {
            const QRect rect = itemBorderRectInBox(orderedItems.last());
            return QRect(rect.left(), rect.bottom() + scaledPx(this, 2), rect.width(), scaledPx(this, kBaseIndicatorLine));
        }

        const QRect rect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex]);
        return QRect(rect.left(), rect.top() - scaledPx(this, 2), rect.width(), scaledPx(this, kBaseIndicatorLine));
    }

    if (orderedItems.isEmpty()) {
        return QRect(m_contentArea->x() + scaledPx(this, kBaseItemMarginLeft), m_contentArea->y() + scaledPx(this, kBaseItemMarginTop), scaledPx(this, 3), scaledPx(this, kBaseItemHeight));
    }

    if (m_reorderInsertIndex <= 0) {
        const QRect firstRect = itemBorderRectInBox(orderedItems.first());
        return QRect(firstRect.left() - scaledPx(this, 3), firstRect.top(), scaledPx(this, 3), firstRect.height());
    }

    if (m_reorderInsertIndex >= orderedItems.size()) {
        const QRect lastRect = itemBorderRectInBox(orderedItems.last());
        return QRect(lastRect.right() + scaledPx(this, 2), lastRect.top(), scaledPx(this, 3), lastRect.height());
    }

    const QRect prevRect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex - 1]);
    const QRect nextRect = itemBorderRectInBox(orderedItems[m_reorderInsertIndex]);
    if (m_reorderIndicatorAfterPrev) {
        return QRect(prevRect.right() + scaledPx(this, 2), prevRect.top(), scaledPx(this, 3), prevRect.height());
    }
    return QRect(nextRect.left() - scaledPx(this, 3), nextRect.top(), scaledPx(this, 3), nextRect.height());
}

void LinBox::clearReorderPreview()
{
    if (!m_reorderDragPaths.isEmpty() || m_reorderInsertIndex >= 0) {
        m_reorderDragPaths.clear();
        m_reorderInsertIndex = -1;
        m_reorderIndicatorAfterPrev = false;
        update();
    }
}

void LinBox::showDropTipForItem(const DesktopRootItemState* state, const QPoint& boxPos, bool isCtrl, bool createShortcut)
{
    if (!state) {
        if (m_dragTip) m_dragTip->hide();
        return;
    }

    if (!m_dragTip) {
        m_dragTip = new FluentDragTip();
    }

    const DesktopRootItemModel& model = state->model;
    const QString clsid = model.itemData.getCLSID();
    const bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
    const bool isPC = (clsid == APP_MY_COMPUTER_CLS);
    const bool isNetwork = (clsid == APP_NETWORK_CLS);
    const bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
    const bool isFolder = (model.objectType == ObjectType::Folder_ObjectType ||
                           model.objectType == ObjectType::SpecialFolder ||
                           model.objectType == ObjectType::DiskLetter);

    if (isPC || isNetwork || isControlPanel) {
        m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
    } else {
        QString actionKeyword;
        QString highlightColor = "#70a1ff";
        QString htmlText;
        QString draggedPath;
        DesktopRootItemModel draggedModel;
        bool isDraggedDiskLetter = false;
        if (ListView* lv = ListView::instance()) {
            draggedPath = lv->draggedItemPath();
            isDraggedDiskLetter = !draggedPath.isEmpty() &&
                lv->resolveItemModelByPath(draggedPath, draggedModel) &&
                draggedModel.objectType == ObjectType::DiskLetter;
        }

        if (isRecycleBin) {
            actionKeyword = tr("Move to");
            highlightColor = "#ff4d4d";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        } else if (isFolder && createShortcut) {
            actionKeyword = tr("Create shortcut in");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>Open</b> <span>%2</span> <b style='color:%1'>here</b>")
                .arg(highlightColor, model.displayName);
        } else if (isFolder) {
            actionKeyword = isCtrl ? tr("Copy to") : tr("Move to");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        } else {
            actionKeyword = tr("Open with");
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, model.displayName);
        }
        m_dragTip->setText(htmlText);
    }

    QPoint targetPos = boxPos + QPoint(scaledPx(this, kBaseDragTipOffset), scaledPx(this, kBaseDragTipOffset));
    if (targetPos.x() + m_dragTip->width() > width()) targetPos.setX(boxPos.x() - m_dragTip->width() - scaledPx(this, kBaseDragTipMargin));
    if (targetPos.y() + m_dragTip->height() > height()) targetPos.setY(boxPos.y() - m_dragTip->height() - scaledPx(this, kBaseDragTipMargin));
    m_dragTip->showAt(mapToGlobal(targetPos));
}

bool LinBox::handleMappedBoxItemTargetDrop(const QStringList& sourcePaths,
                                           const DesktopRootItemState* targetState,
                                           bool copyMode,
                                           bool preferShortcut,
                                           QDropEvent* event)
{
    ListView* lv = ListView::instance();
    if (!lv || !event || !isFolderMapped() || !targetState) {
        return false;
    }

    const DesktopRootItemModel& targetModel = targetState->model;
    const QString targetPath = QDir::toNativeSeparators(targetModel.filePath);
    const QStringList operationPaths = sourcePaths;
    const QString operationName = preferShortcut
        ? QStringLiteral("shortcut")
        : (copyMode ? QStringLiteral("copy") : QStringLiteral("move"));
    const auto finishIgnored = [&]() {
        qDebug() << "[MappedBoxDrop] result=ignored";
        event->ignore();
        return true;
    };

    qDebug() << "[MappedBoxDrop] target item"
             << "sources=" << operationPaths
             << "target=" << targetPath
             << "type=" << static_cast<int>(targetModel.objectType)
             << "operation=" << operationName;

    if (operationPaths.isEmpty() || targetPath.isEmpty()) {
        qDebug() << "[MappedBoxDrop] ignored empty source or target";
        return finishIgnored();
    }

    if (isForbiddenDropTarget(targetModel)) {
        qDebug() << "[MappedBoxDrop] forbidden target"
                 << "operation=" << operationName;
        event->setDropAction(Qt::IgnoreAction);
        return finishIgnored();
    }

    for (const QString& sourcePath : operationPaths) {
        if (normalizeBoxItemPath(sourcePath) == normalizeBoxItemPath(targetPath)) {
            qDebug() << "[MappedBoxDrop] ignored self target" << targetPath;
            return finishIgnored();
        }
    }

    const Qt::DropAction dropAction = preferShortcut
        ? Qt::LinkAction
        : (copyMode ? Qt::CopyAction : Qt::MoveAction);

    if (isRecycleBinDropTarget(targetModel)) {
        ListView::DropInteractionOptions options;
        options.copyMode = copyMode;
        options.preferShortcut = preferShortcut;
        options.finalizeMovedSourcesAfterSuccess = true;
        const bool ok = lv->handleDroppedPathsOnItemTarget(operationPaths, targetModel, options);
        qDebug() << "[MappedBoxDrop] recycle operation result"
                 << "target=" << targetPath
                 << "operation=" << operationName
                 << "result=" << ok;

        if (!ok) {
            return finishIgnored();
        }

        lv->reloadMappedFolderBox(this);
        refreshLayout();
        update();
        raise();
        lv->requestSaveBoxes();
        event->setDropAction(dropAction);
        event->accept();
        return true;
    }

    if (isFolderDropTarget(targetModel)) {
        const QString targetFolder = normalizeMappedFolderPath(targetPath);
        const QStringList srcPaths = toNativeQStringListPaths(operationPaths);
        if (targetFolder.isEmpty() || srcPaths.isEmpty()) {
            qDebug() << "[MappedBoxDrop] ignored invalid folder operation"
                     << "targetFolder=" << targetFolder;
            return finishIgnored();
        }

        bool ok = false;
        if (preferShortcut) {
            ok = lv->createShortcutsInFolder(operationPaths, targetFolder);
        } else if (copyMode) {
            ok = CSystemApi::CopyFiles(srcPaths, targetFolder);
        } else {
            ok = CSystemApi::MoveFiles(srcPaths, targetFolder);
        }

        qDebug() << "[MappedBoxDrop] folder operation result"
                 << "targetFolder=" << targetFolder
                 << "operation=" << operationName
                 << "result=" << ok;

        if (!ok) {
            return finishIgnored();
        }

        if (!copyMode && !preferShortcut) {
            lv->finalizeMovedModelSourcesAfterFileOp(operationPaths);
        }
        lv->reloadMappedFolderBox(this);
        refreshLayout();
        update();
        raise();
        lv->requestSaveBoxes();
        event->setDropAction(dropAction);
        event->accept();
        return true;
    }

    ListView::DropInteractionOptions options;
    options.copyMode = copyMode;
    options.preferShortcut = preferShortcut;
    options.finalizeMovedSourcesAfterSuccess = true;
    const bool ok = lv->handleDroppedPathsOnItemTarget(operationPaths, targetModel, options);
    qDebug() << "[MappedBoxDrop] generic item operation result"
             << "target=" << targetPath
             << "operation=" << operationName
             << "result=" << ok;

    if (!ok) {
        return finishIgnored();
    }

    event->setDropAction(dropAction);
    event->accept();
    return true;
}

void LinBox::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    ListView* lv = ListView::instance();
    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(event->keyboardModifiers());
    const bool isCtrl = (effectiveModifiers & Qt::ControlModifier);
    const bool isInternalDrag = isDesktopOrBoxDragSource(event->source());
    const QStringList paths = lv ? lv->resolveDraggedLocalPaths(event->mimeData()) : QStringList();
    const DesktopRootItemState* hitState = dropTargetItemStateAt(event->pos());
    const QStringList orderedPaths = (isInternalDrag && lv)
        ? lv->orderedDraggedPaths(paths)
        : QStringList();
    const bool preferShortcutForDrop = (isFolderMapped() && lv)
        ? shouldCreateShortcutForMappedFolderDrop(lv, isInternalDrag, orderedPaths, paths, effectiveModifiers)
        : (lv && lv->shouldPreferShortcutForDrop(paths, effectiveModifiers, !isInternalDrag));
    const QString dropTargetText = isFolderMapped() ? mappedFolderDisplayText() : boxTitle();
    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (hitState && !pathSetContains(paths, hitState->model.filePath)) {
        clearReorderPreview();
        const QString clsid = hitState->model.itemData.getCLSID();
        const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
        const bool isFolderTarget = (hitState->model.objectType == ObjectType::Folder_ObjectType ||
                                     hitState->model.objectType == ObjectType::SpecialFolder ||
                                     hitState->model.objectType == ObjectType::DiskLetter);
        const bool isRecycleBinTarget = (clsid == APP_RECYCLE_BIN_CLS);
        event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
        if (isForbiddenTarget) {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName, true);
        } else if (isRecycleBinTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_MOVE, QCoreApplication::translate("LinBox", "Move to %1"), hitState->model.displayName);
        } else if (isFolderTarget && preferShortcutForDrop) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LINK, QCoreApplication::translate("LinBox", "Create shortcut in %1"), hitState->model.displayName);
        } else if (!isFolderTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LABEL, QCoreApplication::translate("LinBox", "Open with %1"), hitState->model.displayName);
        } else {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName);
        }
        event->accept();
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (!paths.isEmpty() && lv) {
            lv->resetRenameClickState();
        }
        if (mappedFolderState() != MappedFolderState::Normal) {
            if (m_dragTip) m_dragTip->hide();
            event->ignore();
            return;
        }
        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const bool isInternalSameFolderMove = isInternalDrag && !isCtrl && !preferShortcutForDrop && areAllPathsDirectChildrenOfFolder(paths, targetFolder);
        if (isInternalSameFolderMove) {
            if (m_dragTip) m_dragTip->hide();
            event->ignore();
            return;
        }
        event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction));
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (!paths.isEmpty() && isInternalDrag && !preferShortcutForDrop && !isCtrl) {
        lv->resetRenameClickState();
        m_reorderDragPaths = paths;
        m_reorderInsertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) m_dragTip->hide();
        event->setDropAction(Qt::MoveAction);
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        update();
        return;
    }

    const QString clsid = hitState ? hitState->model.itemData.getCLSID() : QString();
    const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
    event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
    ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText, isForbiddenTarget);
    event->accept();

    if (hitState) {
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
    } else if (isInternalDrag && !preferShortcutForDrop) {
        if (m_dragTip) m_dragTip->hide();
    } else {
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
    }
}

void LinBox::dragLeaveEvent(QDragLeaveEvent* event) {
    if (m_dragTip) m_dragTip->hide();
    clearReorderPreview();
}

void LinBox::clearDropPreview()
{
    if (m_dragTip) {
        m_dragTip->hide();
    }
    clearReorderPreview();
}

void LinBox::previewGhostDropAt(const QPoint& boxPos, const QStringList& draggedPaths, Qt::KeyboardModifiers modifiers)
{
    ListView* lv = ListView::instance();
    if (!lv || draggedPaths.isEmpty()) {
        clearDropPreview();
        return;
    }

    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(modifiers);
    const bool isCtrl = (effectiveModifiers & Qt::ControlModifier);
    const DesktopRootItemState* hitState = dropTargetItemStateAt(boxPos);
    const QStringList orderedPaths = lv->orderedDraggedPaths(draggedPaths);
    const bool preferShortcutForDrop = isFolderMapped()
        ? shouldCreateShortcutForMappedFolderDrop(lv, true, orderedPaths, draggedPaths, effectiveModifiers)
        : lv->shouldPreferShortcutForDrop(draggedPaths, effectiveModifiers, false);

    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (hitState && !pathSetContains(draggedPaths, hitState->model.filePath)) {
        clearReorderPreview();
        showDropTipForItem(hitState, boxPos, isCtrl, preferShortcutForDrop);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const bool isInternalSameFolderMove =
            !isCtrl &&
            !preferShortcutForDrop &&
            areAllPathsDirectChildrenOfFolder(draggedPaths, targetFolder);
        if (isInternalSameFolderMove) {
            if (m_dragTip) {
                m_dragTip->hide();
            }
            return;
        }
        updateDragTip(boxPos, isCtrl, preferShortcutForDrop);
        return;
    }

    if (!preferShortcutForDrop && !isCtrl) {
        m_reorderDragPaths = draggedPaths;
        m_reorderInsertIndex = insertionIndexForPos(boxPos, draggedPaths, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) {
            m_dragTip->hide();
        }
        update();
        return;
    }

    clearReorderPreview();
    updateDragTip(boxPos, isCtrl, preferShortcutForDrop);
}

void LinBox::dragMoveEvent(QDragMoveEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        if (m_dragTip) m_dragTip->hide();
        event->ignore();
        return;
    }

    ListView* lv = ListView::instance();
    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(event->keyboardModifiers());
    const bool isCtrl = (effectiveModifiers & Qt::ControlModifier);
    const bool isInternalDrag = isDesktopOrBoxDragSource(event->source());
    const QStringList paths = lv ? lv->resolveDraggedLocalPaths(event->mimeData()) : QStringList();
    const DesktopRootItemState* hitState = dropTargetItemStateAt(event->pos());
    const QStringList orderedPaths = (isInternalDrag && lv)
        ? lv->orderedDraggedPaths(paths)
        : QStringList();
    const bool preferShortcutForDrop = (isFolderMapped() && lv)
        ? shouldCreateShortcutForMappedFolderDrop(lv, isInternalDrag, orderedPaths, paths, effectiveModifiers)
        : (lv && lv->shouldPreferShortcutForDrop(paths, effectiveModifiers, !isInternalDrag));
    const QString dropTargetText = isFolderMapped() ? mappedFolderDisplayText() : boxTitle();
    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (hitState && !pathSetContains(paths, hitState->model.filePath)) {
        clearReorderPreview();
        const QString clsid = hitState->model.itemData.getCLSID();
        const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
        const bool isFolderTarget = (hitState->model.objectType == ObjectType::Folder_ObjectType ||
                                     hitState->model.objectType == ObjectType::SpecialFolder ||
                                     hitState->model.objectType == ObjectType::DiskLetter);
        const bool isRecycleBinTarget = (clsid == APP_RECYCLE_BIN_CLS);
        event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
        if (isForbiddenTarget) {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName, true);
        } else if (isRecycleBinTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_MOVE, QCoreApplication::translate("LinBox", "Move to %1"), hitState->model.displayName);
        } else if (isFolderTarget && preferShortcutForDrop) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LINK, QCoreApplication::translate("LinBox", "Create shortcut in %1"), hitState->model.displayName);
        } else if (!isFolderTarget) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(event->mimeData(), DROPIMAGE_LABEL, QCoreApplication::translate("LinBox", "Open with %1"), hitState->model.displayName);
        } else {
            ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), hitState->model.displayName);
        }
        event->accept();
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (!paths.isEmpty() && lv) {
            lv->resetRenameClickState();
        }
        if (mappedFolderState() != MappedFolderState::Normal) {
            if (m_dragTip) m_dragTip->hide();
            event->ignore();
            return;
        }
        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const bool isInternalSameFolderMove = isInternalDrag && !isCtrl && !preferShortcutForDrop && areAllPathsDirectChildrenOfFolder(paths, targetFolder);
        if (isInternalSameFolderMove) {
            if (m_dragTip) m_dragTip->hide();
            event->accept();
            return;
        }
        event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction));
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
        return;
    }

    if (!paths.isEmpty() && isInternalDrag && !preferShortcutForDrop && !isCtrl) {
        lv->resetRenameClickState();
        m_reorderDragPaths = paths;
        m_reorderInsertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
        if (m_dragTip) m_dragTip->hide();
        event->setDropAction(Qt::MoveAction);
        ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText);
        event->accept();
        update();
        return;
    }

    const QString clsid = hitState ? hitState->model.itemData.getCLSID() : QString();
    const bool isForbiddenTarget = (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS);
    event->setDropAction(isForbiddenTarget ? Qt::IgnoreAction : (preferShortcutForDrop ? Qt::LinkAction : (isCtrl ? Qt::CopyAction : Qt::MoveAction)));
    ListViewFileOpsHelper::updateInternalDragDescription(event->mimeData(), event->dropAction(), dropTargetText, isForbiddenTarget);
    event->accept();

    if (hitState) {
        showDropTipForItem(hitState, event->pos(), isCtrl, preferShortcutForDrop);
    } else if (isInternalDrag && !preferShortcutForDrop) {
        if (m_dragTip) m_dragTip->hide();
    } else {
        updateDragTip(event->pos(), isCtrl, preferShortcutForDrop);
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

    const QStringList paths = lv->resolveDraggedLocalPaths(mime);
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }

    const bool isInternalDrag = isDesktopOrBoxDragSource(event->source());
    const Qt::KeyboardModifiers effectiveModifiers = effectiveDragModifiers(event->keyboardModifiers());
    const bool copyMode = (effectiveModifiers & Qt::ControlModifier);
    const QStringList orderedPaths = isInternalDrag
        ? lv->orderedDraggedPaths(paths)
        : QStringList();
    const QStringList operationPaths = orderedPaths.isEmpty() ? paths : orderedPaths;
    const bool preferShortcutForDrop = isFolderMapped()
        ? shouldCreateShortcutForMappedFolderDrop(lv, isInternalDrag, orderedPaths, paths, effectiveModifiers)
        : lv->shouldPreferShortcutForDrop(paths, effectiveModifiers, !isInternalDrag);
    const DesktopRootItemState* hitState = dropTargetItemStateAt(event->pos());
    auto pathSetContains = [this](const QStringList& valuePaths, const QString& targetPath) {
        const QString normalizedTarget = normalizeBoxItemPath(targetPath);
        for (const QString& path : valuePaths) {
            if (normalizeBoxItemPath(path) == normalizedTarget) {
                return true;
            }
        }
        return false;
    };

    if (isFolderMapped() && hitState) {
        clearReorderPreview();
        if (pathSetContains(paths, hitState->model.filePath)) {
            qDebug() << "[MappedBoxDrop] ignored selected target"
                     << "sources=" << paths
                     << "target=" << hitState->model.filePath;
            event->ignore();
            return;
        }
        handleMappedBoxItemTargetDrop(operationPaths, hitState, copyMode, preferShortcutForDrop, event);
        return;
    }

    if (hitState && !pathSetContains(paths, hitState->model.filePath)) {
        clearReorderPreview();
        ListView::DropInteractionOptions options;
        options.copyMode = copyMode;
        options.preferShortcut = preferShortcutForDrop;
        options.finalizeMovedSourcesAfterSuccess = isInternalDrag;
        if (lv->handleDroppedPathsOnItemTarget(operationPaths, hitState->model, options)) {
            refreshLayout();
            update();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }

    if (isFolderMapped()) {
        clearReorderPreview();
        if (mappedFolderState() != MappedFolderState::Normal) {
            qDebug() << "[MappedBoxDrop] target background"
                     << "sources=" << operationPaths
                     << "target=" << mappedFolderPath()
                     << "type=background"
                     << "operation=ignored"
                     << "result=invalid mapped folder";
            event->ignore();
            return;
        }

        const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
        const QString operationName = preferShortcutForDrop
            ? QStringLiteral("shortcut")
            : (copyMode ? QStringLiteral("copy") : QStringLiteral("move"));
        const bool isInternalSameFolderMove = isInternalDrag && !copyMode && !preferShortcutForDrop && areAllPathsDirectChildrenOfFolder(paths, targetFolder);
        if (isInternalSameFolderMove) {
            qDebug() << "[MappedBoxDrop] target background"
                     << "sources=" << operationPaths
                     << "target=" << targetFolder
                     << "type=background"
                     << "operation=noop"
                     << "result=same mapped root";
            event->accept();
            return;
        }
        const QStringList srcPaths = toNativeQStringListPaths(operationPaths);
        if (targetFolder.isEmpty() || srcPaths.isEmpty()) {
            qDebug() << "[MappedBoxDrop] target background"
                     << "sources=" << operationPaths
                     << "target=" << targetFolder
                     << "type=background"
                     << "operation=" << operationName
                     << "result=invalid paths";
            event->ignore();
            return;
        }

        bool ok = false;
        if (preferShortcutForDrop) {
            ok = lv->createShortcutsInFolder(operationPaths, targetFolder);
        } else if (copyMode) {
            ok = CSystemApi::CopyFiles(srcPaths, targetFolder);
        } else {
            ok = CSystemApi::MoveFiles(srcPaths, targetFolder);
        }

        qDebug() << "[MappedBoxDrop] target background"
                 << "sources=" << operationPaths
                 << "target=" << targetFolder
                 << "type=background"
                 << "operation=" << operationName
                 << "result=" << ok;

        if (ok && !copyMode && !preferShortcutForDrop) {
            lv->finalizeMovedModelSourcesAfterFileOp(operationPaths);
        }

        if (ok) {
            lv->reloadMappedFolderBox(this);
            refreshLayout();
            update();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }

    if (isInternalDrag) {
        lv->resetRenameClickState();
        const int insertIndex = insertionIndexForPos(event->pos(), paths, &m_reorderIndicatorAfterPrev);
        clearReorderPreview();

        if (preferShortcutForDrop) {
            if (lv->importPathsToBox(operationPaths, this, true, insertIndex, true)) {
                event->setDropAction(Qt::LinkAction);
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        bool draggedPathsContainMappedSource = false;
        for (const QString& path : paths) {
            if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(path)) {
                if (sourceBox->isFolderMapped()) {
                    draggedPathsContainMappedSource = true;
                    break;
                }
            }
        }

        if (draggedPathsContainMappedSource) {
            if (lv->importPathsToBox(operationPaths, this, copyMode, insertIndex, preferShortcutForDrop)) {
                refreshLayout();
                update();
                raise();
                lv->requestSaveBoxes();
                event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        if (copyMode) {
            if (lv->importPathsToBox(operationPaths, this, true, insertIndex, false)) {
                event->setDropAction(Qt::CopyAction);
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        bool allFromThisBox = true;
          for (const QString& path : operationPaths) {
            if (!findItemStateByPath(path)) {
                allFromThisBox = false;
                break;
            }
        }

        if (allFromThisBox) {
            QSet<QString> draggedPathSet;
            QVector<DesktopRootItemState> draggedStates;
              for (const QString& path : operationPaths) {
                draggedPathSet.insert(normalizeBoxItemPath(path));
            }
            for (int i = 0; i < m_itemStates.size();) {
                if (draggedPathSet.contains(normalizeBoxItemPath(m_itemStates[i].model.filePath))) {
                    draggedStates.append(m_itemStates.takeAt(i));
                } else {
                    ++i;
                }
            }
            const int clampedIndex = qBound(0, insertIndex, m_itemStates.size());
            for (int i = 0; i < draggedStates.size(); ++i) {
                m_itemStates.insert(clampedIndex + i, draggedStates[i]);
            }
            m_lastLayoutItemCount = -1;
            refreshLayout();
            raise();
            lv->requestSaveBoxes();
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }

          for (int i = 0; i < operationPaths.size(); ++i) {
              lv->moveItemToBox(operationPaths[i], this, false, insertIndex >= 0 ? insertIndex + i : -1);
          }
        refreshLayout();
        raise();
        lv->requestSaveBoxes();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (lv->importPathsToBox(operationPaths, this, copyMode, -1, preferShortcutForDrop)) {
        event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (copyMode ? Qt::CopyAction : Qt::MoveAction));
        event->accept();
    } else {
        event->ignore();
    }
    clearReorderPreview();
}

void LinBox::keyPressEvent(QKeyEvent* event) {
    ListView* lv = ListView::instance();
    if (lv) {
        const bool shiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        const bool extendSelection = event->modifiers() & Qt::ShiftModifier;
        const bool ctrlPressed = event->modifiers() & Qt::ControlModifier;
        const bool shortcutPaste = ctrlPressed && (event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_V;
        const bool shortcutNewFolder = ctrlPressed && extendSelection && event->key() == Qt::Key_N;
        const bool permanentDelete = shiftPressed && (event->key() == Qt::Key_Delete);
        //输出对应的键值
        qDebug() << QString("Press Key: %1, Shift: %2, Delete: %3, permanentDelete: %4")
            .arg(QKeySequence(event->key()).toString())
            .arg(shiftPressed ? "Yes" : "No")
            .arg(event->key() == Qt::Key_Delete ? "Yes" : "No")
            .arg(permanentDelete ? "Yes" : "No");

        //first judge if it's a Shift+Delete for permanent delete, if not, then handle regular Delete key
        if (permanentDelete) {
            lv->onMenuDelete(true);
            event->accept();
            qDebug() << "[Box]-Permanent Delete triggered";
            return;
        }
        if (event->matches(QKeySequence::SelectAll)) {
            QStringList allPaths;
            allPaths.reserve(m_itemStates.size());
            for (const DesktopRootItemState& state : m_itemStates) {
                allPaths.push_back(state.model.filePath);
            }
            lv->setSelectedItemPaths(allPaths, allPaths.isEmpty() ? QString() : allPaths.last(), false);
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
                const QStringList srcPaths = toNativeQStringListPaths(filePaths);
                if (preferShortcut) {
                    ok = lv->createShortcutsInFolder(filePaths, targetFolder);
                } else if (isCutOperation) {
                    ok = CSystemApi::MoveFiles(srcPaths, targetFolder);
                } else {
                    ok = CSystemApi::CopyFiles(srcPaths, targetFolder);
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
        if (shortcutNewFolder) {
            lv->createNewFolderInHost(this);
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
            const QString targetPath = !lv->focusedItemPath().isEmpty()
                ? lv->focusedItemPath()
                : (lv->selectedItemPaths().isEmpty() ? QString() : lv->selectedItemPaths().first());
            if (!targetPath.isEmpty() && lv->showPropertiesForPath(targetPath)) {
                event->accept();
                return;
            }
        }
        if (ctrlPressed && event->key() == Qt::Key_Space) {
            const QString targetPath = lv->focusedItemPath();
            if (!targetPath.isEmpty()) {
                lv->toggleItemSelection(targetPath);
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space) {
            const DesktopRootItemState* hoveredState = m_hoveredItemPath.isEmpty()
                ? nullptr
                : findItemStateByPath(m_hoveredItemPath);
            const QString targetPath = hoveredState
                ? hoveredState->model.filePath
                : (!lv->focusedItemPath().isEmpty()
                    ? lv->focusedItemPath()
                    : (lv->selectedItemPaths().isEmpty() ? QString() : lv->selectedItemPaths().first()));
            if (!targetPath.isEmpty() && lv->sendPreviewRequestByPath(targetPath)) {
                event->accept();
                return;
            }
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            const QString targetPath = !lv->focusedItemPath().isEmpty()
                ? lv->focusedItemPath()
                : (lv->selectedItemPaths().isEmpty() ? QString() : lv->selectedItemPaths().first());
            if (!targetPath.isEmpty() && lv->openItemByPath(targetPath)) {
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
        if (event->key() == Qt::Key_F2 && lv->selectedItemPaths().size() == 1) {
            RenameManager::instance()->startRename(lv->selectedItemPaths().first());
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Delete) {
        if (lv) {
            const Qt::KeyboardModifiers modifiers = event->modifiers() | QGuiApplication::keyboardModifiers();
            bool shiftPressed = modifiers & Qt::ShiftModifier;
            lv->onMenuDelete(shiftPressed);
            event->accept();
            qDebug() << "[Box]-Delete triggered, Shift pressed:" << shiftPressed;
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
        
        if (!hitItemStateAt(pos)) {
            return true;
        }
    }
    
    return false;
}

void LinBox::updateCursorShape(const QPoint& pos) {
    if (mappedPlaceholderRect().contains(pos)) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (m_titleBar && isFolderMapped() && m_titleBar->geometry().contains(pos)) {
        const QPoint titlePos = m_titleBar->mapFrom(this, pos);
        if (mappedTitleIconRect().contains(titlePos)) {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }
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
    iconFont.setPixelSize(scaledPx(this, 14));
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

void LinBox::showCustomContextMenu(const QPoint& globalPos, bool extendedVerbs) {
    if (ListView* lv = ListView::instance()) {
        lv->armPendingShellNewTargetBox(this);
    }

    ShellContextMenu scm;
    logBoxDpiTrace("showCustomContextMenu", this, globalPos);
    const QPoint nativeGlobalPos = DpiCoordinateHelper::qtGlobalPointToNativePoint(this, globalPos);
    POINT p = { nativeGlobalPos.x(), nativeGlobalPos.y() };
    const POINT* position = &p;
    HWND ownerHwnd = (HWND)this->winId();

    RightMenu::getInstance()->preparePasteActionsForMenu();
    scm.DisplayBoxBackgroundContextMenu();
    const auto exclusionKeys = ShellContextMenu::ExclusionKeysForPath(ShellContextMenu::OperationType::boxBackground, nullptr);
    scm.setExclusions(ShellMenuExclusionStore::loadForKeys(exclusionKeys));
    const QString contextPath = isFolderMapped()
        ? normalizeMappedFolderPath(mappedFolderPath())
        : defaultDesktopPath();
    if (!contextPath.isEmpty()) {
        scm.setFilesystemDirectory(contextPath.toStdString());
    }

    std::vector<ShellContextMenu::CustomMenuItem> items;
    std::vector<ShellContextMenu::CustomMenuItem> boxItems;
    const QPointer<LinBox> boxGuard(this);

    // 1. Add Box-specific context actions
    // Rename Box
    ShellContextMenu::CustomMenuItem renameItem;
    QIcon renameIcon(":/Assets/rename.svg");
    renameItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.rename"), renameIcon, this);
    renameItem.text = tr("Rename Box").toStdWString();
    renameItem.callback = [boxGuard]() {
        if (boxGuard) {
            boxGuard->beginTitleEditing();
        }
    };
    boxItems.push_back(renameItem);

    if (isFolderMapped()) {
        ShellContextMenu::CustomMenuItem remapItem;
        QIcon remapIcon(":/Assets/change_Mapped_Folder.svg");
        remapItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.remap"), remapIcon, this);
        remapItem.text = tr("Change Mapped Folder").toStdWString();
        remapItem.callback = [boxGuard]() {
            if (boxGuard) {
                boxGuard->remapMappedFolderInteractively();
            }
        };
        boxItems.push_back(remapItem);
    }

    // Toggle View mode
    ShellContextMenu::CustomMenuItem viewItem;
    QIcon viewIcon;
    if(m_showType == BoxShowType::List_ShowType){
        viewIcon = QIcon(":/Assets/list.svg");
    }else{
        viewIcon = QIcon(":/Assets/grid.svg");
    }
    viewItem.hBitmap = cachedMenuBitmap(
        m_showType == BoxShowType::List_ShowType ? QStringLiteral("box.view.list") : QStringLiteral("box.view.grid"),
        viewIcon,
        this);
    viewItem.text = (m_showType == BoxShowType::Icon_ShowType ? tr("List View") : tr("Icon View")).toStdWString();
    viewItem.callback = [boxGuard]() {
        if (boxGuard) {
            boxGuard->setShowType(boxGuard->m_showType == BoxShowType::Icon_ShowType
                        ? BoxShowType::List_ShowType
                        : BoxShowType::Icon_ShowType);
        }
    };
    boxItems.push_back(viewItem);

    // Settings
    ShellContextMenu::CustomMenuItem settingsItem;
    QIcon settingsIcon(":/Assets/settings.svg");
    settingsItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.settings"), settingsIcon, this);
    settingsItem.text = tr("Box Settings").toStdWString();
    settingsItem.callback = [boxGuard]() {
        if (boxGuard && ListView::instance()) {
            ListView::instance()->showSettings(boxGuard.data());
        }
    };
    boxItems.push_back(settingsItem);

    // Destroy Box
    ShellContextMenu::CustomMenuItem destroyItem;
    destroyItem.text = tr("Destroy Box").toStdWString();
    QIcon icon(":/Assets/destroy.svg");
    destroyItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.destroy"), icon, this);
    destroyItem.callback = [boxGuard]() {
        if (boxGuard) {
            emit boxGuard->destroyRequested(boxGuard.data());
        }
    };
    boxItems.push_back(destroyItem);

    // 2. Get shared actions from RightMenu and convert them
    const auto sharedItems = RightMenu::getInstance()->filteredSharedBoxCustomMenuItems();
    if (!sharedItems.empty()) {
        items.insert(items.end(), sharedItems.begin(), sharedItems.end());
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
    scm.setCustomMenuItems(RightMenu::filterCustomItemsForExclusionKey(items, L"box_background"));
    scm.ShowMenu(ownerHwnd, position, nullptr,
        extendedVerbs ? ShellContextMenu::Flags::ExtendedVerbs : ShellContextMenu::Flags::Standard);
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

    QPoint targetPos = pos + QPoint(scaledPx(this, kBaseDragTipOffset), scaledPx(this, kBaseDragTipOffset));
    if (targetPos.x() + m_dragTip->width() > width()) targetPos.setX(pos.x() - m_dragTip->width() - scaledPx(this, kBaseDragTipMargin));
    if (targetPos.y() + m_dragTip->height() > height()) targetPos.setY(pos.y() - m_dragTip->height() - scaledPx(this, kBaseDragTipMargin));

    m_dragTip->showAt(mapToGlobal(targetPos));
}

