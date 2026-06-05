#include "desktop/DesktopRootItemRenderData.h"
#include "desktop/DesktopItemRenderCacheHelper.h"
#include "desktop/DesktopRootItemModel.h"
#include "desktop/DesktopRootItemState.h"
#include "models/data/ListItemData.h"
#include "services/DpiScaleHelper.h"
#include "desktop/RenameManager.h"
#include "services/DesktopItemLoader.h"
#include "SystemApi/SystemApi.h"

#include <QFontMetrics>
#include <QPainter>
#include <QDir>
#include <QImageReader>
#include <QtWinExtras/qwinfunctions.h>
#include <shellapi.h>
#include "services/IconCacheManager.h"
#include "services/SysInfoUtil.h"

namespace {

int scalePxByDpi(qreal dpiScale, int logicalPx)
{
    return qRound(logicalPx * dpiScale);
}

bool isShortcutSuffix(const QFileInfo& fi)
{
    const QString suffix = fi.suffix().toLower();
    return suffix == "lnk" || suffix == "url" || suffix == "appref-ms";
}

bool isWrapSeparator(QChar ch)
{
    return ch.isSpace() || ch == '-' || ch == '_' || ch == '.' || ch == '(' || ch == ')' || ch == '[' || ch == ']';
}

QRect buildSelectionContentRect(const DesktopRootItemModel& model, const DesktopRootItemRenderData& rd, qreal dpiScale)
{
    if (model.layoutMode != DesktopLayoutMode::IconMode) {
        const int inset = scalePxByDpi(dpiScale, 2);
        return rd.itemRect.adjusted(inset, inset, -inset, -inset);
    }

    const int textHeight = qMax(1, model.visibleTextHeight);
    const QRect visibleTextRect(
        rd.textRect.left(),
        rd.textRect.top(),
        rd.textRect.width(),
        qMin(textHeight, rd.textRect.height()));
    return rd.iconRect.united(visibleTextRect);
}

int desiredDesktopItemHeight(const DesktopRootItemModel& model,
                             const DesktopRootItemRenderData& rd,
                             const DesktopRenderParams& params,
                             int baseItemHeight,
                             int visibleTextHeight,
                             int lineHeight)
{
    if (model.layoutMode != DesktopLayoutMode::IconMode) {
        return baseItemHeight;
    }

    const auto sp = [&](int px) { return scalePxByDpi(params.dpiScale, px); };
    const int textHeight = qMax(visibleTextHeight, lineHeight);
    const int contentBottom = rd.textRect.top() + textHeight;
    return qMax(baseItemHeight, contentBottom - model.position.y() + sp(2));
}

QStringList buildDesktopRootLines(const QString& text, const QFontMetrics& metrics, int width, int maxLines)
{
    QString remaining = text;
    remaining.replace('\n', ' ');
    QStringList lines;
    if (remaining.isEmpty() || width <= 0 || maxLines <= 0) {
        lines << remaining;
        return lines;
    }
    while (!remaining.isEmpty() && lines.size() < maxLines) {
        if (metrics.horizontalAdvance(remaining) <= width) {
            lines << remaining;
            break;
        }
        if (lines.size() == maxLines - 1) {
            lines << metrics.elidedText(remaining, Qt::ElideRight, width);
            break;
        }
        int bestFit = 0, preferredBreak = -1;
        for (int i = 1; i <= remaining.size(); ++i) {
            const QString candidate = remaining.left(i);
            if (metrics.horizontalAdvance(candidate) > width) break;
            bestFit = i;
            if (isWrapSeparator(remaining.at(i - 1))) preferredBreak = i;
        }
        int splitIndex = preferredBreak;
        if (splitIndex <= 0 || bestFit - splitIndex > 3) splitIndex = bestFit;
        if (splitIndex <= 0) splitIndex = 1;
        QString currentLine = remaining.left(splitIndex).trimmed();
        if (currentLine.isEmpty()) currentLine = remaining.left(splitIndex);
        lines << currentLine;
        remaining = remaining.mid(splitIndex).trimmed();
    }
    return lines;
}

QString buildDisplayName(const ListItemData& data, const QFileInfo& fileInfo, ObjectType type, const QString& filePath)
{
    if (type == ObjectType::SpecialFolder) return data.fullName();
    if (type == ObjectType::DiskLetter) {
        const QString fullName = data.fullName().trimmed();
        return fullName.isEmpty() ? QDir::toNativeSeparators(filePath) : fullName;
    }
    if (isShortcutSuffix(fileInfo)) {
        const QString shortcutName = fileInfo.completeBaseName();
        return shortcutName.isEmpty() ? data.fullName() : shortcutName;
    }
    if (fileInfo.isFile() || fileInfo.isDir()) {
        const QString name = fileInfo.fileName();
        return name.isEmpty() ? data.fullName() : name;
    }
    return data.fullName();
}

QString buildIconIdentityKey(const ListItemData& data, ObjectType type, const QFileInfo& fileInfo, const QString& normalizedPath)
{
    if (type == ObjectType::SpecialFolder) return QString("special:%1").arg(data.getCLSID());
    const QString suffix = fileInfo.suffix().toLower();
    if (fileInfo.isDir()) return QString("dir:v3:%1").arg(normalizedPath);
    if (suffix == "exe" || suffix == "msi" || suffix == "msp" || suffix == "msu" ||
        suffix == "dll" || suffix == "cpl" || suffix == "scr" ||
        suffix == "lnk" || suffix == "url" || suffix == "ico" || suffix == "appref-ms") {
        return QString("path:%1").arg(normalizedPath);
    }
    if (!suffix.isEmpty()) return QString("ext:%1").arg(suffix);
    return QString("path:%1").arg(normalizedPath);
}

bool checkFileHiddenAttribute(const QString& filePath)
{
    if (filePath.isEmpty() || filePath.startsWith("::")) return false;
    const std::wstring nativePath = QDir::toNativeSeparators(filePath).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN);
}

QString normalizeClsidString(QString clsid)
{
    return clsid.replace("::", "").replace("{", "").replace("}", "").toLower();
}

int fixedIconSideForParams(const DesktopRenderParams& params, DesktopLayoutMode layoutMode, int itemWidth)
{
    if (layoutMode == DesktopLayoutMode::ListMode) return scalePxByDpi(params.dpiScale, 20);
    const int largeThreshold = params.largeIconW;
    const int smallThreshold = params.smallIconW;
    if (itemWidth >= largeThreshold) return scalePxByDpi(params.dpiScale, 96);
    if (itemWidth <= smallThreshold) return scalePxByDpi(params.dpiScale, 32);
    return scalePxByDpi(params.dpiScale, 48);
}

QPixmap buildCroppedRenderPix(const QIcon& icon, int iconSide)
{
    if (icon.isNull() || iconSide <= 0) return QPixmap();
    const int requestSide = qMax(iconSide, 256);
    QPixmap pix = icon.pixmap(requestSide, requestSide);
    if (pix.isNull()) pix = icon.pixmap(iconSide, iconSide);
    if (pix.isNull()) return QPixmap();
    bool wasCropped = false;
    const QImage cropped = SysInfoUtil::autoCropImage(pix.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied), &wasCropped);
    if (wasCropped && !cropped.isNull()) pix = QPixmap::fromImage(cropped);
    if (pix.width() != iconSide || pix.height() != iconSide) pix = pix.scaled(iconSide, iconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return pix;
}

QPixmap applyOpacityEffect(const QPixmap& source, qreal opacity)
{
    if (source.isNull()) return source;
    QPixmap result(source.size());
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(opacity);
    p.drawPixmap(0, 0, source);
    return result;
}

} // anonymous namespace

DesktopRootItemRenderData buildDesktopRootItemRenderData(const DesktopRootItemState& state, bool forceSelected)
{
    DesktopRootItemRenderData rd = state.model.renderData;
    if (forceSelected) { rd.selected = true; rd.hovered = false; }
    return rd;
}

void initDesktopRootItemModel(DesktopRootItemModel& model, const ListItemData& data, const DesktopItemInitContext& context)
{
    model.itemData = data;
    model.objectType = data.type();
    model.specialFolder = data.specialFolderType();
    model.clsid = data.getCLSID();
    model.filePath = (model.objectType == ObjectType::SpecialFolder) ? data.itemData().path : data.filePath();
    model.normalizedPath = QDir::toNativeSeparators(model.filePath);
    while (model.normalizedPath.endsWith('\\')) model.normalizedPath.chop(1);
    model.fileInfo = QFileInfo(model.filePath);
    model.displayName = buildDisplayName(data, model.fileInfo, model.objectType, model.filePath);
    model.isShortcut = isShortcutSuffix(model.fileInfo);
    if (context.getBoxId) model.boxId = context.getBoxId(model.filePath);
    const QString iconKey = buildIconIdentityKey(data, model.objectType, model.fileInfo, model.normalizedPath);
    model.iconCacheKey = iconKey;
    model.sourceIcon = IconCacheManager::instance().getIcon(QString("%1|base|%2").arg(iconKey).arg(ListItemData::s_targetIconSize), [&data]() {
        ListItemData mData = data; mData.initIcon(); return mData.icon();
    });
    const bool isSpecial = !model.isShortcut && ((model.objectType == ObjectType::SpecialFolder) || context.systemFolderCLSIDs.contains(model.clsid) || model.fileInfo.fileName().toLower() == "desktop.ini" || model.filePath.startsWith("::"));
    const bool isOsHidden = checkFileHiddenAttribute(model.filePath);
    const bool isAppHidden = DesktopItemLoader::shouldTreatAsHiddenShellFolder(data);
    model.isHiddenFileVisual = (isOsHidden || isAppHidden);
    bool isVisibleSpecial = false;
    if (isSpecial) {
        QString normClsid = normalizeClsidString(model.clsid);
        for (const auto& info : context.sysIconRegisterInfo) { if (normalizeClsidString(info.clsid) == normClsid) { isVisibleSpecial = !info.isHidden; break; } }
    }
    model.visible = (model.objectType != ObjectType::DiskLetter) && !isAppHidden && model.boxId.isEmpty() && ((isSpecial && isVisibleSpecial) || (!isSpecial && (context.showHiddenFiles || !isOsHidden)));
    model.isReady = true;
}

DesktopRootItemRenderData buildRenderDataFromModel(const DesktopRootItemModel& model, const DesktopRenderParams& params)
{
    DesktopRootItemRenderData rd;
    const auto sp = [&](int px) { return scalePxByDpi(params.dpiScale, px); };
    rd.filePath = model.filePath;
    rd.displayName = model.displayName;
    const int itemWidth = params.iconWidth;
    const int itemHeight = params.iconHeight;
    const int iconSide = qBound(sp(16), fixedIconSideForParams(params, model.layoutMode, itemWidth), sp(128));
    const bool listMode = model.layoutMode == DesktopLayoutMode::ListMode;
    const int clampedIconSide = qMin(iconSide, listMode ? (itemHeight - sp(6)) : (itemWidth - sp(4)));
    int textTop = 0;
    if (listMode) {
        const int leftPadding = sp(8);
        const int rightPadding = sp(12);
        const int iconY = model.position.y() + qMax(0, (itemHeight - clampedIconSide) / 2);
        rd.iconRect = QRect(model.position.x() + leftPadding, iconY, clampedIconSide, clampedIconSide);
        const int textLeft = rd.iconRect.right() + 1 + sp(params.iconTextGap);
        const int textWidth = qMax(sp(10), itemWidth - (textLeft - model.position.x()) - rightPadding);
        textTop = model.position.y() + sp(2);
        rd.textRect = QRect(textLeft, textTop, textWidth, qMax(sp(18), itemHeight - sp(4)));
    } else {
        rd.iconRect = QRect(model.position.x() + (itemWidth - clampedIconSide) / 2, model.position.y() + sp(4), clampedIconSide, clampedIconSide);
        textTop = rd.iconRect.bottom() + sp(params.iconTextGap);
        const int textWidth = qMax(sp(10), itemWidth - sp(4));
        rd.textRect = QRect(model.position.x() + sp(2), textTop, textWidth, qMax(sp(18), itemHeight - (textTop - model.position.y())));
    }
    QPixmap rawPix;
    if (model.objectType != ObjectType::SpecialFolder && model.fileInfo.isFile() && !model.isShortcut) {
        const QString pKey = QString("image_preview:%1|%2|%3").arg(QDir::toNativeSeparators(model.fileInfo.absoluteFilePath())).arg(model.fileInfo.lastModified().toMSecsSinceEpoch()).arg(clampedIconSide);
        rawPix = IconCacheManager::instance().getPixmap(pKey, [&]() {
            QImageReader r(model.fileInfo.absoluteFilePath()); r.setAutoTransform(true); if (!r.canRead()) return QPixmap();
            QImage img = r.read(); if (img.isNull()) return QPixmap();
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const QSize ts(clampedIconSide, clampedIconSide);
            const QImage s = img.scaled(ts, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmap p(ts); p.fill(Qt::transparent); QPainter pp(&p); pp.setRenderHint(QPainter::SmoothPixmapTransform);
            pp.drawImage(QPoint((ts.width() - s.width()) / 2, (ts.height() - s.height()) / 2), s); return p;
        });
    }
    if (rawPix.isNull()) rawPix = IconCacheManager::instance().getPixmap(QString("%1|render|%2").arg(model.iconCacheKey).arg(clampedIconSide), [&]() { return buildCroppedRenderPix(model.sourceIcon, clampedIconSide); });
    if (rawPix.isNull()) rawPix = buildCroppedRenderPix(SysInfoUtil::SysFileIconByDLL(0, ListItemData::s_targetIconSize, "shell32.dll"), clampedIconSide);
    if (model.objectType != ObjectType::SpecialFolder && model.fileInfo.isDir() && !rawPix.isNull()) rawPix = SysInfoUtil::sanitizeFolderPreviewMatte(rawPix);
    if (model.objectType == ObjectType::DiskLetter && !rawPix.isNull()) rawPix = applyOpacityEffect(rawPix, 0.8);
    if (model.isHiddenFileVisual && !rawPix.isNull()) rawPix = applyOpacityEffect(rawPix, 0.45);
    rd.iconPixmap = rawPix;
    if (model.isShortcut && params.showShortcutArrow) {
        const int as = listMode ? sp(12) : sp(32);
        rd.shortcutArrowPixmap = IconCacheManager::instance().getPixmap(QString("shortcut_arrow|%1").arg(as), [as]() {
            QIcon ai = SysInfoUtil::SysFileIconByDLL(155, 32, "imageres.dll"); if (ai.isNull()) ai = SysInfoUtil::SysFileIconByDLL(30, 32, "shell32.dll"); return ai.pixmap(as, as);
        });
        if (listMode) {
            rd.shortcutArrowRect = QRect(rd.iconRect.right() - as + 1, rd.iconRect.bottom() - as + 1, as, as);
        } else {
            rd.shortcutArrowRect = QRect(rd.iconRect.left(), rd.iconRect.bottom() - as, as, as);
        }
    }
    rd.labelFont = params.labelFont;
    const QFontMetrics fm(rd.labelFont);
    const QString renderText = model.expandedPreviewText.isEmpty() ? model.displayName : model.expandedPreviewText;
    int visibleTextHeight = 0;
    if (model.layoutMode == DesktopLayoutMode::IconMode) {
        if (!model.isRenaming) {
            rd.textLines = buildDesktopRootLines(renderText, fm, rd.textRect.width(), model.isFullNameExpanded ? 64 : 2);
            visibleTextHeight = qMax(1, rd.textLines.size()) * fm.lineSpacing();
        }
        rd.textOptions.alignment = Qt::AlignHCenter | Qt::AlignTop; rd.textOptions.outlineWidth = 1;
    } else {
        if (!model.isRenaming) {
            rd.textLines << fm.elidedText(renderText, Qt::ElideRight, rd.textRect.width());
            visibleTextHeight = fm.lineSpacing();
        }
        rd.textOptions.alignment = Qt::AlignVCenter | Qt::AlignLeft; rd.textOptions.outlineWidth = 1;
    }
    if (visibleTextHeight <= 0) {
        visibleTextHeight = qMax(model.visibleTextHeight, fm.lineSpacing());
    }
    const int effectiveHeight = desiredDesktopItemHeight(model, rd, params, itemHeight, visibleTextHeight, fm.lineSpacing());
    rd.textRect.setHeight(qMax(sp(18), effectiveHeight - (textTop - model.position.y())));
    rd.itemRect = QRect(model.position, QSize(itemWidth, effectiveHeight));
    DesktopRootItemModel modelForGeometry = model;
    modelForGeometry.visibleTextHeight = visibleTextHeight;
    rd.selectionContentRect = buildSelectionContentRect(modelForGeometry, rd, params.dpiScale);
    rd.renameEditorRect = rd.textRect;
    rd.renameEditorRect.setHeight(qMax(sp(22), rd.renameEditorRect.height()));
    rd.staticContentRect = rd.iconRect.united(rd.textRect);
    if (!rd.shortcutArrowRect.isNull()) {
        rd.staticContentRect = rd.staticContentRect.united(rd.shortcutArrowRect);
    }
    rd.outerHighlightRect = rd.selectionContentRect.adjusted(-sp(3), -sp(3), sp(3), sp(3));
    rd.innerHighlightRect = rd.selectionContentRect.adjusted(sp(1), sp(1), -sp(1), -sp(1));
    rd.outerHighlightRadius = sp(8);
    rd.innerHighlightRadius = sp(2);
    rd.selectionBorderWidth = qMax<qreal>(1.0, params.dpiScale * 0.9);
    rd.textOptions.textColor = QColor(255, 255, 255); rd.textOptions.outlineColor = QColor(0, 0, 0, 105); rd.textOptions.outlineOffset = QPoint(0, 1);
    rd.textOptions.glowColor = QColor(0, 0, 0, 52); rd.textOptions.glowOffset = QPoint(0, 1); rd.textOptions.largeGlowRadius = 1; rd.textOptions.tightGlowRadius = 1;
    rd.selected = model.isSelected;
    rd.hovered = model.isHovered && !model.isSelected;
    rd.cutMarked = model.isCutMarked;
    rd.expandedFullName = model.isFullNameExpanded;
    prepareDesktopRootItemStaticContentCache(rd);
    return rd;
}

DesktopRootItemRenderData localizeDesktopRootItemRenderData(
    const DesktopRootItemRenderData& item,
    const QPoint& origin)
{
    DesktopRootItemRenderData localized = item;
    const QPoint offset = -origin;
    localized.itemRect.translate(offset);
    localized.iconRect.translate(offset);
    localized.textRect.translate(offset);
    localized.renameEditorRect.translate(offset);
    localized.shortcutArrowRect.translate(offset);
    localized.selectionContentRect.translate(offset);
    localized.staticContentRect.translate(offset);
    localized.outerHighlightRect.translate(offset);
    localized.innerHighlightRect.translate(offset);
    return localized;
}

QRect desktopRootItemHighlightDirtyRect(const DesktopRootItemState& state)
{
    return desktopRootItemHighlightDirtyRect(state.model.renderData);
}

bool openDesktopRootItem(const DesktopRootItemModel& model)
{
    if (model.filePath.isEmpty()) {
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(model.filePath);
    if (QFileInfo(nativePath).isDir()) {
        const QString explorerArgument = QStringLiteral("\"%1\"").arg(nativePath);
        HINSTANCE result = ShellExecuteW(nullptr,
            L"open",
            L"explorer.exe",
            reinterpret_cast<LPCWSTR>(explorerArgument.utf16()),
            nullptr,
            SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    CSystemApi::File_RightMenuOperations(nativePath, Z_OPEN);
    return true;
}
