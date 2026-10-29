#include "ui/SettingUI.h"
#include "ui/Fluent/SmoothScrollArea.h"
#include "Version.h"
#include "box/LinBox.h"
#include "box/BoxManager.h"
#include "desktop/ListView.h"
#include <windows.h>
#include <winuser.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <QScreen>
#include <QApplication>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QPainter>
#include <QFrame>
#include <QCheckBox>
#include <QImageReader>
#include "desktop/ListView.h"
#include "desktop/LinDesktop.h"
#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/LinWidgetProjectTools.h"
#include "desktop/widgets/runtime_export.h"
#include "desktop/runtime/RuntimeInstaller.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInput.h"
#include "services/update/UpdateChecker.h"
#include "services/update/UpdateDownloader.h"
#include <QProcess>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTextCursor>
#include <QDateTime>
#include <windowsx.h>
#include <QStackedWidget>
#include "plugins/PluginManager.h"
#include <QStyledItemDelegate>
#include <QPainterPath>
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentButton.h"
#include "ui/Fluent/FluentCheckBox.h"
#include "ui/Fluent/FluentComboBox.h"
#include "ui/Fluent/FluentCard.h"
#include "ui/Fluent/FluentColorWheel.h"
#include "app/AppFonts.h"
#include "app/LanguageManager.h"
#include "services/AppPaths.h"
#include "services/ShellMenuExclusionStore.h"
#include "services/SysInfoUtil.h" // For ShortcutArrowManager
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QComboBox>
#include <QListWidget>
#include <QLayoutItem>
#include <QFileDialog>
#include <QInputDialog>
#include <QDesktopServices>
#include <QSignalBlocker>
#include <QSettings>
#include <QTabWidget>
#include <QUrl>
#include <QDir>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QColorDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QtWinExtras/QtWin>
#include <functional>
#include <memory>
#include <algorithm>
#include "desktop/RightMenu.h"
#include <ShellContextMenu.h>

// Custom delegate to draw the Fluent Design 2 style sidebar
class SidebarDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);

        bool isSelected = (opt.state & QStyle::State_Selected);
        bool isHovered = (opt.state & QStyle::State_MouseOver);

        const QColor accentColor(24, 112, 255);
        const QColor selectedBg(232, 242, 255);
        const QColor selectedBorder(214, 228, 247);
        const QColor hoverBg(246, 250, 255);
        const QColor textPrimary(32, 45, 69);
        const QColor textSecondary(72, 86, 112);
        const QColor iconSelected(24, 112, 255);
        const QColor iconNormal(72, 86, 112);
        const QColor iconHover(42, 58, 86);

        const int itemHeight = opt.rect.height();
        const int iconSize = 40;
        const int itemLeft = 12;
        const int itemRight = 10;
        const int iconLeftPadding = 34;
        const int textLeftPadding = 76;

        if (isSelected || isHovered) {
            QRect bgRect = opt.rect.adjusted(itemLeft, 3, -itemRight, -3);
            QPainterPath path;
            path.addRoundedRect(bgRect, 9, 9);
            
            if (isSelected) {
                painter->fillPath(path, selectedBg);
                painter->setPen(QPen(selectedBorder, 1));
                painter->drawPath(path);
            } else if (isHovered) {
                painter->fillPath(path, hoverBg);
            }
        }

        if (isSelected) {
            const int barWidth = 3;
            const int barHeight = qMin(24, itemHeight - 30);
            QRect barRect(opt.rect.left() + iconLeftPadding - 10,
                         opt.rect.top() + (itemHeight - barHeight) / 2, 
                         barWidth, barHeight);
            painter->setBrush(accentColor);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(barRect, 2, 2);
        }

        const QString glyphText = index.data(Qt::UserRole + 1).toString();
        QRect iconRect(opt.rect.left() + iconLeftPadding, 
                       opt.rect.top() + (itemHeight - iconSize) / 2, 
                       iconSize, iconSize);

        const QColor iconColor = isSelected ? iconSelected : (isHovered ? iconHover : iconNormal);
        if (!glyphText.isEmpty()) {
            QFont iconFont("Segoe Fluent Icons");
            if (!QFontInfo(iconFont).exactMatch()) {
                iconFont.setFamily("Segoe MDL2 Assets");
            }
            iconFont.setPixelSize(28);
            iconFont.setWeight(QFont::Normal);
            painter->setFont(iconFont);
            painter->setPen(iconColor);
            painter->drawText(iconRect, Qt::AlignCenter, glyphText);
        }

        QString text = index.data(Qt::DisplayRole).toString();
        painter->setPen(isSelected ? accentColor : (isHovered ? textPrimary : textSecondary));
        
        QFont font = painter->font();
        font.setFamily(AppFonts::uiFamily());
        font.setPixelSize(13);
        font.setWeight(QFont::Weight::Medium);
        painter->setFont(font);
        
        QRect textRect(opt.rect.left() + textLeftPadding, opt.rect.top(), 
                       opt.rect.width() - textLeftPadding - 16, itemHeight);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        painter->restore();
    }
};

class MenuListMousePressFilter : public QObject {
public:
    MenuListMousePressFilter(QListWidget* list, std::function<bool(const QPoint&, Qt::KeyboardModifiers)> mousePressHandler)
        : QObject(list), m_list(list), m_mousePressHandler(std::move(mousePressHandler))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!m_list || watched != m_list->viewport() || event->type() != QEvent::MouseButtonPress) {
            return QObject::eventFilter(watched, event);
        }

        const QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (m_mousePressHandler && m_mousePressHandler(mouseEvent->pos(), mouseEvent->modifiers())) {
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QListWidget* m_list = nullptr;
    std::function<bool(const QPoint&, Qt::KeyboardModifiers)> m_mousePressHandler;
};

namespace {
void ensureSegoeFluentIconsLoaded()
{
    static bool loaded = false;
    if (loaded) {
        return;
    }
    loaded = true;
    QFontDatabase::addApplicationFont(QStringLiteral(":/Assets/Segoe Fluent Icons.ttf"));
}

QString settingsConfigPath()
{
    return AppPaths::configFilePath();
}

QStringList shellMenuCustomExtensionKeys()
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    QStringList keys = settings.value(QStringLiteral("ShellMenuExclusions/CustomExtensionKeys")).toStringList();
    keys.removeDuplicates();
    keys.removeAll(QString());
    return keys;
}

void saveShellMenuCustomExtensionKeys(QStringList keys)
{
    keys.removeDuplicates();
    keys.removeAll(QString());
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ShellMenuExclusions/CustomExtensionKeys"), keys);
    settings.sync();
}

bool isUsableShellMenuBitmap(HBITMAP bitmap)
{
    const quintptr rawValue = reinterpret_cast<quintptr>(bitmap);
    return bitmap && rawValue > 16 && rawValue != static_cast<quintptr>(-1);
}

QIcon shellMenuBitmapIcon(HBITMAP bitmap)
{
    if (!isUsableShellMenuBitmap(bitmap)) {
        return QIcon();
    }

    const QPixmap pixmap = QtWin::fromHBITMAP(bitmap, QtWin::HBitmapAlpha);
    return pixmap.isNull() ? QIcon() : QIcon(pixmap);
}

QStringList wideVectorToStringList(const std::vector<std::wstring>& values)
{
    QStringList result;
    for (const std::wstring& value : values) {
        result.append(QString::fromStdWString(value));
    }
    return result;
}

ShellMenuExclusionEntry shellMenuEntryFromItem(QListWidgetItem* item)
{
    ShellMenuExclusionEntry entry;
    if (!item) {
        return entry;
    }

    entry.rawText = item->data(Qt::UserRole).toString().toStdWString();
    entry.normalizedText = item->data(Qt::UserRole + 1).toString().toStdWString();
    entry.matchText = item->data(Qt::UserRole + 5).toString().toStdWString();
    const QStringList menuPath = item->data(Qt::UserRole + 6).toStringList();
    const QStringList matchPath = item->data(Qt::UserRole + 7).toStringList();
    entry.menuPath.reserve(static_cast<size_t>(menuPath.size()));
    entry.matchPath.reserve(static_cast<size_t>(matchPath.size()));
    for (const QString& segment : menuPath) {
        entry.menuPath.push_back(segment.toStdWString());
    }
    for (const QString& segment : matchPath) {
        entry.matchPath.push_back(segment.toStdWString());
    }
    entry.enabled = true;
    return entry;
}

bool shellMenuEntryMatches(const std::vector<ShellMenuExclusionEntry>& entries,
    const QString& rawText,
    const QString& normalizedText,
    const QString& matchText,
    const QStringList& matchPath)
{
    const std::wstring raw = rawText.toStdWString();
    const std::wstring normalized = normalizedText.toStdWString();
    const std::wstring match = matchText.toStdWString();
    std::vector<std::wstring> path;
    path.reserve(static_cast<size_t>(matchPath.size()));
    for (const QString& segment : matchPath) {
        path.push_back(segment.toStdWString());
    }
    for (const ShellMenuExclusionEntry& entry : entries) {
        if (ShellContextMenu::MenuEntryMatches(entry, raw, normalized, match, path)) {
            return true;
        }
    }
    return false;
}

QString shellMenuDisplayText(QString text)
{
    text.remove(QLatin1Char('&'));
    const int tabIndex = text.indexOf(QLatin1Char('\t'));
    if (tabIndex >= 0) {
        text.truncate(tabIndex);
    }
    return text.trimmed();
}

struct ShellMenuRuleTypeOption
{
    QString title;
    QString key;
};

QString shellMenuTypeTitle(const QList<ShellMenuRuleTypeOption>& options, const QString& key)
{
    for (const ShellMenuRuleTypeOption& option : options) {
        if (option.key == key) {
            return option.title;
        }
    }
    return key;
}

int shellMenuDesktopPriority(const ShellMenuSnapshotItem& item)
{
    const QString normalized = QString::fromStdWString(item.normalizedText).trimmed().toLower();
    if (normalized == QStringLiteral("view") || normalized == QString::fromWCharArray(L"查看") || normalized == QString::fromWCharArray(L"视图")) {
        return 0;
    }
    if (normalized == QStringLiteral("sort by") || normalized == QString::fromWCharArray(L"排序方式") || normalized == QString::fromWCharArray(L"排序")) {
        return 1;
    }
    if (normalized == QStringLiteral("refresh") || normalized == QString::fromWCharArray(L"刷新")) {
        return 2;
    }
    if (normalized == QStringLiteral("paste") || normalized == QString::fromWCharArray(L"粘贴")) {
        return 3;
    }
    if (normalized == QStringLiteral("paste shortcut") || normalized == QString::fromWCharArray(L"粘贴快捷方式")) {
        return 4;
    }
    return 100;
}

void prioritizeDesktopMenuItems(std::vector<ShellMenuSnapshotItem>& items)
{
    std::stable_sort(items.begin(), items.end(), [](const ShellMenuSnapshotItem& left, const ShellMenuSnapshotItem& right) {
        return shellMenuDesktopPriority(left) < shellMenuDesktopPriority(right);
    });
}

QColor readDesktopSelectionColor()
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const QColor color(settings.value("Desktop/SelectionRectangleColor", QStringLiteral("#64A0F0")).toString());
    return color.isValid() ? color : QColor(100, 160, 240);
}

QColor desktopSelectionFillColor(const QColor& baseColor)
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const int opacity = qBound(20, settings.value("Desktop/SelectionRectangleOpacity", 75).toInt(), 100);
    QColor fill = baseColor;
    fill.setAlpha(qBound(35, opacity * 140 / 100, 180));
    return fill;
}

QColor desktopSelectionBorderColor(const QColor& baseColor)
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    const int contrast = qBound(0, settings.value("Desktop/SelectionRectangleContrast", 60).toInt(), 100);
    QColor border = baseColor.darker(110);
    border.setAlpha(qBound(95, contrast * 230 / 100, 230));
    return border;
}

void reloadSelectionOverlays()
{
    if (ListView* lv = ListView::instance()) {
        if (LinDesktop* desktop = qobject_cast<LinDesktop*>(lv->parentWidget())) {
            if (desktop->selectArea) {
                desktop->selectArea->reloadColorsFromSettings();
            }
        }

        const QVector<LinBox*> allBoxes = lv->boxes();
        for (LinBox* box : allBoxes) {
            if (box) {
                box->refreshSelectionOverlayColor();
            }
        }
    }
}

QString glyph(char16_t code)
{
    return QString(QChar(code));
}

QRect centeredPreviewImageRect(const QSize& size)
{
    const int imageWidth = qMax(1, size.width() * 90 / 100);
    return QRect((size.width() - imageWidth) / 2, 0, imageWidth, size.height());
}

QLabel* createPageTitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("pageTitle");
    return label;
}

QLabel* createPageSubtitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("pageSubtitle");
    label->setWordWrap(true);
    return label;
}

QLabel* createCardTitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("cardTitle");
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

QLabel* createCardSubtitle(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setObjectName("cardSubtitle");
    label->setWordWrap(true);
    return label;
}

QWidget* createPanel(QWidget* parent, const QString& objectName = QStringLiteral("settingsPanel"))
{
    QWidget* panel = new QWidget(parent);
    panel->setObjectName(objectName);
    panel->setMinimumWidth(0);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* shadow = new QGraphicsDropShadowEffect(panel);
    shadow->setBlurRadius(24);
    shadow->setXOffset(0);
    shadow->setYOffset(8);
    shadow->setColor(QColor(15, 23, 42, 18));
    panel->setGraphicsEffect(shadow);
    return panel;
}

QLabel* createGlyphLabel(const QString& glyphText, QWidget* parent, int pixelSize = 20, const QColor& color = QColor(0, 120, 212))
{
    QLabel* label = new QLabel(glyphText, parent);
    label->setObjectName("glyphLabel");
    label->setAlignment(Qt::AlignCenter);
    label->setFixedSize(pixelSize + 18, pixelSize + 18);
    label->setStyleSheet(QString(
        "QLabel#glyphLabel {"
        " background: transparent;"
        " color: %1;"
        " font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        " font-size: %2px;"
        "}").arg(color.name()).arg(pixelSize));
    return label;
}

QFrame* createDivider(QWidget* parent)
{
    QFrame* divider = new QFrame(parent);
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    divider->setObjectName("softDivider");
    return divider;
}

QPushButton* createSettingsActionButton(const QString& text, QWidget* parent, bool accent = false, bool destructive = false)
{
    QPushButton* button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(32);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 7px;"
        "  padding: 0 12px;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: %4; }"
        "QPushButton:disabled { background: #e5e7eb; border-color: #d1d5db; color: #9ca3af; }")
        .arg(accent ? QStringLiteral("#0078d4") : (destructive ? QStringLiteral("#fff1f2") : QStringLiteral("#ffffff")),
             accent ? QStringLiteral("#ffffff") : (destructive ? QStringLiteral("#b91c1c") : QStringLiteral("#111827")),
             accent ? QStringLiteral("#0078d4") : (destructive ? QStringLiteral("#fecdd3") : QStringLiteral("#cfd8e6")),
             accent ? QStringLiteral("#0b86e8") : (destructive ? QStringLiteral("#ffe4e6") : QStringLiteral("#f5f9ff"))));
    return button;
}

QString settingsWidgetAccentColor(const QString& id)
{
    const uint hash = qHash(id.toLower());
    const QStringList colors {
        QStringLiteral("#2878ff"),
        QStringLiteral("#2ecb86"),
        QStringLiteral("#8b5cf6"),
        QStringLiteral("#ff8a18"),
        QStringLiteral("#06b6d4"),
        QStringLiteral("#ef4444")
    };
    return colors.at(static_cast<int>(hash % colors.size()));
}

QString settingsWidgetIconGlyph(const QString& id)
{
    const QString lower = id.toLower();
    if (lower.contains(QStringLiteral("note"))) {
        return glyph(0xE70B);
    }
    if (lower.contains(QStringLiteral("monitor")) || lower.contains(QStringLiteral("system"))) {
        return glyph(0xE9D9);
    }
    if (lower.contains(QStringLiteral("timer")) || lower.contains(QStringLiteral("clock"))) {
        return glyph(0xE916);
    }
    if (lower.contains(QStringLiteral("weather"))) {
        return glyph(0xE706);
    }
    return glyph(0xE8A5);
}

QString settingsDefaultWidgetIconPath()
{
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QStringList candidates {
        QStringLiteral(":/defaultIcon.png"),
        QDir(appDirPath).filePath(QStringLiteral("resources/defaultIcon.png")),
        QDir(appDirPath).filePath(QStringLiteral("defaultIcon.png")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("src/resources/defaultIcon.png")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.last();
}

bool settingsIsSafeRelativeWidgetPath(const QString& path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))) {
        return false;
    }
    if (path.contains(QLatin1Char(':')) || path.contains(QLatin1Char('\0'))) {
        return false;
    }
    const QStringList parts = path.split(QRegularExpression(QStringLiteral("[/\\\\]")), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QStringLiteral(".") || part == QStringLiteral("..")) {
            return false;
        }
    }
    return true;
}

bool settingsCanLoadIconImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return false;
    }
    const QSize size = reader.size();
    return size.isValid() && !size.isEmpty();
}

QString settingsResolveWidgetIconPath(const QString& packagePath, const QString& iconPath)
{
    const QString trimmed = iconPath.trimmed();
    if (!trimmed.isEmpty() && settingsIsSafeRelativeWidgetPath(trimmed)) {
        const QFileInfo iconInfo(QDir(packagePath).filePath(trimmed));
        if (iconInfo.exists() && iconInfo.isFile() && settingsCanLoadIconImage(iconInfo.absoluteFilePath())) {
            return iconInfo.absoluteFilePath();
        }
    }
    return settingsDefaultWidgetIconPath();
}

QLabel* createWidgetIconLabel(const QString& id, const QString& iconPath, QWidget* parent, int size = 56)
{
    QLabel* icon = new QLabel(parent);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(size, size);
    const QString fallbackIconPath = settingsDefaultWidgetIconPath();
    QPixmap pixmap(iconPath);
    if (pixmap.isNull() && iconPath != fallbackIconPath) {
        pixmap.load(fallbackIconPath);
    }
    if (!pixmap.isNull()) {
        icon->setPixmap(pixmap.scaled(QSize(size, size), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        icon->setStyleSheet(QStringLiteral(
            "QLabel {"
            " background: #eef3f8;"
            " border-radius: 12px;"
            "}"));
        return icon;
    }
    icon->setText(settingsWidgetIconGlyph(id));
    icon->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: %1;"
        " color: white;"
        " border-radius: 12px;"
        " font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        " font-size: %2px;"
        " font-weight: 700;"
        "}").arg(settingsWidgetAccentColor(id)).arg(qMax(18, size / 2)));
    return icon;
}

QLabel* createWidgetMetaLabel(const QString& title, const QString& value, QWidget* parent)
{
    QLabel* label = new QLabel(QStringLiteral("<span style='color:#7a8498;font-weight:700;'>%1</span><br><span style='color:#1f2a44;font-weight:800;'>%2</span>")
                                   .arg(title.toHtmlEscaped(), value.toHtmlEscaped()),
                               parent);
    label->setTextFormat(Qt::RichText);
    label->setMinimumWidth(110);
    return label;
}

QWidget* createSettingsLinWidgetRow(QListWidget* list,
                                    QListWidgetItem* item,
                                    const QString& id,
                                    const QString& name,
                                    const QString& version,
                                    const QString& description,
                                    const QString& iconPath,
                                    const QString& badge,
                                    QWidget* parent,
                                    std::function<void(QListWidgetItem*)> openHandler = nullptr)
{
    QWidget* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("linWidgetStoreRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(20, 14, 20, 14);
    rowLayout->setSpacing(18);

    rowLayout->addWidget(createWidgetIconLabel(id, iconPath, row));

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);
    auto* nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(8);
    auto* nameLabel = new QLabel(name.isEmpty() ? id : name, row);
    nameLabel->setStyleSheet(QStringLiteral("color:#10182d;font-size:15px;font-weight:800;background:transparent;"));
    nameRow->addWidget(nameLabel);
    if (!badge.isEmpty()) {
        auto* badgeLabel = new QLabel(badge, row);
        badgeLabel->setStyleSheet(QStringLiteral("color:#1771e6;background:#eaf3ff;border-radius:5px;padding:2px 7px;font-size:11px;font-weight:800;"));
        nameRow->addWidget(badgeLabel);
    }
    nameRow->addStretch();
    auto* descriptionLabel = new QLabel(description.trimmed().isEmpty() ? QObject::tr("暂无描述。") : description.trimmed(), row);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet(QStringLiteral("color:#7a8498;font-size:12px;font-weight:600;background:transparent;"));
    titleLayout->addLayout(nameRow);
    titleLayout->addWidget(descriptionLabel);
    rowLayout->addLayout(titleLayout, 2);

    rowLayout->addWidget(createWidgetMetaLabel(QObject::tr("版本"), version.isEmpty() ? QStringLiteral("unknown") : version, row));

    row->installEventFilter(list);
    const QList<QWidget*> children = row->findChildren<QWidget*>();
    for (QWidget* child : children) {
        if (!qobject_cast<QAbstractButton*>(child)) {
            child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }
    }
    if (openHandler) {
        row->setCursor(Qt::PointingHandCursor);
        row->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        row->installEventFilter(new MenuListMousePressFilter(list, [item, openHandler](const QPoint&, Qt::KeyboardModifiers) {
            openHandler(item);
            return true;
        }));
    }

    return row;
}

QWidget* createSwitchRow(const QString& icon,
                         const QString& title,
                         const QString& subtitle,
                         FluentToggleSwitch* toggle,
                         QWidget* parent)
{
    QWidget* row = new QWidget(parent);
    row->setObjectName("settingsRow");
    row->setMinimumWidth(0);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(14);

    layout->addWidget(createGlyphLabel(icon, row, 20), 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QLabel* titleLabel = new QLabel(title, row);
    titleLabel->setObjectName("rowTitle");
    titleLabel->setWordWrap(true);
    titleLabel->setMinimumWidth(0);
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QLabel* subtitleLabel = new QLabel(subtitle, row);
    subtitleLabel->setObjectName("rowSubtitle");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setMinimumWidth(0);
    subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subtitleLabel);

    layout->addLayout(textLayout, 1);
    if (toggle) {
        layout->addWidget(toggle, 0, Qt::AlignVCenter);
    }

    return row;
}

QPushButton* createColorPreset(const QColor& color, QWidget* parent)
{
    QPushButton* button = new QPushButton(parent);
    button->setObjectName("colorPreset");
    button->setFixedSize(28, 28);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("presetColor", color.name());
    button->setStyleSheet(QString(
        "QPushButton#colorPreset {"
        " background: %1;"
        " border: 2px solid rgba(255,255,255,0.96);"
        " border-radius: 7px;"
        "}"
        "QPushButton#colorPreset:hover {"
        " border: 2px solid #0078d4;"
        "}").arg(color.name()));
    return button;
}

void updateColorPresetIndicators(QWidget* container, const QColor& selectedColor)
{
    if (!container) {
        return;
    }

    const QString selectedName = selectedColor.name().toLower();
    const QList<QLabel*> indicators = container->findChildren<QLabel*>(QStringLiteral("colorPresetIndicator"));
    bool matchedPreset = false;
    for (QLabel* indicator : indicators) {
        const QString presetName = indicator->property("presetColor").toString().toLower();
        const bool isPresetMatch = !presetName.isEmpty() && presetName == selectedName;
        if (isPresetMatch) {
            matchedPreset = true;
        }
        indicator->setVisible(isPresetMatch);
    }

    for (QLabel* indicator : indicators) {
        if (indicator->property("customColorIndicator").toBool()) {
            indicator->setVisible(!matchedPreset);
        }
    }
}

QWidget* createHomeTile(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent)
{
    QWidget* tile = new QWidget(parent);
    tile->setObjectName("homeTile");
    tile->setMinimumHeight(142);
    QVBoxLayout* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(16, 18, 16, 16);
    layout->setSpacing(8);
    layout->addWidget(createGlyphLabel(icon, tile, 34), 0, Qt::AlignHCenter);

    QLabel* titleLabel = new QLabel(title, tile);
    titleLabel->setObjectName("homeTileTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel(subtitle, tile);
    subtitleLabel->setObjectName("homeTileSubtitle");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitleLabel);
    layout->addStretch();
    return tile;
}

QWidget* createQuickAction(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent)
{
    QWidget* action = new QWidget(parent);
    action->setObjectName("quickAction");
    QHBoxLayout* layout = new QHBoxLayout(action);
    layout->setContentsMargins(14, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(createGlyphLabel(icon, action, 22), 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    QLabel* titleLabel = new QLabel(title, action);
    titleLabel->setObjectName("rowTitle");
    QLabel* subtitleLabel = new QLabel(subtitle, action);
    subtitleLabel->setObjectName("rowSubtitle");
    subtitleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subtitleLabel);
    layout->addLayout(textLayout, 1);
    layout->addWidget(createGlyphLabel(glyph(0xE974), action, 16, QColor("#27344c")), 0, Qt::AlignVCenter);
    return action;
}

QWidget* createPluginBadge(const QString& icon, const QString& text, QWidget* parent, const QColor& color)
{
    QWidget* badge = new QWidget(parent);
    badge->setObjectName("pluginBadge");
    badge->setStyleSheet(QString(
        "QWidget#pluginBadge { background: %1; border: 1px solid %2; border-radius: 7px; }")
        .arg(color.lighter(185).name())
        .arg(color.lighter(145).name()));
    QHBoxLayout* layout = new QHBoxLayout(badge);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(5);
    layout->addWidget(createGlyphLabel(icon, badge, 12, color), 0, Qt::AlignVCenter);
    QLabel* label = new QLabel(text, badge);
    label->setObjectName("badgeLabel");
    label->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 700;").arg(color.darker(130).name()));
    layout->addWidget(label);
    return badge;
}

QWidget* createPluginFeature(const QString& icon, const QString& title, const QString& subtitle, QWidget* parent)
{
    QWidget* feature = new QWidget(parent);
    feature->setObjectName("pluginFeature");
    QHBoxLayout* layout = new QHBoxLayout(feature);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(14);
    layout->addWidget(createGlyphLabel(icon, feature, 24), 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(3);
    QLabel* titleLabel = new QLabel(title, feature);
    titleLabel->setObjectName("rowTitle");
    QLabel* subtitleLabel = new QLabel(subtitle, feature);
    subtitleLabel->setObjectName("rowSubtitle");
    subtitleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subtitleLabel);
    layout->addLayout(textLayout, 1);
    return feature;
}
}

SettingUI::SettingUI(LinBox* targetBox, const QVector<LinBox*>& allBoxes, QWidget* parent)
    : FluentBaseWidget(parent), m_targetBox(targetBox)
{
    m_allBoxes.reserve(allBoxes.size());
    for (LinBox* box : allBoxes) {
        if (box) {
            m_allBoxes.append(QPointer<LinBox>(box));
        }
    }

    resize(1024, 720);
    setMinimumSize(880, 620);
    setWindowTitle(tr("LinDesk"));
    setResizable(true);
    setWindowBtnMode(None);

    m_selectionPreviewColor = readDesktopSelectionColor();

    initUI();
    setupStyles();

    // 1. Persistence - Restore Window Size only
    QString configPath = settingsConfigPath();
    QSettings settings(configPath, QSettings::IniFormat);
    if (settings.contains("SettingUI/Size")) {
        resize(settings.value("SettingUI/Size").toSize().expandedTo(minimumSize()));
    } else {
        resize(1024, 720);
    }

    // Always center on screen or near box (don't persist position as requested)
    if (m_targetBox) {
        LinBox* targetBoxPtr = m_targetBox.data();
        move(targetBoxPtr->mapToGlobal(QPoint(targetBoxPtr->width()/2 - width()/2, targetBoxPtr->height()/2 - height()/2)));
    }

    ensureTrayIcon();
}

void SettingUI::initUI() {
    ensureSegoeFluentIconsLoaded();

    QHBoxLayout* mainLayout = new QHBoxLayout();
    centralLayout()->addLayout(mainLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QWidget* sidebarContainer = new QWidget(container());
    sidebarContainer->setObjectName("sidebarContainer");
    sidebarContainer->setMinimumWidth(155);
    sidebarContainer->setMaximumWidth(195);
    sidebarContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(0, 18, 0, 28);
    sidebarLayout->setSpacing(0);
    sidebarLayout->setAlignment(Qt::AlignTop);

    QWidget* sidebarBrand = new QWidget(sidebarContainer);
    sidebarBrand->setObjectName("sidebarBrand");
    QHBoxLayout* sidebarBrandLayout = new QHBoxLayout(sidebarBrand);
    sidebarBrandLayout->setContentsMargins(22, 0, 16, 0);
    sidebarBrandLayout->setSpacing(12);
    sidebarBrandLayout->setAlignment(Qt::AlignVCenter);

    QLabel* sidebarBrandIcon = new QLabel(sidebarBrand);
    sidebarBrandIcon->setObjectName("sidebarBrandIcon");
    sidebarBrandIcon->setFixedSize(38, 38);
    sidebarBrandIcon->setPixmap(QIcon(":/Assets/icon.png").pixmap(38, 38));
    sidebarBrandLayout->addWidget(sidebarBrandIcon);

    m_titleBarTitle = new QLabel(tr("LinDesk"), sidebarBrand);
    m_titleBarTitle->setObjectName("sidebarBrandTitle");
    sidebarBrandLayout->addWidget(m_titleBarTitle);
    sidebarBrandLayout->addStretch(1);

    sidebarLayout->addWidget(sidebarBrand);
    sidebarLayout->addSpacing(28);

    m_sidebar = new QListWidget(sidebarContainer);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    struct TabInfo { char16_t icon; QString text; };
    QVector<TabInfo> tabs = {
        {0xE713, tr("General")},
        {0xE7F8, tr("Window")},
        {0xECAA, tr("Context Menu")},
        {0xF133, tr("Box")},
        {0xF4A5, tr("LinWidgets")},
        {0xF22C, tr("Plugins")},
        {0xE946, tr("About")}
    };

    for (const auto& tab : tabs) {
        QListWidgetItem* item = new QListWidgetItem(m_sidebar);
        item->setText(tab.text);
        item->setSizeHint(QSize(230, 50));
        item->setData(Qt::UserRole + 1, QString(QChar(tab.icon)));
        switch (m_sidebar->count() - 1) {
        case 0: m_generalTabItem = item; break;
        case 1: m_desktopTabItem = item; break;
        case 2: m_contextMenuTabItem = item; break;
        case 3: m_boxTabItem = item; break;
        case 4: m_linWidgetsTabItem = item; break;
        case 5: m_pluginsTabItem = item; break;
        case 6: m_aboutTabItem = item; break;
        default: break;
        }
    }

    m_sidebar->setItemDelegate(new SidebarDelegate(m_sidebar));
    m_sidebar->setSpacing(3);
    m_sidebar->setMinimumHeight(320);
    m_sidebar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sidebarLayout->addWidget(m_sidebar);

    QWidget* rightArea = new QWidget(container());
    rightArea->setObjectName("rightArea");
    rightArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightArea);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    QWidget* windowControls = new QWidget(rightArea);
    windowControls->setObjectName("settingsWindowControls");
    windowControls->setFixedHeight(32);
    QHBoxLayout* windowControlsLayout = new QHBoxLayout(windowControls);
    windowControlsLayout->setContentsMargins(0, 0, 0, 0);
    windowControlsLayout->setSpacing(0);
    windowControlsLayout->addStretch(1);

    auto createWindowControlButton = [windowControls](const QString& text, const QString& objectName) {
        QPushButton* button = new QPushButton(text, windowControls);
        button->setObjectName(objectName);
        button->setFixedSize(45, 32);
        button->setCursor(Qt::ArrowCursor);
        return button;
    };

    m_settingsMinBtn = createWindowControlButton(QString::fromUtf8(u8"\uE921"), QStringLiteral("settingsMinBtn"));
    m_settingsMaxBtn = createWindowControlButton(QString::fromUtf8(u8"\uE922"), QStringLiteral("settingsMaxBtn"));
    m_settingsCloseBtn = createWindowControlButton(QString::fromUtf8(u8"\uE8BB"), QStringLiteral("settingsCloseBtn"));
    windowControlsLayout->addWidget(m_settingsMinBtn);
    windowControlsLayout->addWidget(m_settingsMaxBtn);
    windowControlsLayout->addWidget(m_settingsCloseBtn);

    connect(m_settingsMinBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(m_settingsMaxBtn, &QPushButton::clicked, this, [this]() {
        if (windowState() & Qt::WindowMaximized) {
            setWindowState(Qt::WindowNoState);
        } else {
            setWindowState(Qt::WindowMaximized);
        }
        updateWindowControlButtons();
    });
    connect(m_settingsCloseBtn, &QPushButton::clicked, this, &QWidget::close);
    rightLayout->addWidget(windowControls);

    m_contentStack = new QStackedWidget(container());
    m_contentStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    for (int i = 0; i < 7; ++i) {
        QWidget* placeholder = new QWidget(container());
        m_contentStack->addWidget(placeholder);
    }

    rightLayout->addWidget(m_contentStack);

    mainLayout->addWidget(sidebarContainer, 15);
    mainLayout->addWidget(rightArea, 85);

    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        QWidget* page = getOrCreatePage(row);
        if (page) {
            m_contentStack->setCurrentIndex(row);
        }
        if (row == 2 && m_shellTypeList && !m_shellTypeList->currentItem() && m_shellTypeList->count() > 0) {
            m_shellTypeList->setCurrentRow(0);
        }
    });

    const int initialRow = (m_targetBox ? 3 : 0);
    getOrCreatePage(initialRow);
    m_sidebar->setCurrentRow(initialRow);
    m_contentStack->setCurrentIndex(initialRow);
    updateSidebarItemHeights();
}

QWidget* SettingUI::getOrCreatePage(int index) {
    if (index < 0 || index >= 7 || !m_contentStack) {
        return nullptr;
    }
    if (m_pageCache[index]) {
        return m_pageCache[index];
    }

    QWidget* page = nullptr;
    switch (index) {
    case 0:
        page = wrapScrollablePage(createGeneralPage());
        break;
    case 1:
        page = wrapScrollablePage(createDesktopPage());
        break;
    case 2:
        page = wrapScrollablePage(createContextMenuPage());
        break;
    case 3:
        page = wrapScrollablePage(createGridPage());
        break;
    case 4:
        page = wrapScrollablePage(createLinWidgetsPage());
        break;
    case 5:
        page = createPluginsPage();
        break;
    case 6:
        page = wrapScrollablePage(createAboutPage());
        break;
    default:
        break;
    }

    if (page) {
        m_pageCache[index] = page;
        if (index < m_contentStack->count()) {
            QWidget* placeholder = m_contentStack->widget(index);
            m_contentStack->removeWidget(placeholder);
            m_contentStack->insertWidget(index, page);
            if (placeholder) {
                placeholder->deleteLater();
            }
        } else {
            m_contentStack->addWidget(page);
        }

        if (index == 1) {
            updateDesktopSelectionPreview();
        } else if (index == 3) {
            updateControlsFromTarget();
            updatePreviewStyle();
        }
    }

    return m_pageCache[index];
}

QWidget* SettingUI::wrapScrollablePage(QWidget* content) {
    if (!content) {
        return new QWidget(container());
    }

    SmoothScrollArea* scroll = new SmoothScrollArea(container());
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(0);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    if (scroll->viewport()) {
        scroll->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
        scroll->viewport()->setAutoFillBackground(true);
        QPalette p = scroll->viewport()->palette();
        p.setColor(QPalette::Window, QColor(249, 251, 255));
        scroll->viewport()->setPalette(p);
    }

    content->setMinimumWidth(0);
    content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::MinimumExpanding);
    if (QLayout* contentLayout = content->layout()) {
        contentLayout->setSizeConstraint(QLayout::SetNoConstraint);
    }
    scroll->setWidget(content);
    return scroll;
}

QString SettingUI::startupShortcutPath() const
{
    wchar_t startupDir[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, startupDir))) {
        return QString();
    }

    return QDir::fromNativeSeparators(QString::fromWCharArray(startupDir)) + "/LinDesk.lnk";
}

bool SettingUI::isLaunchAtLoginEnabled() const
{
    const QString shortcutPath = startupShortcutPath();
    return !shortcutPath.isEmpty() && QFile::exists(shortcutPath);
}

bool SettingUI::setLaunchAtLoginEnabled(bool enabled)
{
    const QString shortcutPath = startupShortcutPath();
    if (shortcutPath.isEmpty()) {
        return false;
    }

    if (!enabled) {
        return !QFile::exists(shortcutPath) || QFile::remove(shortcutPath);
    }

    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString workingDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const std::wstring shortcutPathW = QDir::toNativeSeparators(shortcutPath).toStdWString();
    const std::wstring exePathW = exePath.toStdWString();
    const std::wstring workingDirW = workingDir.toStdWString();

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
    if (FAILED(hr) || !shellLink) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    shellLink->SetPath(exePathW.c_str());
    shellLink->SetWorkingDirectory(workingDirW.c_str());
    shellLink->SetIconLocation(exePathW.c_str(), 0);
    shellLink->SetDescription(L"Launch LinDesk at sign in");

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    bool ok = false;
    if (SUCCEEDED(hr) && persistFile) {
        ok = SUCCEEDED(persistFile->Save(shortcutPathW.c_str(), TRUE));
        persistFile->Release();
    }

    shellLink->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }

    return ok;
}

bool SettingUI::shouldMinimizeToTray() const
{
    const QString configPath = settingsConfigPath();
    QSettings settings(configPath, QSettings::IniFormat);
    return settings.value("General/MinimizeToTray", true).toBool();
}

void SettingUI::ensureTrayIcon()
{
    if (m_trayIcon || !QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    QIcon trayIcon = QApplication::windowIcon();
    if (trayIcon.isNull()) {
        trayIcon = QIcon(":/Assets/icon.png");
    }
    m_trayIcon->setIcon(trayIcon);
    m_trayIcon->setToolTip(tr("LinDesk Settings"));

    m_trayMenu = new QMenu(this);
    m_trayMenu->setStyleSheet(
        "QMenu {"
        " background: #ffffff;"
        " color: #1f2937;"
        " border: 1px solid rgba(0, 0, 0, 0.12);"
        " padding: 6px 0px;"
        " }"
        "QMenu::item {"
        " padding: 8px 18px;"
        " background: transparent;"
        " }"
        "QMenu::item:selected {"
        " background: #e8f3ff;"
        " color: #0f172a;"
        " }"
        "QMenu::separator {"
        " height: 1px;"
        " margin: 4px 10px;"
        " background: rgba(0, 0, 0, 0.08);"
        " }"
    );
    m_showTrayAction = m_trayMenu->addAction(tr("Show Settings"));
    m_exitTrayAction = m_trayMenu->addAction(tr("Exit"));
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_showTrayAction, &QAction::triggered, this, &SettingUI::restoreFromTray);
    connect(m_exitTrayAction, &QAction::triggered, this, &SettingUI::exitFromTray);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            restoreFromTray();
        }
    });
}

void SettingUI::restoreFromTray()
{
    restoreAndActivate();
}

void SettingUI::exitFromTray()
{
    m_isQuittingFromTray = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    QCoreApplication::quit();
}

void SettingUI::restoreAndActivate()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    setWindowState(windowState() & ~Qt::WindowMinimized);
    if (!isVisible()) {
        show();
    }
    showNormal();
    raise();
    activateWindow();
#ifdef Q_OS_WIN
    if (const HWND hwnd = reinterpret_cast<HWND>(winId())) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        SetActiveWindow(hwnd);
    }
#endif
}

QWidget* SettingUI::createGeneralPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    QString configPath = settingsConfigPath();
    QSettings settings(configPath, QSettings::IniFormat);

    m_generalPageTitle = createPageTitle(tr("Settings Home"), page);
    m_generalPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_generalPageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(8);
    layout->addWidget(createPageSubtitle(tr("Manage LinDesk preferences, desktop behavior, and plugins from one place."), page));
    layout->addSpacing(8);

    QWidget* tileGrid = new QWidget(page);
    QGridLayout* tileLayout = new QGridLayout(tileGrid);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setHorizontalSpacing(16);
    tileLayout->setVerticalSpacing(16);
    tileLayout->addWidget(createHomeTile(glyph(0xE80F), tr("General"), tr("Configure core application behavior and preferences."), tileGrid), 0, 0);
    tileLayout->addWidget(createHomeTile(glyph(0xE7F4), tr("Desktop"), tr("Customize desktop experience and settings."), tileGrid), 0, 1);
    tileLayout->addWidget(createHomeTile(glyph(0xEA37), tr("Box"), tr("Adjust Box interface and interaction."), tileGrid), 0, 2);
    tileLayout->addWidget(createHomeTile(glyph(0xE74C), tr("Plugins"), tr("Browse, install, and manage plugins."), tileGrid), 0, 3);
    for (int column = 0; column < 4; ++column) {
        tileLayout->setColumnStretch(column, 1);
    }
    layout->addWidget(tileGrid);

    QLabel* statusTitle = createCardTitle(tr("System Status"), page);
    layout->addSpacing(16);
    layout->addWidget(statusTitle);
    layout->addSpacing(8);

    QWidget* statusPanel = createPanel(page);
    QVBoxLayout* statusLayout = new QVBoxLayout(statusPanel);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);

    m_autoStartToggle = new FluentToggleSwitch(page);
    const bool autoStartEnabled = isLaunchAtLoginEnabled();
    m_autoStartToggle->setChecked(autoStartEnabled);
    settings.setValue("General/AutoStart", autoStartEnabled);
    connect(m_autoStartToggle, &FluentToggleSwitch::toggled, this, [this, configPath](bool checked){
        if (!setLaunchAtLoginEnabled(checked)) {
            if (m_autoStartToggle) {
                const QSignalBlocker blocker(m_autoStartToggle);
                m_autoStartToggle->setChecked(!checked);
            }
            FluentDialog::showInfo(this, tr("Launch at Login"), tr("LinDesk could not update the startup shortcut."));
            return;
        }

        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/AutoStart", checked);
    });
    m_autoStartCard = nullptr;
    statusLayout->addWidget(createSwitchRow(glyph(0xE7E6), tr("Launch at Login"), tr("Automatically start LinDesk when you sign in to Windows."), m_autoStartToggle, statusPanel));
    statusLayout->addWidget(createDivider(statusPanel));

    FluentToggleSwitch* trayOnly = new FluentToggleSwitch(page);
    trayOnly->setChecked(settings.value("General/MinimizeToTray", true).toBool());
    connect(trayOnly, &FluentToggleSwitch::toggled, this, [configPath](bool checked){
        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/MinimizeToTray", checked);
    });
    m_trayOnlyCard = nullptr;
    statusLayout->addWidget(createSwitchRow(glyph(0xE921), tr("Minimize to Tray"), tr("Keep LinDesk running in the system tray when the window closes."), trayOnly, statusPanel));
    statusLayout->addWidget(createDivider(statusPanel));

    m_openGLToggle = new FluentToggleSwitch(page);
    m_openGLToggle->setChecked(settings.value("General/UseOpenGL", false).toBool());
    connect(m_openGLToggle, &FluentToggleSwitch::toggled, this, [this, configPath](bool checked) {
        QSettings s(configPath, QSettings::IniFormat);
        const bool previousValue = s.value("General/UseOpenGL", false).toBool();
        if (previousValue == checked) {
            return;
        }

        s.setValue("General/UseOpenGL", checked);
        s.sync();
        FluentDialog::showInfo(
            this,
            tr("Restart Required"),
            tr("Restart LinDesk to apply the OpenGL rendering change."));
    });
    m_openGLCard = nullptr;
    statusLayout->addWidget(createSwitchRow(glyph(0xE7F8), tr("OpenGL Rendering"), tr("Use OpenGL to accelerate rendering."), m_openGLToggle, statusPanel));
    statusLayout->addWidget(createDivider(statusPanel));

    QWidget* languageRow = new QWidget(statusPanel);
    languageRow->setObjectName("settingsRow");
    QHBoxLayout* languageRowLayout = new QHBoxLayout(languageRow);
    languageRowLayout->setContentsMargins(14, 10, 14, 10);
    languageRowLayout->setSpacing(14);
    languageRowLayout->addWidget(createGlyphLabel(glyph(0xE774), languageRow, 20), 0, Qt::AlignVCenter);
    QVBoxLayout* languageText = new QVBoxLayout();
    languageText->setContentsMargins(0, 0, 0, 0);
    languageText->setSpacing(2);
    QLabel* languageTitle = new QLabel(tr("Display Language"), languageRow);
    languageTitle->setObjectName("rowTitle");
    QLabel* languageSub = new QLabel(tr("Choose which language is used in the interface. Restart LinDesk to apply changes."), languageRow);
    languageSub->setObjectName("rowSubtitle");
    languageText->addWidget(languageTitle);
    languageText->addWidget(languageSub);
    languageRowLayout->addLayout(languageText, 1);

    m_languageCombo = new FluentComboBox(page);
    m_languageCombo->addItem(tr("English (United States)"), QStringLiteral("en_US"));
    m_languageCombo->addItem(tr("Chinese (Simplified)"), QStringLiteral("zh_CN"));
    const QString configuredLanguage = LanguageManager::configuredLanguageCode();
    int languageIndex = m_languageCombo->findData(configuredLanguage);
    if (languageIndex < 0) {
        languageIndex = 0;
    }
    m_languageCombo->setCurrentIndex(languageIndex);
    m_languageCombo->setMinimumWidth(180);
    connect(m_languageCombo, QOverload<int>::of(&FluentComboBox::currentIndexChanged), this, [this](int index){
        if (index < 0) {
            return;
        }

        const QString languageCode = LanguageManager::normalizeLanguageCode(m_languageCombo->itemData(index));
        if (languageCode == LanguageManager::configuredLanguageCode()) {
            return;
        }

        if (!LanguageManager::instance().setConfiguredLanguageCode(languageCode)) {
            FluentDialog::showInfo(this, tr("Display Language"), tr("LinDesk could not save the language setting."));
            return;
        }

        if (languageCode == LanguageManager::instance().currentLanguageCode()) {
            return;
        }

        FluentDialog dlg(this);
        dlg.setDialogTitle(tr("Restart Required"));
        dlg.setMessage(tr("Restart LinDesk to apply the display language change."));
        dlg.addActionButton(tr("Restart Now"), FluentDialog::ActionType::Primary, [this]() {
            const bool started = QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                QCoreApplication::arguments(),
                QCoreApplication::applicationDirPath());
            if (started) {
                QCoreApplication::quit();
            } else {
                FluentDialog::showInfo(this, tr("Restart Required"), tr("LinDesk could not restart automatically. Please restart it manually."));
            }
        });
        dlg.addActionButton(tr("Later"), FluentDialog::ActionType::Close);
        dlg.exec();
    });
    languageRowLayout->addWidget(m_languageCombo);
    statusLayout->addWidget(languageRow);
    m_languageCard = nullptr;
    layout->addWidget(statusPanel);
    layout->addSpacing(16);

    QWidget* tipPanel = createPanel(page);
    QHBoxLayout* tipLayout = new QHBoxLayout(tipPanel);
    tipLayout->setContentsMargins(16, 12, 16, 12);
    tipLayout->setSpacing(12);
    tipLayout->addWidget(createGlyphLabel(glyph(0xE82F), tipPanel, 22), 0, Qt::AlignVCenter);
    QLabel* tipText = new QLabel(tr("Tip: You can find more settings in each section. Explore the categories above to personalize your LinDesk experience."), tipPanel);
    tipText->setObjectName("rowSubtitle");
    tipText->setWordWrap(true);
    tipLayout->addWidget(tipText, 1);
    layout->addWidget(tipPanel);

    layout->addStretch();
    return page;
}

QWidget* SettingUI::createDesktopPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    m_desktopPageTitle = createPageTitle(tr("Desktop Customization"), page);
    m_desktopPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_desktopPageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    QWidget* previewContainer = createPanel(page);
    m_desktopPreviewContainer = previewContainer;
    previewContainer->setFixedHeight(livePreviewHeightForWindow());
    previewContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(22, 10, 22, 20);
    previewLayout->setSpacing(6);

    QLabel* previewTitle = createCardTitle(tr("Live Preview"), previewContainer);
    previewLayout->addWidget(previewTitle);
    previewLayout->addWidget(createCardSubtitle(tr("Preview the desktop selection rectangle color, contrast, and opacity."), previewContainer));

    m_desktopSelectionPreview = new QLabel(previewContainer);
    m_desktopSelectionPreview->setMinimumHeight(124);
    m_desktopSelectionPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_desktopSelectionPreview->setAlignment(Qt::AlignCenter | Qt::AlignHCenter);
    m_desktopSelectionPreview->setObjectName("previewSurface");
    previewLayout->addWidget(m_desktopSelectionPreview);

    layout->addWidget(previewContainer, 0);

    QWidget* selectionPanel = createPanel(page);
    m_desktopSelectionPanel = selectionPanel;
    selectionPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* selectionLayout = new QVBoxLayout(selectionPanel);
    selectionLayout->setContentsMargins(22, 18, 22, 18);
    selectionLayout->setSpacing(0);

    m_selectionColorSectionTitle = createCardTitle(tr("Selection Rectangle Color"), selectionPanel);
    selectionLayout->addWidget(m_selectionColorSectionTitle);
    selectionLayout->addSpacing(12);

    QWidget* presetsRow = new QWidget(selectionPanel);
    presetsRow->setObjectName("settingsInnerRow");
    presetsRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* presetsLayout = new QHBoxLayout(presetsRow);
    presetsLayout->setContentsMargins(12, 8, 12, 8);
    presetsLayout->setSpacing(8);
    QLabel* presetsLabel = new QLabel(tr("Presets"), presetsRow);
    presetsLabel->setObjectName("rowTitle");
    presetsLayout->addWidget(presetsLabel);
    presetsLayout->addStretch();

    m_selectionColorValueEdit = new QLineEdit(presetsRow);
    m_selectionColorValueEdit->setReadOnly(true);
    m_selectionColorValueEdit->setAlignment(Qt::AlignCenter);
    m_selectionColorValueEdit->setMinimumWidth(72);
    m_selectionColorValueEdit->setMaximumWidth(84);
    m_selectionColorValueEdit->setFixedHeight(30);
    m_selectionColorValueEdit->setText(m_selectionPreviewColor.name().toUpper());
    m_selectionColorValueEdit->setObjectName("valueEdit");
    presetsLayout->addWidget(m_selectionColorValueEdit);

    const QVector<QColor> presets = {
        QColor("#F0CF24"),
        QColor("#63A8F5"),
        QColor("#00C853"),
        QColor("#8B45E6"),
        QColor("#E94B98"),
        QColor("#FF8A24"),
        QColor("#AEB4BE")
    };
    for (const QColor& preset : presets) {
        QWidget* presetContainer = new QWidget(presetsRow);
        presetContainer->setObjectName("colorPresetContainer");
        QVBoxLayout* presetContainerLayout = new QVBoxLayout(presetContainer);
        presetContainerLayout->setContentsMargins(0, 0, 0, 0);
        presetContainerLayout->setSpacing(3);

        QPushButton* button = createColorPreset(preset, presetContainer);
        button->setFixedSize(24, 24);
        QLabel* indicator = new QLabel(presetContainer);
        indicator->setObjectName("colorPresetIndicator");
        indicator->setProperty("presetColor", preset.name());
        indicator->setFixedSize(6, 6);
        indicator->setStyleSheet(QStringLiteral(
            "QLabel#colorPresetIndicator {"
            " background: #1870ff;"
            " border-radius: 3px;"
            "}"));
        indicator->setVisible(false);

        presetContainerLayout->addWidget(button, 0, Qt::AlignHCenter);
        presetContainerLayout->addWidget(indicator, 0, Qt::AlignHCenter);
        connect(button, &QPushButton::clicked, this, [this, presetsRow, preset]() {
            m_selectionPreviewColor = preset;
            if (m_selectionColorValueEdit) {
                m_selectionColorValueEdit->setText(preset.name().toUpper());
            }
            QSettings settings(settingsConfigPath(), QSettings::IniFormat);
            settings.setValue("Desktop/SelectionRectangleColor", preset.name());
            settings.sync();
            reloadSelectionOverlays();
            updateColorPresetIndicators(presetsRow, preset);
            updateDesktopSelectionPreview();
        });
        presetsLayout->addWidget(presetContainer);
    }

    QWidget* pickerContainer = new QWidget(presetsRow);
    pickerContainer->setObjectName("colorPresetContainer");
    QVBoxLayout* pickerContainerLayout = new QVBoxLayout(pickerContainer);
    pickerContainerLayout->setContentsMargins(0, 0, 0, 0);
    pickerContainerLayout->setSpacing(3);

    QPushButton* pickerButton = new QPushButton(glyph(0xE790), pickerContainer);
    pickerButton->setObjectName("colorPickerButton");
    pickerButton->setFixedSize(24, 24);
    pickerButton->setCursor(Qt::PointingHandCursor);
    QLabel* pickerIndicator = new QLabel(pickerContainer);
    pickerIndicator->setObjectName("colorPresetIndicator");
    pickerIndicator->setProperty("customColorIndicator", true);
    pickerIndicator->setFixedSize(6, 6);
    pickerIndicator->setStyleSheet(QStringLiteral(
        "QLabel#colorPresetIndicator {"
        " background: #1870ff;"
        " border-radius: 3px;"
        "}"));
    pickerIndicator->setVisible(false);
    pickerContainerLayout->addWidget(pickerButton, 0, Qt::AlignHCenter);
    pickerContainerLayout->addWidget(pickerIndicator, 0, Qt::AlignHCenter);
    presetsLayout->addWidget(pickerContainer);
    updateColorPresetIndicators(presetsRow, m_selectionPreviewColor);
    selectionLayout->addWidget(presetsRow);
    selectionLayout->addWidget(createDivider(selectionPanel));

    auto applySelectionColor = [this, presetsRow](const QColor& color) {
        if (!color.isValid()) {
            return;
        }
        m_selectionPreviewColor = color;
        if (m_selectionColorValueEdit) {
            m_selectionColorValueEdit->setText(color.name().toUpper());
        }
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionRectangleColor", color.name());
        settings.sync();
        reloadSelectionOverlays();
        updateColorPresetIndicators(presetsRow, color);
        updateDesktopSelectionPreview();
    };

    connect(pickerButton, &QPushButton::clicked, this, [this, applySelectionColor]() {
        QColorDialog* dialog = new QColorDialog(m_selectionPreviewColor, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Selection Rectangle Color"));
        dialog->setOption(QColorDialog::NoButtons, true);
        connect(dialog, &QColorDialog::currentColorChanged, this, [applySelectionColor](const QColor& color) {
            applySelectionColor(color);
        });
        connect(dialog, &QColorDialog::colorSelected, this, [applySelectionColor](const QColor& color) {
            applySelectionColor(color);
        });
        dialog->open();
    });

    QWidget* contrastRow = new QWidget(selectionPanel);
    contrastRow->setObjectName("settingsInnerRow");
    contrastRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* contrastLayout = new QHBoxLayout(contrastRow);
    contrastLayout->setContentsMargins(14, 8, 14, 8);
    contrastLayout->setSpacing(12);
    QVBoxLayout* contrastText = new QVBoxLayout();
    contrastText->setContentsMargins(0, 0, 0, 0);
    contrastText->setSpacing(2);
    QLabel* contrastTitle = new QLabel(tr("Contrast"), contrastRow);
    contrastTitle->setObjectName("rowTitle");
    contrastTitle->setWordWrap(true);
    contrastTitle->setMinimumWidth(0);
    contrastTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contrastText->addWidget(contrastTitle);
    contrastLayout->addLayout(contrastText, 1);
    contrastLayout->addWidget(createGlyphLabel(glyph(0xE706), contrastRow, 18, QColor("#5b6b86")));
    QSlider* contrastSlider = new QSlider(Qt::Horizontal, contrastRow);
    contrastSlider->setRange(0, 100);
    QSettings desktopSettings(settingsConfigPath(), QSettings::IniFormat);
    contrastSlider->setValue(qBound(0, desktopSettings.value("Desktop/SelectionRectangleContrast", 60).toInt(), 100));
    contrastSlider->setMinimumWidth(120);
    contrastSlider->setMaximumWidth(160);
    contrastSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    contrastLayout->addWidget(contrastSlider, 2);
    QLabel* contrastValue = new QLabel(QStringLiteral("%1%").arg(contrastSlider->value()), contrastRow);
    contrastValue->setObjectName("valueLabel");
    contrastValue->setFixedWidth(44);
    contrastValue->setAlignment(Qt::AlignCenter);
    contrastLayout->addWidget(contrastValue);
    connect(contrastSlider, &QSlider::valueChanged, this, [this, contrastValue](int value) {
        contrastValue->setText(QStringLiteral("%1%").arg(value));
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionRectangleContrast", value);
        settings.sync();
        reloadSelectionOverlays();
        updateDesktopSelectionPreview();
    });
    selectionLayout->addWidget(contrastRow);
    selectionLayout->addWidget(createDivider(selectionPanel));

    QWidget* strengthRow = new QWidget(selectionPanel);
    strengthRow->setObjectName("settingsInnerRow");
    strengthRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* strengthLayout = new QHBoxLayout(strengthRow);
    strengthLayout->setContentsMargins(14, 8, 14, 8);
    strengthLayout->setSpacing(12);
    QVBoxLayout* strengthText = new QVBoxLayout();
    strengthText->setContentsMargins(0, 0, 0, 0);
    strengthText->setSpacing(2);
    QLabel* strengthTitle = new QLabel(tr("Opacity / Strength"), strengthRow);
    strengthTitle->setObjectName("rowTitle");
    strengthTitle->setWordWrap(true);
    strengthTitle->setMinimumWidth(0);
    strengthTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    strengthText->addWidget(strengthTitle);
    strengthLayout->addLayout(strengthText, 1);
    strengthLayout->addWidget(createGlyphLabel(glyph(0xEB48), strengthRow, 18, QColor("#5b6b86")));
    QSlider* strengthSlider = new QSlider(Qt::Horizontal, strengthRow);
    strengthSlider->setRange(20, 100);
    strengthSlider->setValue(qBound(20, desktopSettings.value("Desktop/SelectionRectangleOpacity", 75).toInt(), 100));
    strengthSlider->setMinimumWidth(120);
    strengthSlider->setMaximumWidth(160);
    strengthSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    strengthLayout->addWidget(strengthSlider, 2);
    QLabel* strengthValue = new QLabel(QStringLiteral("%1%").arg(strengthSlider->value()), strengthRow);
    strengthValue->setObjectName("valueLabel");
    strengthValue->setFixedWidth(44);
    strengthValue->setAlignment(Qt::AlignCenter);
    strengthLayout->addWidget(strengthValue);
    connect(strengthSlider, &QSlider::valueChanged, this, [this, strengthValue](int value) {
        strengthValue->setText(QStringLiteral("%1%").arg(value));
        QSettings settings(settingsConfigPath(), QSettings::IniFormat);
        settings.setValue("Desktop/SelectionRectangleOpacity", value);
        settings.sync();
        reloadSelectionOverlays();
        updateDesktopSelectionPreview();
    });
    selectionLayout->addWidget(strengthRow);

    layout->addWidget(selectionPanel, 0);
    updateDesktopSelectionPreview();

    ListView* lv = ListView::instance();
    FluentToggleSwitch* arrowToggle = new FluentToggleSwitch(page);

    bool showArrow = lv ? lv->shortcutArrowVisible() : true;

    arrowToggle->setChecked(showArrow);

    connect(arrowToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setShortcutArrowVisible(checked);
    });

    QWidget* behaviorPanel = createPanel(page);
    behaviorPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* behaviorLayout = new QVBoxLayout(behaviorPanel);
    behaviorLayout->setContentsMargins(22, 18, 22, 18);
    behaviorLayout->setSpacing(0);
    behaviorLayout->addWidget(createCardTitle(tr("Desktop Behavior"), behaviorPanel));
    behaviorLayout->addSpacing(12);

    m_shortcutArrowCard = nullptr;
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE724), tr("Shortcut Arrows"), tr("Toggle visibility of the small arrow on desktop shortcut icons."), arrowToggle, behaviorPanel));
    behaviorLayout->addWidget(createDivider(behaviorPanel));

    FluentToggleSwitch* hideToggle = new FluentToggleSwitch(page);
    bool doubleClickHide = lv ? lv->isDoubleClickHideIconsEnabled() : true;
    hideToggle->setChecked(doubleClickHide);
    connect(hideToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setDoubleClickHideIconsEnabled(checked);
    });
    m_hideIconsCard = nullptr;
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE949), tr("Double Click to Hide"), tr("Quickly show or hide all desktop icons by double-clicking on empty spaces."), hideToggle, behaviorPanel));
    behaviorLayout->addWidget(createDivider(behaviorPanel));

    FluentToggleSwitch* fadeToggle = new FluentToggleSwitch(page);
    bool fadeTransition = lv ? lv->isFadeTransitionOnHideEnabled() : true;
    fadeToggle->setChecked(fadeTransition);
    connect(fadeToggle, &FluentToggleSwitch::toggled, this, [](bool checked){
        ListView* lv = ListView::instance();
        if (lv) lv->setFadeTransitionOnHideEnabled(checked);
    });
    behaviorLayout->addWidget(createSwitchRow(glyph(0xE790), tr("Fade Transition on Hide"), tr("Play a smooth opacity fade-in/out transition when hiding or showing desktop elements."), fadeToggle, behaviorPanel));

    layout->addWidget(behaviorPanel, 0);
    layout->addWidget(createWindows11MenuPanel(page), 0);

    return page;
}

QWidget* SettingUI::createWindows11MenuPanel(QWidget* page)
{
    QWidget* win11MenuPanel = createPanel(page);
    win11MenuPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* win11MenuLayout = new QVBoxLayout(win11MenuPanel);
    win11MenuLayout->setContentsMargins(22, 18, 22, 18);
    win11MenuLayout->setSpacing(0);
    win11MenuLayout->addWidget(createCardTitle(tr("Windows11 Menu"), win11MenuPanel));
    win11MenuLayout->addSpacing(12);

    const bool windows11ContextMenuSupported = RightMenu::isWindows11SystemContextMenuSupported();
    const QString windows11UnsupportedTip = tr("This option is available only on Windows 11 in Release builds.");
    auto applyWindows11MenuAvailability = [windows11ContextMenuSupported, &windows11UnsupportedTip](FluentToggleSwitch* toggle) {
        if (!toggle) {
            return;
        }
        toggle->setEnabled(windows11ContextMenuSupported);
        if (!windows11ContextMenuSupported) {
            toggle->setToolTip(windows11UnsupportedTip);
        }
    };

    m_windows11ContextMenuToggle = new FluentToggleSwitch(page);
    m_windows11ContextMenuToggle->setChecked(RightMenu::isWindows11SystemContextMenuEnabled());
    applyWindows11MenuAvailability(m_windows11ContextMenuToggle);
    connect(m_windows11ContextMenuToggle, &FluentToggleSwitch::toggled, this, [this, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (m_windows11ContextMenuToggle) {
                const QSignalBlocker blocker(m_windows11ContextMenuToggle);
                m_windows11ContextMenuToggle->setChecked(false);
            }
            return;
        }

        RightMenu::setWindows11SystemContextMenuEnabled(checked);
    });
    m_windows11ContextMenuCard = nullptr;
    win11MenuLayout->addWidget(createSwitchRow(
        glyph(0xE8B8),
        tr("Windows 11 Style Context Menu"),
        windows11ContextMenuSupported
            ? tr("Use the LinDesk Windows 11 style desktop context menu when right-clicking empty space in ListView.")
            : windows11UnsupportedTip,
        m_windows11ContextMenuToggle,
        win11MenuPanel));
    win11MenuLayout->addWidget(createDivider(win11MenuPanel));

    FluentToggleSwitch* showNewBoxMenuToggle = new FluentToggleSwitch(page);
    showNewBoxMenuToggle->setChecked(RightMenu::isNewBoxMenuItemEnabled());
    applyWindows11MenuAvailability(showNewBoxMenuToggle);
    connect(showNewBoxMenuToggle, &FluentToggleSwitch::toggled, this, [showNewBoxMenuToggle, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (showNewBoxMenuToggle) {
                const QSignalBlocker blocker(showNewBoxMenuToggle);
                showNewBoxMenuToggle->setChecked(RightMenu::isNewBoxMenuItemEnabled());
            }
            return;
        }
        RightMenu::setNewBoxMenuItemEnabled(checked);
    });
    win11MenuLayout->addWidget(createSwitchRow(
        glyph(0xE8B7),
        tr("Show New Box Menu"),
        tr("Show the New Box entry in the desktop context menu."),
        showNewBoxMenuToggle,
        win11MenuPanel));
    win11MenuLayout->addWidget(createDivider(win11MenuPanel));

    FluentToggleSwitch* showLinDeskMenuToggle = new FluentToggleSwitch(page);
    showLinDeskMenuToggle->setChecked(RightMenu::isLinDeskMenuEnabled());
    applyWindows11MenuAvailability(showLinDeskMenuToggle);
    connect(showLinDeskMenuToggle, &FluentToggleSwitch::toggled, this, [showLinDeskMenuToggle, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (showLinDeskMenuToggle) {
                const QSignalBlocker blocker(showLinDeskMenuToggle);
                showLinDeskMenuToggle->setChecked(RightMenu::isLinDeskMenuEnabled());
            }
            return;
        }
        RightMenu::setLinDeskMenuEnabled(checked);
    });
    win11MenuLayout->addWidget(createSwitchRow(
        glyph(0xE713),
        tr("Show LinDesk Menu"),
        tr("Show the LinDesk submenu and its actions in the desktop context menu."),
        showLinDeskMenuToggle,
        win11MenuPanel));
    win11MenuLayout->addWidget(createDivider(win11MenuPanel));

    FluentToggleSwitch* showThirdPartyShellMenuToggle = new FluentToggleSwitch(page);
    showThirdPartyShellMenuToggle->setChecked(RightMenu::isThirdPartyShellMenuEnabled());
    applyWindows11MenuAvailability(showThirdPartyShellMenuToggle);
    connect(showThirdPartyShellMenuToggle, &FluentToggleSwitch::toggled, this, [showThirdPartyShellMenuToggle, windows11ContextMenuSupported](bool checked) {
        if (!windows11ContextMenuSupported) {
            if (showThirdPartyShellMenuToggle) {
                const QSignalBlocker blocker(showThirdPartyShellMenuToggle);
                showThirdPartyShellMenuToggle->setChecked(RightMenu::isThirdPartyShellMenuEnabled());
            }
            return;
        }
        RightMenu::setThirdPartyShellMenuEnabled(checked);
    });
    win11MenuLayout->addWidget(createSwitchRow(
        glyph(0xECAA),
        tr("Show Third Party Shell Menus"),
        tr("Show registered third party Shell menu items in the Windows 11 style context menu."),
        showThirdPartyShellMenuToggle,
        win11MenuPanel));

    return win11MenuPanel;
}

QWidget* SettingUI::createContextMenuPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    m_contextMenuPageTitle = createPageTitle(tr("Context Menu"), page);
    m_contextMenuPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_contextMenuPageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    QWidget* filterTogglePanel = createPanel(page);
    filterTogglePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* filterToggleLayout = new QVBoxLayout(filterTogglePanel);
    filterToggleLayout->setContentsMargins(22, 10, 22, 10);
    filterToggleLayout->setSpacing(0);
    FluentToggleSwitch* enableFilterToggle = new FluentToggleSwitch(filterTogglePanel);
    enableFilterToggle->setChecked(RightMenu::isShellMenuExclusionEnabled());
    filterToggleLayout->addWidget(createSwitchRow(
        glyph(0xE71C),
        tr("Enable Context Menu Filtering"),
        tr("Filter context menu items using the exclusion rules below. Turn off to bypass menu filtering."),
        enableFilterToggle,
        filterTogglePanel));
    layout->addWidget(filterTogglePanel, 0);
    layout->addSpacing(12);

    QWidget* shellExcludePanel = createPanel(page);
    shellExcludePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    shellExcludePanel->setMinimumHeight(620);
    QVBoxLayout* shellExcludeLayout = new QVBoxLayout(shellExcludePanel);
    shellExcludeLayout->setContentsMargins(22, 18, 22, 20);
    shellExcludeLayout->setSpacing(12);

    QHBoxLayout* shellHeaderLayout = new QHBoxLayout();
    QVBoxLayout* shellTitleLayout = new QVBoxLayout();
    shellTitleLayout->setSpacing(4);
    shellTitleLayout->addWidget(createCardTitle(tr("Shell Menu Exclusions"), shellExcludePanel));
    shellTitleLayout->addWidget(createCardSubtitle(tr("Choose a menu type. LinDesk scans the real menu immediately, then you can hide items from future context menus."), shellExcludePanel));
    shellHeaderLayout->addLayout(shellTitleLayout, 1);
    const auto applyOutlineButtonStyle = [](FluentButton* button) {
        button->setColors(QColor("#ffffff"), QColor("#eef6ff"), QColor("#005a9e"), QColor("#0078d4"));
    };
    FluentButton* addExtensionButton = new FluentButton(tr("Add"), shellExcludePanel);
    applyOutlineButtonStyle(addExtensionButton);
    addExtensionButton->setFixedWidth(86);
    shellHeaderLayout->addWidget(addExtensionButton, 0, Qt::AlignTop);
    FluentButton* removeExtensionButton = new FluentButton(tr("Remove"), shellExcludePanel);
    removeExtensionButton->setColors(QColor("#ffffff"), QColor("#fff1f0"), QColor("#a4262c"), QColor("#d13438"));
    removeExtensionButton->setFixedWidth(96);
    removeExtensionButton->setEnabled(false);
    removeExtensionButton->setVisible(false);
    shellHeaderLayout->addWidget(removeExtensionButton, 0, Qt::AlignTop);
    FluentButton* copyRulesButton = new FluentButton(tr("Copy From"), shellExcludePanel);
    applyOutlineButtonStyle(copyRulesButton);
    copyRulesButton->setFixedWidth(112);
    shellHeaderLayout->addWidget(copyRulesButton, 0, Qt::AlignTop);
    FluentButton* applyRulesButton = new FluentButton(tr("Apply To"), shellExcludePanel);
    applyOutlineButtonStyle(applyRulesButton);
    applyRulesButton->setFixedWidth(100);
    shellHeaderLayout->addWidget(applyRulesButton, 0, Qt::AlignTop);
    shellExcludeLayout->addLayout(shellHeaderLayout);

    QFrame* shellBody = new QFrame(shellExcludePanel);
    shellBody->setObjectName(QStringLiteral("shellExcludeBody"));
    shellBody->setFrameShape(QFrame::NoFrame);
    shellBody->setMinimumHeight(520);
    shellBody->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QHBoxLayout* shellBodyLayout = new QHBoxLayout(shellBody);
    shellBodyLayout->setContentsMargins(0, 0, 0, 0);
    shellBodyLayout->setSpacing(14);

    QWidget* shellTypePanel = new QWidget(shellBody);
    shellTypePanel->setObjectName(QStringLiteral("shellExcludeTypePanel"));
    shellTypePanel->setMinimumWidth(180);
    shellTypePanel->setMaximumWidth(240);
    QVBoxLayout* shellTypePanelLayout = new QVBoxLayout(shellTypePanel);
    shellTypePanelLayout->setContentsMargins(0, 0, 0, 0);
    shellTypePanelLayout->setSpacing(8);
    QLabel* defaultTypeTitle = new QLabel(tr("Default Types"), shellTypePanel);
    defaultTypeTitle->setObjectName(QStringLiteral("shellExcludeGroupTitle"));
    shellTypePanelLayout->addWidget(defaultTypeTitle);

    QListWidget* shellTypeList = new QListWidget(shellTypePanel);
    shellTypeList->setObjectName(QStringLiteral("shellExcludeTypeList"));
    shellTypeList->setFrameShape(QFrame::NoFrame);
    shellTypeList->setFixedHeight(230);
    shellTypeList->setSpacing(4);
    shellTypeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shellTypeList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    shellTypePanelLayout->addWidget(shellTypeList);

    QLabel* customTypeTitle = new QLabel(tr("Custom Extensions"), shellTypePanel);
    customTypeTitle->setObjectName(QStringLiteral("shellExcludeGroupTitle"));
    shellTypePanelLayout->addWidget(customTypeTitle);
    QListWidget* customTypeList = new QListWidget(shellTypePanel);
    customTypeList->setObjectName(QStringLiteral("shellExcludeTypeList"));
    customTypeList->setFrameShape(QFrame::NoFrame);
    customTypeList->setSpacing(4);
    customTypeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    customTypeList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    shellTypePanelLayout->addWidget(customTypeList, 1);
    shellBodyLayout->addWidget(shellTypePanel);

    QWidget* shellDetailPanel = new QWidget(shellBody);
    shellDetailPanel->setObjectName(QStringLiteral("shellExcludeDetail"));
    QVBoxLayout* shellDetailLayout = new QVBoxLayout(shellDetailPanel);
    shellDetailLayout->setContentsMargins(0, 0, 0, 0);
    shellDetailLayout->setSpacing(10);
    shellBodyLayout->addWidget(shellDetailPanel, 1);

    QLabel* shellCurrentTitle = createCardTitle(tr("Desktop"), shellDetailPanel);
    QLabel* shellExcludeStatusLabel = createCardSubtitle(QString(), shellDetailPanel);
    QHBoxLayout* currentHeaderLayout = new QHBoxLayout();
    QVBoxLayout* currentTextLayout = new QVBoxLayout();
    currentTextLayout->setContentsMargins(0, 0, 0, 0);
    currentTextLayout->setSpacing(3);
    currentTextLayout->addWidget(shellCurrentTitle);
    currentTextLayout->addWidget(shellExcludeStatusLabel);
    currentHeaderLayout->addLayout(currentTextLayout, 1);
    shellDetailLayout->addLayout(currentHeaderLayout);

    QWidget* menuTreePane = new QWidget(shellDetailPanel);
    menuTreePane->setObjectName(QStringLiteral("shellExcludePane"));
    menuTreePane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* menuTreeLayout = new QVBoxLayout(menuTreePane);
    menuTreeLayout->setContentsMargins(0, 0, 0, 0);
    menuTreeLayout->setSpacing(6);
    QLabel* menuTreeTitle = createCardSubtitle(tr("Menu Items"), menuTreePane);
    menuTreeLayout->addWidget(menuTreeTitle);
    QListWidget* shellMenuTreeList = new QListWidget(menuTreePane);
    shellMenuTreeList->setObjectName(QStringLiteral("shellExcludeTreeList"));
    shellMenuTreeList->setFrameShape(QFrame::NoFrame);
    shellMenuTreeList->setMinimumHeight(420);
    shellMenuTreeList->setMinimumWidth(360);
    shellMenuTreeList->setSpacing(0);
    shellMenuTreeList->setSelectionMode(QAbstractItemView::SingleSelection);
    shellMenuTreeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shellMenuTreeList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    menuTreeLayout->addWidget(shellMenuTreeList, 1);
    QLabel* shellMenuEmptyLabel = createCardSubtitle(tr("No menu items were found."), menuTreePane);
    shellMenuEmptyLabel->setObjectName(QStringLiteral("shellExcludeEmptyLabel"));
    shellMenuEmptyLabel->setAlignment(Qt::AlignCenter);
    shellMenuEmptyLabel->setVisible(false);
    menuTreeLayout->addWidget(shellMenuEmptyLabel, 0, Qt::AlignCenter);
    shellDetailLayout->addWidget(menuTreePane, 1);

    shellExcludeLayout->addWidget(shellBody);

    auto addTypeItem = [](QListWidget* list, const QString& title, const QString& key) {
        QListWidgetItem* item = new QListWidgetItem(title, list);
        item->setData(Qt::UserRole, key);
        item->setSizeHint(QSize(170, 36));
        return item;
    };
    auto ruleTypeOptions = std::make_shared<QList<ShellMenuRuleTypeOption>>();
    auto addDefaultType = [&](const QString& title, const QString& key) {
        ruleTypeOptions->append({ title, key });
        addTypeItem(shellTypeList, title, key);
    };
    addDefaultType(tr("Desktop"), QStringLiteral("desktop"));
    addDefaultType(tr("Box Background"), QStringLiteral("box_background"));
    addDefaultType(tr("Folder"), QStringLiteral("folder"));
    addDefaultType(tr("Shortcut"), QStringLiteral("shortcut"));
    addDefaultType(tr("No Extension File"), QStringLiteral("file_no_extension"));
    for (const QString& key : shellMenuCustomExtensionKeys()) {
        if (key.startsWith(QStringLiteral("file_ext:"))) {
            const QString title = key.mid(QStringLiteral("file_ext:").size());
            ruleTypeOptions->append({ title, key });
            addTypeItem(customTypeList, title, key);
        }
    }

    auto refreshTypeTitle = [shellCurrentTitle](const QString& key, const QString& fallback) {
        if (key.startsWith(QStringLiteral("file_ext:"))) {
            shellCurrentTitle->setText(key.mid(QStringLiteral("file_ext:").size()));
        } else if (key.startsWith(QStringLiteral("file_progid:"))) {
            shellCurrentTitle->setText(QObject::tr("ProgID %1").arg(key.mid(QStringLiteral("file_progid:").size())));
        } else {
            shellCurrentTitle->setText(fallback);
        }
    };

    auto childRowsForParent = [](QListWidget* list, int row) {
        QVector<int> rows;
        if (!list || row < 0 || row >= list->count()) {
            return rows;
        }
        QListWidgetItem* parentItem = list->item(row);
        if (!parentItem || !parentItem->data(Qt::UserRole + 4).toBool()) {
            return rows;
        }
        const int parentDepth = parentItem->data(Qt::UserRole + 3).toInt();
        for (int childRow = row + 1; childRow < list->count(); ++childRow) {
            QListWidgetItem* childItem = list->item(childRow);
            if (!childItem || childItem->data(Qt::UserRole + 3).toInt() <= parentDepth) {
                break;
            }
            rows.push_back(childRow);
        }
        return rows;
    };

    auto entriesForTreeRow = [childRowsForParent](QListWidget* list, int row) {
        std::vector<ShellMenuExclusionEntry> entries;
        if (!list || row < 0 || row >= list->count()) {
            return entries;
        }
        if (QListWidgetItem* item = list->item(row)) {
            entries.push_back(shellMenuEntryFromItem(item));
        }
        for (const int childRow : childRowsForParent(list, row)) {
            if (QListWidgetItem* childItem = list->item(childRow)) {
                entries.push_back(shellMenuEntryFromItem(childItem));
            }
        }
        return entries;
    };

    auto removeExclusionEntry = [](const QString& key, const ShellMenuExclusionEntry& entry) {
        ShellMenuExclusionStore::remove(key, entry);
    };

    auto updateTreeStatusLabel = [shellMenuTreeList, shellExcludeStatusLabel](const QString& key) {
        int visibleCount = 0;
        int hiddenCount = 0;
        for (int rowIndex = 0; rowIndex < shellMenuTreeList->count(); ++rowIndex) {
            QListWidgetItem* item = shellMenuTreeList->item(rowIndex);
            QWidget* rowWidget = item ? shellMenuTreeList->itemWidget(item) : nullptr;
            FluentCheckBox* checkbox = rowWidget ? rowWidget->findChild<FluentCheckBox*>(QStringLiteral("shellExcludeRowCheckBox")) : nullptr;
            if (!checkbox) {
                continue;
            }
            if (checkbox->isChecked()) {
                ++visibleCount;
            } else {
                ++hiddenCount;
            }
        }
        shellExcludeStatusLabel->setText(QObject::tr("%1 visible, %2 hidden for %3.")
            .arg(visibleCount)
            .arg(hiddenCount)
            .arg(key));
    };

    auto updateTreeFromExclusions = [shellMenuTreeList](const std::vector<ShellMenuExclusionEntry>& exclusions) {
        for (int rowIndex = 0; rowIndex < shellMenuTreeList->count(); ++rowIndex) {
            QListWidgetItem* item = shellMenuTreeList->item(rowIndex);
            QWidget* rowWidget = item ? shellMenuTreeList->itemWidget(item) : nullptr;
            FluentCheckBox* checkbox = rowWidget ? rowWidget->findChild<FluentCheckBox*>(QStringLiteral("shellExcludeRowCheckBox")) : nullptr;
            if (!item || !checkbox) {
                continue;
            }
            const QString rawText = item->data(Qt::UserRole).toString();
            const QString normalizedText = item->data(Qt::UserRole + 1).toString();
            const QString matchText = item->data(Qt::UserRole + 5).toString();
            const QStringList matchPath = item->data(Qt::UserRole + 7).toStringList();
            const QSignalBlocker blocker(checkbox);
            checkbox->setChecked(!shellMenuEntryMatches(exclusions, rawText, normalizedText, matchText, matchPath));
        }
    };

    auto makeMenuRow = [this](QListWidget* list, const QIcon& icon, int depth, bool hasChildren, const QString& rawText, bool visible) {
        QWidget* row = new QWidget(list);
        row->setObjectName(QStringLiteral("shellExcludeRow"));
        row->setAttribute(Qt::WA_Hover, true);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 5, 12, 5);
        rowLayout->setSpacing(9);

        if (depth > 0) {
            QWidget* indent = new QWidget(row);
            indent->setFixedWidth(qMin(72, depth * 20));
            rowLayout->addWidget(indent);
        }

        QLabel* branchLabel = new QLabel(row);
        branchLabel->setObjectName(QStringLiteral("shellExcludeBranchIcon"));
        branchLabel->setFixedSize(16, 22);
        branchLabel->setAlignment(Qt::AlignCenter);
        branchLabel->setText(hasChildren ? glyph(0xE70D) : QString());
        rowLayout->addWidget(branchLabel, 0, Qt::AlignVCenter);

        FluentCheckBox* checkbox = new FluentCheckBox(row);
        checkbox->setObjectName(QStringLiteral("shellExcludeRowCheckBox"));
        checkbox->setChecked(visible);
        rowLayout->addWidget(checkbox, 0, Qt::AlignVCenter);

        QLabel* iconLabel = new QLabel(row);
        iconLabel->setObjectName(QStringLiteral("shellExcludeRowIcon"));
        iconLabel->setFixedSize(20, 20);
        iconLabel->setAlignment(Qt::AlignCenter);
        if (!icon.isNull()) {
            iconLabel->setPixmap(icon.pixmap(16, 16));
        }
        rowLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

        QLabel* title = new QLabel(rawText, row);
        title->setObjectName(QStringLiteral("shellExcludeRowTitle"));
        title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        title->setTextInteractionFlags(Qt::NoTextInteraction);
        rowLayout->addWidget(title, 1, Qt::AlignVCenter);
        return row;
    };

    auto scanShellMenu = std::make_shared<std::function<void(const QString&)>>();

    auto isCustomExtensionKey = [](const QString& key) {
        return key.startsWith(QStringLiteral("file_ext:")) && shellMenuCustomExtensionKeys().contains(key);
    };

    *scanShellMenu = [=](const QString& key) {
        shellMenuTreeList->clear();
        shellMenuEmptyLabel->setVisible(false);
        const auto exclusions = ShellMenuExclusionStore::load(key);
        auto items = RightMenu::getInstance()->shellMenuPreviewItems(key);
        if (key == QStringLiteral("desktop") || key == QStringLiteral("box_background")) {
            prioritizeDesktopMenuItems(items);
        }
        int rowCount = 0;
        int hiddenCount = 0;
        std::function<void(const std::vector<ShellMenuSnapshotItem>&, int)> appendItems;
        appendItems = [&](const std::vector<ShellMenuSnapshotItem>& menuItems, int depth) {
            for (const ShellMenuSnapshotItem& shellItem : menuItems) {
                if (shellItem.isSeparator || shellItem.text.empty()) {
                    continue;
                }
                const QString rawText = shellMenuDisplayText(QString::fromStdWString(shellItem.text));
                const QString storedRawText = QString::fromStdWString(shellItem.text);
                const QString normalizedText = QString::fromStdWString(shellItem.normalizedText);
                const QString matchText = QString::fromStdWString(shellItem.matchText);
                const QStringList menuPath = wideVectorToStringList(shellItem.menuPath);
                const QStringList matchPath = wideVectorToStringList(shellItem.matchPath);
                const bool visible = !shellMenuEntryMatches(exclusions, storedRawText, normalizedText, matchText, matchPath);
                if (!visible) {
                    ++hiddenCount;
                }
                const QString source = QString::fromStdWString(shellItem.source).isEmpty()
                    ? tr("system")
                    : QString::fromStdWString(shellItem.source);
                QListWidgetItem* item = new QListWidgetItem(shellMenuTreeList);
                item->setData(Qt::UserRole, storedRawText);
                item->setData(Qt::UserRole + 1, normalizedText);
                item->setData(Qt::UserRole + 2, source);
                item->setData(Qt::UserRole + 3, depth);
                item->setData(Qt::UserRole + 4, !shellItem.children.empty());
                item->setData(Qt::UserRole + 5, matchText);
                item->setData(Qt::UserRole + 6, menuPath);
                item->setData(Qt::UserRole + 7, matchPath);
                QWidget* row = makeMenuRow(shellMenuTreeList, shellMenuBitmapIcon(shellItem.hBitmap), depth, !shellItem.children.empty(), rawText, visible);
                item->setSizeHint(row->sizeHint().expandedTo(QSize(0, 36)));
                shellMenuTreeList->setItemWidget(item, row);
                ++rowCount;
                if (!shellItem.children.empty()) {
                    appendItems(shellItem.children, depth + 1);
                }
            }
        };
        appendItems(items, 0);
        shellMenuEmptyLabel->setVisible(rowCount == 0);
        shellMenuTreeList->setVisible(rowCount > 0);
        shellExcludeStatusLabel->setText(rowCount == 0
            ? tr("No menu items were found for %1.").arg(key)
            : tr("%1 visible, %2 hidden for %3.").arg(rowCount - hiddenCount).arg(hiddenCount).arg(key));

        for (int rowIndex = 0; rowIndex < shellMenuTreeList->count(); ++rowIndex) {
            QListWidgetItem* item = shellMenuTreeList->item(rowIndex);
            QWidget* rowWidget = item ? shellMenuTreeList->itemWidget(item) : nullptr;
            FluentCheckBox* checkbox = rowWidget ? rowWidget->findChild<FluentCheckBox*>(QStringLiteral("shellExcludeRowCheckBox")) : nullptr;
            if (!item || !checkbox) {
                continue;
            }
            connect(checkbox, &FluentCheckBox::toggled, this, [=](bool checked) {
                const int clickedRow = shellMenuTreeList->row(item);
                const std::vector<ShellMenuExclusionEntry> entries = entriesForTreeRow(shellMenuTreeList, clickedRow);
                if (checked) {
                    for (const ShellMenuExclusionEntry& entry : entries) {
                        removeExclusionEntry(key, entry);
                    }
                } else {
                    for (const ShellMenuExclusionEntry& entry : entries) {
                        ShellMenuExclusionStore::add(key, entry);
                    }
                }
                RightMenu::getInstance()->invalidateCustomMenuCaches();

                for (const int childRow : childRowsForParent(shellMenuTreeList, clickedRow)) {
                    QListWidgetItem* childItem = shellMenuTreeList->item(childRow);
                    QWidget* childRowWidget = childItem ? shellMenuTreeList->itemWidget(childItem) : nullptr;
                    FluentCheckBox* childCheckbox = childRowWidget ? childRowWidget->findChild<FluentCheckBox*>(QStringLiteral("shellExcludeRowCheckBox")) : nullptr;
                    if (!childCheckbox) {
                        continue;
                    }
                    const QSignalBlocker blocker(childCheckbox);
                    childCheckbox->setChecked(checked);
                }
                updateTreeStatusLabel(key);
            });
        }
    };

    auto applyExclusionFilterAvailability = [=](bool filterEnabled) {
        shellBody->setEnabled(filterEnabled);
        addExtensionButton->setEnabled(filterEnabled);
        copyRulesButton->setEnabled(filterEnabled);
        applyRulesButton->setEnabled(filterEnabled);
        QListWidgetItem* current = shellTypeList->currentItem() ? shellTypeList->currentItem() : customTypeList->currentItem();
        const QString currentKey = current ? current->data(Qt::UserRole).toString() : QString();
        const bool canRemoveType = isCustomExtensionKey(currentKey);
        removeExtensionButton->setVisible(canRemoveType);
        removeExtensionButton->setEnabled(filterEnabled && canRemoveType);
    };

    connect(enableFilterToggle, &FluentToggleSwitch::toggled, this, [=](bool checked) {
        RightMenu::setShellMenuExclusionEnabled(checked);
        applyExclusionFilterAvailability(checked);
    });

    auto selectTypeKey = [=](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        QListWidget* sourceList = item->listWidget();
        const QSignalBlocker defaultBlocker(shellTypeList);
        const QSignalBlocker customBlocker(customTypeList);
        if (sourceList == shellTypeList) {
            customTypeList->setCurrentItem(nullptr);
        } else if (sourceList == customTypeList) {
            shellTypeList->setCurrentItem(nullptr);
        }
        const QString key = item->data(Qt::UserRole).toString();
        refreshTypeTitle(key, item->text());
        const bool canRemoveType = isCustomExtensionKey(key);
        removeExtensionButton->setVisible(canRemoveType);
        removeExtensionButton->setEnabled(enableFilterToggle->checked() && canRemoveType);
        (*scanShellMenu)(key);
    };

    connect(shellTypeList, &QListWidget::currentItemChanged, this, [=](QListWidgetItem* current) {
        selectTypeKey(current);
    });
    connect(customTypeList, &QListWidget::currentItemChanged, this, [=](QListWidgetItem* current) {
        selectTypeKey(current);
    });

    connect(copyRulesButton, &QPushButton::clicked, this, [=]() {
        QListWidgetItem* current = shellTypeList->currentItem() ? shellTypeList->currentItem() : customTypeList->currentItem();
        if (!current) {
            return;
        }

        FluentDialog dialog(this);
        dialog.setDialogTitle(tr("Copy Rules"));
        dialog.setMessage(tr("Choose a source type to copy hidden menu rules from."));
        FluentComboBox* sourceCombo = new FluentComboBox(&dialog);
        for (const ShellMenuRuleTypeOption& option : *ruleTypeOptions) {
            sourceCombo->addItem(option.title, option.key);
        }
        dialog.contentLayout()->insertWidget(dialog.contentLayout()->count() - 1, sourceCombo);

        bool confirmed = false;
        QString sourceKey;
        dialog.addActionButton(tr("Copy"), FluentDialog::ActionType::Primary, [&]() {
            confirmed = true;
            sourceKey = sourceCombo->currentData().toString();
        });
        dialog.addActionButton(tr("Cancel"), FluentDialog::ActionType::Secondary);
        if (dialog.exec() != QDialog::Accepted || !confirmed || sourceKey.isEmpty()) {
            return;
        }

        const std::vector<ShellMenuExclusionEntry> sourceEntries = ShellMenuExclusionStore::load(sourceKey);
        if (sourceEntries.empty()) {
            shellExcludeStatusLabel->setText(tr("No hidden rules to copy."));
            return;
        }

        const QString targetKey = current->data(Qt::UserRole).toString();
        const int addedCount = ShellMenuExclusionStore::merge(targetKey, sourceEntries);
        RightMenu::getInstance()->invalidateCustomMenuCaches();
        updateTreeFromExclusions(ShellMenuExclusionStore::load(targetKey));
        updateTreeStatusLabel(targetKey);
        shellExcludeStatusLabel->setText(tr("Copied %1 rules from %2.")
            .arg(addedCount)
            .arg(shellMenuTypeTitle(*ruleTypeOptions, sourceKey)));
    });

    connect(applyRulesButton, &QPushButton::clicked, this, [=]() {
        QListWidgetItem* current = shellTypeList->currentItem() ? shellTypeList->currentItem() : customTypeList->currentItem();
        if (!current) {
            return;
        }

        const QString sourceKey = current->data(Qt::UserRole).toString();
        const std::vector<ShellMenuExclusionEntry> sourceEntries = ShellMenuExclusionStore::load(sourceKey);
        if (sourceEntries.empty()) {
            shellExcludeStatusLabel->setText(tr("No hidden rules to copy."));
            return;
        }

        FluentDialog dialog(this);
        dialog.setDialogTitle(tr("Apply Rules"));
        dialog.setMessage(tr("Choose target types to receive the current hidden menu rules."));
        QListWidget* targetList = new QListWidget(&dialog);
        targetList->setObjectName(QStringLiteral("shellExcludeTypeList"));
        targetList->setFrameShape(QFrame::NoFrame);
        targetList->setSelectionMode(QAbstractItemView::NoSelection);
        targetList->setMinimumHeight(220);
        for (const ShellMenuRuleTypeOption& option : *ruleTypeOptions) {
            if (option.key == sourceKey) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(targetList);
            item->setData(Qt::UserRole, option.key);
            item->setSizeHint(QSize(260, 34));
            QWidget* row = new QWidget(targetList);
            QHBoxLayout* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(10, 4, 10, 4);
            rowLayout->setSpacing(10);
            FluentCheckBox* checkbox = new FluentCheckBox(row);
            checkbox->setObjectName(QStringLiteral("applyRulesTargetCheckBox"));
            rowLayout->addWidget(checkbox, 0, Qt::AlignVCenter);
            QLabel* label = new QLabel(option.title, row);
            label->setObjectName(QStringLiteral("shellExcludeRowTitle"));
            rowLayout->addWidget(label, 1, Qt::AlignVCenter);
            targetList->setItemWidget(item, row);
            connect(checkbox, &FluentCheckBox::toggled, this, [targetList, checkbox](bool checked) {
                if (!checked) {
                    return;
                }
                for (int rowIndex = 0; rowIndex < targetList->count(); ++rowIndex) {
                    QListWidgetItem* otherItem = targetList->item(rowIndex);
                    QWidget* otherRow = otherItem ? targetList->itemWidget(otherItem) : nullptr;
                    FluentCheckBox* otherCheckBox = otherRow ? otherRow->findChild<FluentCheckBox*>(QStringLiteral("applyRulesTargetCheckBox")) : nullptr;
                    if (otherCheckBox && otherCheckBox != checkbox) {
                        const QSignalBlocker blocker(otherCheckBox);
                        otherCheckBox->setChecked(false);
                    }
                }
            });
        }
        dialog.contentLayout()->insertWidget(dialog.contentLayout()->count() - 1, targetList);

        bool confirmed = false;
        QStringList targetKeys;
        dialog.addActionButton(tr("Apply"), FluentDialog::ActionType::Primary, [&]() {
            confirmed = true;
            for (int row = 0; row < targetList->count(); ++row) {
                QListWidgetItem* item = targetList->item(row);
                QWidget* rowWidget = item ? targetList->itemWidget(item) : nullptr;
                FluentCheckBox* checkbox = rowWidget ? rowWidget->findChild<FluentCheckBox*>(QStringLiteral("applyRulesTargetCheckBox")) : nullptr;
                if (item && checkbox && checkbox->isChecked()) {
                    targetKeys.append(item->data(Qt::UserRole).toString());
                }
            }
        });
        dialog.addActionButton(tr("Cancel"), FluentDialog::ActionType::Secondary);
        if (dialog.exec() != QDialog::Accepted || !confirmed) {
            return;
        }
        if (targetKeys.isEmpty()) {
            shellExcludeStatusLabel->setText(tr("No target type selected."));
            return;
        }

        int addedCount = 0;
        for (const QString& targetKey : targetKeys) {
            addedCount += ShellMenuExclusionStore::merge(targetKey, sourceEntries);
        }
        RightMenu::getInstance()->invalidateCustomMenuCaches();
        shellExcludeStatusLabel->setText(tr("Applied %1 rules to %2 types.")
            .arg(addedCount)
            .arg(targetKeys.size()));
    });

    connect(addExtensionButton, &QPushButton::clicked, this, [=]() {
        FluentDialog dialog(this);
        dialog.setDialogTitle(tr("File Extension"));
        dialog.setMessage(tr("Enter a file extension, for example .txt."));
        QLabel* extensionLabel = new QLabel(tr("Extension"), &dialog);
        extensionLabel->setObjectName(QStringLiteral("rowTitle"));
        dialog.contentLayout()->insertWidget(dialog.contentLayout()->count() - 1, extensionLabel);
        FluentInput* extensionInput = new FluentInput(&dialog);
        extensionInput->setText(QStringLiteral("txt"));
        extensionInput->selectAll();
        dialog.contentLayout()->insertWidget(dialog.contentLayout()->count() - 1, extensionInput);
        QLabel* reuseLabel = new QLabel(tr("Reuse Rules From"), &dialog);
        reuseLabel->setObjectName(QStringLiteral("rowTitle"));
        dialog.contentLayout()->insertWidget(dialog.contentLayout()->count() - 1, reuseLabel);
        FluentComboBox* reuseCombo = new FluentComboBox(&dialog);
        reuseCombo->setFixedWidth(260);
        reuseCombo->addItem(tr("Do Not Reuse"), QString());
        for (const ShellMenuRuleTypeOption& option : *ruleTypeOptions) {
            if (option.key.startsWith(QStringLiteral("file_ext:"))) {
                reuseCombo->addItem(option.title, option.key);
            }
        }
        dialog.contentLayout()->insertWidget(dialog.contentLayout()->count() - 1, reuseCombo);
        QLabel* validationLabel = new QLabel(&dialog);
        validationLabel->setObjectName(QStringLiteral("rowSubtitle"));
        validationLabel->setStyleSheet(QStringLiteral("color: #d13438;"));
        validationLabel->setWordWrap(true);
        validationLabel->hide();
        dialog.contentLayout()->insertWidget(dialog.contentLayout()->count() - 1, validationLabel);

        bool confirmed = false;
        QString extension;
        QString reuseKey;
        dialog.addActionButton(tr("Add"), FluentDialog::ActionType::Primary, [&]() {
            QString input = extensionInput->text().trimmed().toLower();
            if (input.isEmpty()) {
                validationLabel->setText(tr("Enter a file extension."));
                validationLabel->show();
                dialog.keepOpenOnce();
                QTimer::singleShot(0, extensionInput, qOverload<>(&FluentInput::setFocus));
                return;
            }
            if (!input.startsWith('.')) {
                input.prepend('.');
            }

            const QString candidateKey = QStringLiteral("file_ext:") + input;
            for (int i = 0; i < customTypeList->count(); ++i) {
                if (customTypeList->item(i)->data(Qt::UserRole).toString() == candidateKey) {
                    customTypeList->setCurrentRow(i);
                    validationLabel->setText(tr("Extension type already exists: %1").arg(input));
                    validationLabel->show();
                    shellExcludeStatusLabel->setText(tr("Extension type already exists: %1").arg(input));
                    dialog.keepOpenOnce();
                    QTimer::singleShot(0, extensionInput, qOverload<>(&FluentInput::setFocus));
                    return;
                }
            }

            confirmed = true;
            extension = input;
            reuseKey = reuseCombo->currentData().toString();
        });
        dialog.addActionButton(tr("Cancel"), FluentDialog::ActionType::Secondary);
        QTimer::singleShot(0, extensionInput, qOverload<>(&FluentInput::setFocus));

        if (dialog.exec() != QDialog::Accepted || !confirmed) {
            return;
        }

        const QString key = QStringLiteral("file_ext:") + extension;
        QListWidgetItem* item = addTypeItem(customTypeList, extension, key);
        ruleTypeOptions->append({ extension, key });
        QStringList customKeys = shellMenuCustomExtensionKeys();
        customKeys.append(key);
        saveShellMenuCustomExtensionKeys(customKeys);
        customTypeList->setCurrentItem(item);
        int copiedCount = 0;
        if (!reuseKey.isEmpty()) {
            copiedCount += ShellMenuExclusionStore::merge(key, ShellMenuExclusionStore::load(reuseKey));
        }
        if (copiedCount > 0) {
            updateTreeFromExclusions(ShellMenuExclusionStore::load(key));
            updateTreeStatusLabel(key);
            shellExcludeStatusLabel->setText(tr("Added %1 and reused %2 rules.")
                .arg(extension)
                .arg(copiedCount));
        }
    });

    connect(removeExtensionButton, &QPushButton::clicked, this, [=]() {
        QListWidgetItem* current = customTypeList->currentItem();
        if (!current) {
            return;
        }

        const QString key = current->data(Qt::UserRole).toString();
        if (!isCustomExtensionKey(key)) {
            return;
        }

        QStringList customKeys = shellMenuCustomExtensionKeys();
        customKeys.removeAll(key);
        saveShellMenuCustomExtensionKeys(customKeys);
        for (int optionIndex = 0; optionIndex < ruleTypeOptions->size(); ++optionIndex) {
            if (ruleTypeOptions->at(optionIndex).key == key) {
                ruleTypeOptions->removeAt(optionIndex);
                break;
            }
        }

        const int row = customTypeList->row(current);
        delete customTypeList->takeItem(row);
        const int nextRow = qBound(0, qMin(row, customTypeList->count() - 1), customTypeList->count() - 1);
        if (nextRow >= 0) {
            customTypeList->setCurrentRow(nextRow);
            shellExcludeStatusLabel->setText(tr("Removed extension type: %1").arg(key.mid(QStringLiteral("file_ext:").size())));
        } else {
            shellTypeList->setCurrentRow(0);
            shellExcludeStatusLabel->setText(tr("Removed extension type."));
            removeExtensionButton->setEnabled(false);
            removeExtensionButton->setVisible(false);
        }
    });

    m_shellTypeList = shellTypeList;
    applyExclusionFilterAvailability(enableFilterToggle->checked());
    layout->addWidget(shellExcludePanel, 0);
    return page;
}

QWidget* SettingUI::createGridPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    m_boxPageTitle = createPageTitle(tr("Box UI"), page);
    m_boxPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_boxPageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    QWidget* previewContainer = createPanel(page);
    m_boxPreviewContainer = previewContainer;
    previewContainer->setFixedHeight(livePreviewHeightForWindow());
    previewContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(22, 10, 22, 20);
    previewLayout->setSpacing(6);
    
    m_previewLabel = createCardTitle(tr("Live Preview"), previewContainer);
    previewLayout->addWidget(m_previewLabel);
    previewLayout->addWidget(createCardSubtitle(tr("See how your desktop box surface reacts to the current style."), previewContainer));
    
    m_previewBackground = new QLabel(previewContainer);
    m_previewBackground->setMinimumHeight(124);
    m_previewBackground->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewBackground->setScaledContents(false);
    m_previewBackground->setObjectName("previewSurface");
    m_previewBackground->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewBackground);
    previewLayout->addStretch();

    layout->addWidget(previewContainer, 0);

    QWidget* stylePanel = createPanel(page);
    stylePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* styleLayout = new QVBoxLayout(stylePanel);
    styleLayout->setContentsMargins(22, 18, 22, 18);
    styleLayout->setSpacing(0);

    m_boxColorSectionTitle = createCardTitle(tr("Accent Color"), stylePanel);
    styleLayout->addWidget(m_boxColorSectionTitle);
    styleLayout->addSpacing(12);

    QColor initialBoxColor = QColor("#63A8F5");
    if (LinBox* sourceBox = controlSourceBox()) {
        initialBoxColor = sourceBox->backgroundColor();
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialBoxColor = bm->unifiedBackgroundColor();
    }

    QWidget* presetsRow = new QWidget(stylePanel);
    presetsRow->setObjectName("settingsInnerRow");
    presetsRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_boxColorPresetsRow = presetsRow;
    QHBoxLayout* presetsLayout = new QHBoxLayout(presetsRow);
    presetsLayout->setContentsMargins(12, 8, 12, 8);
    presetsLayout->setSpacing(8);
    QLabel* presetsLabel = new QLabel(tr("Presets"), presetsRow);
    presetsLabel->setObjectName("rowTitle");
    presetsLayout->addWidget(presetsLabel);
    presetsLayout->addStretch();

    m_boxColorValueEdit = new QLineEdit(presetsRow);
    m_boxColorValueEdit->setReadOnly(true);
    m_boxColorValueEdit->setAlignment(Qt::AlignCenter);
    m_boxColorValueEdit->setMinimumWidth(72);
    m_boxColorValueEdit->setMaximumWidth(84);
    m_boxColorValueEdit->setFixedHeight(30);
    m_boxColorValueEdit->setText(initialBoxColor.name().toUpper());
    m_boxColorValueEdit->setObjectName("valueEdit");
    presetsLayout->addWidget(m_boxColorValueEdit);

    auto applyBoxColor = [this](const QColor& color) {
        if (!color.isValid()) {
            return;
        }
        for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setBackgroundColor(color);
            }
        }
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesBackgroundColor(color);
        }
        if (m_boxColorValueEdit) {
            m_boxColorValueEdit->setText(color.name().toUpper());
        }
        updateColorPresetIndicators(m_boxColorPresetsRow, color);
        m_previewColor = color;
        updatePreviewStyle();
        if (ListView* lv = ListView::instance()) {
            lv->requestSaveBoxes();
        }
    };

    const QVector<QColor> boxPresets = {
        QColor("#F0CF24"),
        QColor("#63A8F5"),
        QColor("#00C853"),
        QColor("#8B45E6"),
        QColor("#E94B98"),
        QColor("#FF8A24"),
        QColor("#AEB4BE")
    };
    for (const QColor& preset : boxPresets) {
        QWidget* presetContainer = new QWidget(presetsRow);
        presetContainer->setObjectName("colorPresetContainer");
        QVBoxLayout* presetContainerLayout = new QVBoxLayout(presetContainer);
        presetContainerLayout->setContentsMargins(0, 0, 0, 0);
        presetContainerLayout->setSpacing(3);

        QPushButton* button = createColorPreset(preset, presetContainer);
        button->setFixedSize(24, 24);
        QLabel* indicator = new QLabel(presetContainer);
        indicator->setObjectName("colorPresetIndicator");
        indicator->setProperty("presetColor", preset.name());
        indicator->setFixedSize(6, 6);
        indicator->setStyleSheet(QStringLiteral(
            "QLabel#colorPresetIndicator {"
            " background: #1870ff;"
            " border-radius: 3px;"
            "}"));
        indicator->setVisible(false);

        presetContainerLayout->addWidget(button, 0, Qt::AlignHCenter);
        presetContainerLayout->addWidget(indicator, 0, Qt::AlignHCenter);
        connect(button, &QPushButton::clicked, this, [applyBoxColor, preset]() {
            applyBoxColor(preset);
        });
        presetsLayout->addWidget(presetContainer);
    }

    QWidget* pickerContainer = new QWidget(presetsRow);
    pickerContainer->setObjectName("colorPresetContainer");
    QVBoxLayout* pickerContainerLayout = new QVBoxLayout(pickerContainer);
    pickerContainerLayout->setContentsMargins(0, 0, 0, 0);
    pickerContainerLayout->setSpacing(3);

    QPushButton* pickerButton = new QPushButton(glyph(0xE790), pickerContainer);
    pickerButton->setObjectName("colorPickerButton");
    pickerButton->setFixedSize(24, 24);
    pickerButton->setCursor(Qt::PointingHandCursor);
    QLabel* pickerIndicator = new QLabel(pickerContainer);
    pickerIndicator->setObjectName("colorPresetIndicator");
    pickerIndicator->setProperty("customColorIndicator", true);
    pickerIndicator->setFixedSize(6, 6);
    pickerIndicator->setStyleSheet(QStringLiteral(
        "QLabel#colorPresetIndicator {"
        " background: #1870ff;"
        " border-radius: 3px;"
        "}"));
    pickerIndicator->setVisible(false);
    pickerContainerLayout->addWidget(pickerButton, 0, Qt::AlignHCenter);
    pickerContainerLayout->addWidget(pickerIndicator, 0, Qt::AlignHCenter);
    presetsLayout->addWidget(pickerContainer);
    updateColorPresetIndicators(presetsRow, initialBoxColor);

    connect(pickerButton, &QPushButton::clicked, this, [this, applyBoxColor]() {
        QColorDialog* dialog = new QColorDialog(m_previewColor, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Accent Color"));
        dialog->setOption(QColorDialog::NoButtons, true);
        connect(dialog, &QColorDialog::currentColorChanged, this, [applyBoxColor](const QColor& color) {
            applyBoxColor(color);
        });
        connect(dialog, &QColorDialog::colorSelected, this, [applyBoxColor](const QColor& color) {
            applyBoxColor(color);
        });
        dialog->open();
    });

    styleLayout->addWidget(presetsRow);
    styleLayout->addWidget(createDivider(stylePanel));

    m_opacitySlider = new QSlider(Qt::Horizontal, page);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setMinimumWidth(120);
    m_opacitySlider->setMaximumWidth(160);
    m_opacitySlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_opacityLabelValue = new QLabel(page);
    m_opacityLabelValue->setObjectName("valueLabel");
    m_opacityLabelValue->setFixedWidth(44);
    m_opacityLabelValue->setAlignment(Qt::AlignCenter);

    QWidget* opacityRow = new QWidget(stylePanel);
    opacityRow->setObjectName("settingsInnerRow");
    opacityRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(14, 8, 14, 8);
    opacityLayout->setSpacing(12);
    QVBoxLayout* opacityTextLayout = new QVBoxLayout();
    opacityTextLayout->setContentsMargins(0, 0, 0, 0);
    opacityTextLayout->setSpacing(2);
    QLabel* opacityTitle = new QLabel(tr("Background Opacity"), opacityRow);
    opacityTitle->setObjectName("rowTitle");
    opacityTitle->setWordWrap(true);
    opacityTitle->setMinimumWidth(0);
    opacityTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QLabel* opacitySub = new QLabel(tr("Adjust the transparency of the box background."), opacityRow);
    opacitySub->setObjectName("rowSubtitle");
    opacitySub->setWordWrap(true);
    opacitySub->setMinimumWidth(0);
    opacitySub->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    opacityTextLayout->addWidget(opacityTitle);
    opacityTextLayout->addWidget(opacitySub);
    opacityLayout->addLayout(opacityTextLayout, 1);
    opacityLayout->addWidget(createGlyphLabel(glyph(0xEB48), opacityRow, 18, QColor("#5b6b86")));
    opacityLayout->addWidget(m_opacitySlider, 2);
    opacityLayout->addWidget(m_opacityLabelValue);

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){
        for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setBackgroundOpacity(v / 100.0f);
            }
        }
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesBackgroundOpacity(v / 100.0f);
        }
        m_opacityLabelValue->setText(QString("%1%").arg(v));
        m_previewOpacity = v / 100.0f;  // Sync preview opacity
        updatePreviewStyle();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });
    m_opacityCard = nullptr;
    styleLayout->addWidget(opacityRow);
    styleLayout->addWidget(createDivider(stylePanel));

    int initialOpacity = qRound(m_previewOpacity * 100.0f);
    if (LinBox* sourceBox = controlSourceBox()) {
        initialOpacity = qRound(sourceBox->backgroundOpacity() * 100.0f);
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialOpacity = qRound(bm->unifiedBackgroundOpacity() * 100.0f);
    }
    initialOpacity = qBound(m_opacitySlider->minimum(), initialOpacity, m_opacitySlider->maximum());
    m_opacitySlider->setValue(initialOpacity);
    m_opacityLabelValue->setText(QString("%1%").arg(initialOpacity));

    m_glassToggle = new FluentToggleSwitch(page);
    bool initialGlass = false;
    if (LinBox* sourceBox = controlSourceBox()) {
        initialGlass = sourceBox->glassEffect();
    } else if (BoxManager* bm = BoxManager::instance()) {
        initialGlass = bm->unifiedGlassEffect();
    }
    m_glassToggle->setChecked(initialGlass, false);
    
    connect(m_glassToggle, &FluentToggleSwitch::toggled, this, [this](bool v){ 
        for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setGlassEffect(v);
            }
        }
        if (BoxManager* bm = BoxManager::instance()) {
            bm->setAllBoxesGlassEffect(v);
        }
        m_previewGlass = v;  // Sync preview glass
        updatePreviewStyle();
        ListView* lv = ListView::instance();
        if (lv) lv->requestSaveBoxes();
    });

    m_glassCard = nullptr;
    styleLayout->addWidget(createSwitchRow(glyph(0xE91B), tr("Acrylic Glass Effect"), tr("Enable real-time DWM background blur for a translucent look."), m_glassToggle, stylePanel));
    styleLayout->addWidget(createDivider(stylePanel));

    QSettings boxSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    bool initialAutoPeek = boxSettings.value(QStringLiteral("Box/AutoPeekOnHover"), true).toBool();
    bool initialAutoPeekInstant = boxSettings.value(QStringLiteral("Box/AutoPeekInstant"), false).toBool();

    FluentToggleSwitch* autoPeekToggle = new FluentToggleSwitch(page);
    autoPeekToggle->setChecked(initialAutoPeek, false);
    connect(autoPeekToggle, &FluentToggleSwitch::toggled, this, [](bool v){
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Box/AutoPeekOnHover"), v);
        settings.sync();
        if (BoxManager* bm = BoxManager::instance()) {
            for (const auto& boxPtr : bm->allBoxes()) {
                if (LinBox* box = boxPtr.data()) {
                    box->setAutoPeekEnabled(v);
                }
            }
        }
    });
    styleLayout->addWidget(createSwitchRow(glyph(0xE7B3), tr("Auto-Peek on Hover"), tr("Automatically expand collapsed boxes when hovered, and roll up when the mouse leaves."), autoPeekToggle, stylePanel));

    styleLayout->addWidget(createDivider(stylePanel));

    FluentToggleSwitch* autoPeekInstantToggle = new FluentToggleSwitch(page);
    autoPeekInstantToggle->setChecked(initialAutoPeekInstant, false);
    connect(autoPeekInstantToggle, &FluentToggleSwitch::toggled, this, [](bool v){
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Box/AutoPeekInstant"), v);
        settings.sync();
        if (BoxManager* bm = BoxManager::instance()) {
            for (const auto& boxPtr : bm->allBoxes()) {
                if (LinBox* box = boxPtr.data()) {
                    box->setAutoPeekInstant(v);
                }
            }
        }
    });
    styleLayout->addWidget(createSwitchRow(glyph(0xE823), tr("Instant Auto-Peek"), tr("Expand immediately on hover without a 150ms debounce delay."), autoPeekInstantToggle, stylePanel));
    layout->addWidget(stylePanel);

    if (LinBox* sourceBox = controlSourceBox()) {
        m_previewColor = sourceBox->backgroundColor();
        m_previewOpacity = sourceBox->backgroundOpacity();
        m_previewGlass = sourceBox->glassEffect();
    } else {
        m_previewColor = QColor(40, 40, 40);
    }
    updatePreviewStyle();

    layout->addStretch();
    return page;
}

namespace {
QString runtimeLinWidgetsDirForSettings()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
}

QString marketplaceFilePathForSettings()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgetMarketplace/marketplace.json"));
}

QString safeLinWidgetFileName(QString id)
{
    for (QChar& ch : id) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    return id.isEmpty() ? QStringLiteral("widget") : id;
}

bool settingsSkipLinWidgetCopyEntry(const QString& name)
{
    return name == QStringLiteral(".git") ||
        name == QStringLiteral("node_modules") ||
        name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0;
}

bool settingsCopyLinWidgetDirectory(const QString& sourcePath, const QString& targetPath, QString* errorMessage)
{
    const QDir source(sourcePath);
    if (!source.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source widget directory does not exist: %1").arg(sourcePath);
        }
        return false;
    }
    if (!QDir().mkpath(targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create widget directory: %1").arg(targetPath);
        }
        return false;
    }

    const QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (settingsSkipLinWidgetCopyEntry(entry.fileName())) {
            continue;
        }
        const QString targetEntryPath = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!settingsCopyLinWidgetDirectory(entry.absoluteFilePath(), targetEntryPath, errorMessage)) {
                return false;
            }
        } else {
            QFile::remove(targetEntryPath);
            if (!QFile::copy(entry.absoluteFilePath(), targetEntryPath)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Cannot copy widget file: %1").arg(entry.absoluteFilePath());
                }
                return false;
            }
        }
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool settingsRemoveDirectory(const QString& path, QString* errorMessage)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    if (!info.isDir() || !QDir(path).removeRecursively()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot remove directory: %1").arg(path);
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QString settingsInstalledTargetForManifest(const LinWidget::WidgetManifest& manifest)
{
    QString safe = manifest.id.trimmed();
    for (QChar& ch : safe) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    if (safe.isEmpty()) {
        safe = QStringLiteral("widget");
    }
    return QDir(AppPaths::userLinWidgetsDir()).filePath(safe);
}

QString safeLinWidgetInstanceFileNameForSettings(QString instanceId)
{
    for (QChar& ch : instanceId) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    return instanceId.isEmpty() ? QStringLiteral("widget") : instanceId;
}

bool settingsEnsureDefaultLinWidgetsInstalled()
{
    // Do not auto-install any default widgets on first launch.
    const QString targetRoot = AppPaths::userLinWidgetsDir();
    const QString markerPath = QDir(targetRoot).filePath(QStringLiteral(".defaults-installed"));
    if (QFileInfo::exists(markerPath)) {
        return true;
    }

    QDir().mkpath(targetRoot);
    QFile marker(markerPath);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write("1\n");
    }
    return true;
}

bool settingsRemoveLinWidgetLayoutState(const QString& instanceId, const QString& packagePath)
{
    QVector<LinWidget::WidgetLayoutState> states;
    QString errorMessage;
    if (!LinWidget::ConfigManager::loadLayout(LinWidget::ConfigManager::defaultConfigPath(), &states, &errorMessage)) {
        return false;
    }

    const QString packageKey = QDir::cleanPath(QFileInfo(packagePath).absoluteFilePath()).toLower();
    QVector<LinWidget::WidgetLayoutState> keptStates;
    keptStates.reserve(states.size());
    for (const LinWidget::WidgetLayoutState& state : states) {
        const QString statePackageKey = QDir::cleanPath(QFileInfo(state.packagePath).absoluteFilePath()).toLower();
        if (state.instanceId == instanceId || (!packageKey.isEmpty() && statePackageKey == packageKey)) {
            continue;
        }
        keptStates.append(state);
    }
    return LinWidget::ConfigManager::saveLayout(LinWidget::ConfigManager::defaultConfigPath(), keptStates, nullptr);
}

constexpr int SettingsLinWidgetPackagePathRole = Qt::UserRole;
constexpr int SettingsLinWidgetRegistryIdRole = Qt::UserRole + 1;
constexpr int SettingsLinWidgetRuntimeTypeRole = Qt::UserRole + 2;
constexpr int SettingsLinWidgetDefaultSizeRole = Qt::UserRole + 3;
constexpr int SettingsLinWidgetDevelopmentRole = Qt::UserRole + 4;
constexpr int SettingsLinWidgetNameRole = Qt::UserRole + 5;
constexpr int SettingsLinWidgetVersionRole = Qt::UserRole + 6;
constexpr int SettingsLinWidgetAuthorRole = Qt::UserRole + 7;
constexpr int SettingsLinWidgetDescriptionRole = Qt::UserRole + 8;
constexpr int SettingsLinWidgetAutoLoadRole = Qt::UserRole + 9;
constexpr int SettingsLinWidgetGrantedPermissionsRole = Qt::UserRole + 10;
constexpr int SettingsLinWidgetRequestedPermissionsRole = Qt::UserRole + 11;
constexpr int SettingsLinWidgetSourcePathRole = Qt::UserRole + 12;
constexpr int SettingsLinWidgetCategoryRole = Qt::UserRole + 13;
constexpr int SettingsLinWidgetTrustedRole = Qt::UserRole + 14;
constexpr int SettingsLinWidgetOfficialInstalledPathRole = Qt::UserRole + 15;
constexpr int SettingsLinWidgetManifestValidRole = Qt::UserRole + 16;
constexpr int SettingsLinWidgetManifestErrorRole = Qt::UserRole + 17;
constexpr int SettingsLinWidgetIconPathRole = Qt::UserRole + 18;
constexpr int SettingsLinWidgetMultiInstanceRole = Qt::UserRole + 19;

QString settingsPermissionsText(const QStringList& permissions)
{
    return permissions.isEmpty() ? QStringLiteral("none") : permissions.join(QStringLiteral(", "));
}

QString settingsPermissionDescription(const QString& permission)
{
    const QString normalized = permission.trimmed().toLower();
    if (normalized == QStringLiteral("network")) {
        return QObject::tr("Allow this widget to access external network services.");
    }
    if (normalized == QStringLiteral("storage")) {
        return QObject::tr("Allow this widget to save local browser state.");
    }
    if (normalized == QStringLiteral("desktop")) {
        return QObject::tr("Allow this widget to use future privileged desktop APIs.");
    }
    return QObject::tr("Allow this widget to use this permission.");
}

QStringList promptLinWidgetInstallPermissions(QWidget* parent, const LinWidget::WidgetManifest& manifest, bool* accepted)
{
    QStringList requestedPermissions = manifest.permissions;
    requestedPermissions.removeDuplicates();
    requestedPermissions.sort();
    if (accepted) {
        *accepted = false;
    }

    FluentDialog dialog(parent);
    dialog.setDialogTitle(QObject::tr("Install LinWidget"));
    dialog.setMessage(QObject::tr("%1 requests the following permissions. Checked permissions will be granted during installation.")
                          .arg(manifest.name.isEmpty() ? manifest.id : manifest.name));

    QVector<QPair<QString, FluentCheckBox*>> permissionChecks;
    if (requestedPermissions.isEmpty()) {
        auto* emptyLabel = new QLabel(QObject::tr("This widget does not request sensitive permissions."), &dialog);
        emptyLabel->setWordWrap(true);
        emptyLabel->setStyleSheet(QStringLiteral("color:#66738c;font-size:13px;font-weight:600;background:transparent;"));
        dialog.contentLayout()->addWidget(emptyLabel);
    } else {
        for (const QString& permission : requestedPermissions) {
            auto* row = new QWidget(&dialog);
            row->setMinimumWidth(0);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 6, 0, 6);
            rowLayout->setSpacing(10);

            auto* check = new FluentCheckBox(row);
            check->setChecked(true);
            auto* label = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#66738c;'>%2</span>")
                                         .arg(permission.toHtmlEscaped(), settingsPermissionDescription(permission).toHtmlEscaped()),
                                     row);
            label->setTextFormat(Qt::RichText);
            label->setWordWrap(true);
            label->setMinimumWidth(0);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            rowLayout->addWidget(check, 0, Qt::AlignTop);
            rowLayout->addWidget(label, 1, Qt::AlignVCenter);
            dialog.contentLayout()->addWidget(row);
            permissionChecks.append(qMakePair(permission, check));
        }
    }

    bool confirmed = false;
    dialog.addActionButton(QObject::tr("Install"), FluentDialog::ActionType::Primary, [&]() {
        confirmed = true;
    });
    dialog.addActionButton(QObject::tr("Cancel"), FluentDialog::ActionType::Secondary);
    dialog.exec();

    if (!confirmed) {
        return QStringList();
    }

    QStringList grantedPermissions;
    for (const auto& pair : permissionChecks) {
        if (pair.second && pair.second->isChecked()) {
            grantedPermissions.append(pair.first);
        }
    }
    grantedPermissions.removeDuplicates();
    grantedPermissions.sort();
    if (accepted) {
        *accepted = true;
    }
    return grantedPermissions;
}

QString settingsWidgetStateText(const QHash<QString, QString>& runningById,
                                const QHash<QString, QString>& runningByPackage,
                                const QHash<QString, int>& runningCountById,
                                const QString& registryKey,
                                const QString& packageKey,
                                bool multiInstance)
{
    if (multiInstance) {
        const int count = runningCountById.value(registryKey, 0);
        return count > 0
            ? QStringLiteral("running %1/3").arg(count)
            : QStringLiteral("stopped");
    }
    return runningById.contains(registryKey) || runningByPackage.contains(packageKey)
        ? QStringLiteral("running")
        : QStringLiteral("stopped");
}

QString settingsWidgetItemText(const QString& name,
                               const QString& id,
                               const QString& version,
                               bool isDevelopment,
                               bool autoLoad,
                               const QString& path,
                               const QString& state)
{
    return QStringLiteral("%1\nid: %2\nversion: %3\nmode: %4    state: %5    autoLoad: %6\npath: %7")
        .arg(name.isEmpty() ? id : name)
        .arg(id)
        .arg(version.isEmpty() ? QStringLiteral("unknown") : version)
        .arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"))
        .arg(state)
        .arg(autoLoad ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(path);
}
}

QWidget* SettingUI::createLinWidgetsPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    m_linWidgetsPageTitle = createPageTitle(tr("LinWidgets"), page);
    m_linWidgetsPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_linWidgetsPageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    layout->addWidget(createPageSubtitle(tr("Make your desktop more powerful and beautiful"), page), 0, Qt::AlignLeft);
    layout->addSpacing(24);

    auto* topCards = new QWidget(page);
    auto* topCardsLayout = new QHBoxLayout(topCards);
    topCardsLayout->setContentsMargins(0, 0, 0, 0);
    topCardsLayout->setSpacing(16);

    auto* runtimePanel = createPanel(topCards);
    runtimePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* runtimeLayout = new QHBoxLayout(runtimePanel);
    runtimeLayout->setContentsMargins(22, 18, 22, 18);
    runtimeLayout->setSpacing(14);
    auto* runtimeIcon = new QLabel(glyph(0xE7B8), runtimePanel);
    runtimeIcon->setAlignment(Qt::AlignCenter);
    runtimeIcon->setFixedSize(46, 46);
    runtimeIcon->setStyleSheet(QStringLiteral(
        "QLabel {"
        " color:#1771e6;"
        " background:#eef6ff;"
        " border:1px solid #cfe2ff;"
        " border-radius:12px;"
        " font-family:'Segoe Fluent Icons','Segoe MDL2 Assets';"
        " font-size:20px;"
        "}"));
    runtimeLayout->addWidget(runtimeIcon, 0, Qt::AlignVCenter);
    auto* runtimeText = new QVBoxLayout();
    runtimeText->setContentsMargins(0, 0, 0, 0);
    runtimeText->setSpacing(4);
    runtimeText->addWidget(createCardTitle(tr("Widget Runtime"), runtimePanel));
    runtimeText->addWidget(createCardSubtitle(tr("Manage LinWidget runtime environment. Closing it will release widget resources."), runtimePanel));
    runtimeLayout->addLayout(runtimeText, 1);
    m_linWidgetsRuntimeToggle = new FluentToggleSwitch(runtimePanel);
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    m_linWidgetsRuntimeToggle->setChecked(settings.value("LinWidgets/Enabled", true).toBool(), false);
    runtimeLayout->addWidget(m_linWidgetsRuntimeToggle, 0, Qt::AlignVCenter);
    topCardsLayout->addWidget(runtimePanel, 1);

    layout->addWidget(topCards);
    layout->addSpacing(14);

    auto* subNavContainer = new QWidget(page);
    subNavContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* subNavLayout = new QHBoxLayout(subNavContainer);
    subNavLayout->setContentsMargins(0, 0, 0, 0);
    subNavLayout->setSpacing(8);

    auto* segmentedBar = new QWidget(subNavContainer);
    segmentedBar->setObjectName(QStringLiteral("linWidgetSubsystemBar"));
    segmentedBar->setFixedHeight(38);
    segmentedBar->setStyleSheet(QStringLiteral(
        "QWidget#linWidgetSubsystemBar {"
        "  background: #f1f5f9;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 8px;"
        "}"));
    auto* segmentedLayout = new QHBoxLayout(segmentedBar);
    segmentedLayout->setContentsMargins(3, 3, 3, 3);
    segmentedLayout->setSpacing(4);

    m_subsystemWidgetsBtn = new QPushButton(tr("Desktop Widgets"), segmentedBar);
    m_subsystemWidgetsBtn->setCursor(Qt::PointingHandCursor);
    m_subsystemWidgetsBtn->setFixedHeight(30);

    m_subsystemWallpaperBtn = new QPushButton(tr("Live Wallpaper"), segmentedBar);
    m_subsystemWallpaperBtn->setCursor(Qt::PointingHandCursor);
    m_subsystemWallpaperBtn->setFixedHeight(30);

    auto updateSubsystemButtons = [this](int activeIndex) {
        auto styleBtn = [](QPushButton* btn, bool active) {
            btn->setStyleSheet(active ? QStringLiteral(
                "QPushButton {"
                "  background: #ffffff;"
                "  color: #0f172a;"
                "  font-weight: 700;"
                "  font-size: 13px;"
                "  border: 1px solid #cbd5e1;"
                "  border-radius: 6px;"
                "  padding: 0 16px;"
                "}"
            ) : QStringLiteral(
                "QPushButton {"
                "  background: transparent;"
                "  color: #64748b;"
                "  font-weight: 500;"
                "  font-size: 13px;"
                "  border: none;"
                "  border-radius: 6px;"
                "  padding: 0 16px;"
                "}"
                "QPushButton:hover {"
                "  background: rgba(255, 255, 255, 0.6);"
                "  color: #1e293b;"
                "}"
            ));
        };
        styleBtn(m_subsystemWidgetsBtn, activeIndex == 0);
        styleBtn(m_subsystemWallpaperBtn, activeIndex == 1);
        if (m_linWidgetSubsystemStack) {
            m_linWidgetSubsystemStack->setCurrentIndex(activeIndex);
        }
        if (activeIndex == 1) {
            refreshWallpaperSubsystemState();
        }
    };

    connect(m_subsystemWidgetsBtn, &QPushButton::clicked, this, [updateSubsystemButtons]() {
        updateSubsystemButtons(0);
    });
    connect(m_subsystemWallpaperBtn, &QPushButton::clicked, this, [updateSubsystemButtons]() {
        updateSubsystemButtons(1);
    });

    segmentedLayout->addWidget(m_subsystemWidgetsBtn);
    segmentedLayout->addWidget(m_subsystemWallpaperBtn);
    subNavLayout->addWidget(segmentedBar);
    subNavLayout->addStretch();

    layout->addWidget(subNavContainer);
    layout->addSpacing(10);

    m_linWidgetSubsystemStack = new QStackedWidget(page);
    layout->addWidget(m_linWidgetSubsystemStack, 1);

    m_linWidgetsTabs = new QTabWidget(m_linWidgetSubsystemStack);
    m_linWidgetsTabs->setDocumentMode(true);
    m_linWidgetsTabs->setObjectName(QStringLiteral("linWidgetsTabs"));
    m_linWidgetSubsystemStack->addWidget(m_linWidgetsTabs);

    m_wallpaperSubsystemPage = createWallpaperSubsystemPage(m_linWidgetSubsystemStack);
    m_linWidgetSubsystemStack->addWidget(m_wallpaperSubsystemPage);

    updateSubsystemButtons(0);

    auto createListPage = [this, page](QListWidget*& list, const QString& title, const QString& subtitle) {
        auto* tab = new QWidget(page);
        auto* tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(0, 16, 0, 0);
        tabLayout->setSpacing(0);
        auto* panel = createPanel(tab);
        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(22, 18, 22, 18);
        panelLayout->setSpacing(10);
        panelLayout->addWidget(createCardTitle(title, panel));
        panelLayout->addWidget(createCardSubtitle(subtitle, panel));
        list = new QListWidget(panel);
        list->setObjectName(QStringLiteral("linWidgetSettingsList"));
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setMinimumHeight(320);
        list->setWordWrap(true);
        list->setMouseTracking(true);
        panelLayout->addWidget(list, 1);
        tabLayout->addWidget(panel, 1);
        return tab;
    };

    m_linWidgetsTabs->addTab(createListPage(m_officialLinWidgetsList,
                                            tr("Official"),
                                            tr("Install widgets provided by LinDesk.")),
                             tr("Official"));
    m_linWidgetsTabs->addTab(createListPage(m_marketplaceLinWidgetsList,
                                            tr("Marketplace"),
                                            tr("Browse the local market source, online discovery capability will be opened later.")),
                             tr("Marketplace"));

    auto* installedTab = new QWidget(page);
    auto* installedLayout = new QGridLayout(installedTab);
    installedLayout->setContentsMargins(0, 16, 0, 0);
    installedLayout->setHorizontalSpacing(16);
    installedLayout->setVerticalSpacing(0);
    auto* installedListPanel = createPanel(installedTab);
    auto* installedListLayout = new QVBoxLayout(installedListPanel);
    installedListLayout->setContentsMargins(22, 18, 22, 18);
    installedListLayout->setSpacing(10);
    installedListLayout->addWidget(createCardTitle(tr("Installed"), installedListPanel));
    installedListLayout->addWidget(createCardSubtitle(tr("Manage widgets that have been copied to the Roaming directory."), installedListPanel));
    m_installedLinWidgetsList = new QListWidget(installedListPanel);
    m_installedLinWidgetsList->setObjectName(QStringLiteral("linWidgetSettingsList"));
    m_installedLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_installedLinWidgetsList->setMinimumHeight(340);
    m_installedLinWidgetsList->setWordWrap(true);
    m_installedLinWidgetsList->setMouseTracking(true);
    installedListLayout->addWidget(m_installedLinWidgetsList, 1);
    installedLayout->addWidget(installedListPanel, 0, 0, 1, 1);

    auto* inspectorPanel = createPanel(installedTab);
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(22, 18, 22, 18);
    inspectorLayout->setSpacing(10);
    inspectorLayout->addWidget(createCardTitle(tr("Inspector"), inspectorPanel));
    m_linWidgetsInspectorLabel = createCardSubtitle(tr("Select a widget to inspect details."), inspectorPanel);
    inspectorLayout->addWidget(m_linWidgetsInspectorLabel);
    inspectorLayout->addWidget(createDivider(inspectorPanel));
    inspectorLayout->addWidget(createCardSubtitle(tr("Permissions"), inspectorPanel));
    auto* permissionRow = new QHBoxLayout();
    permissionRow->setContentsMargins(0, 0, 0, 0);
    permissionRow->setSpacing(10);
    m_linWidgetNetworkPermissionCheck = new QCheckBox(QStringLiteral("network"), inspectorPanel);
    m_linWidgetStoragePermissionCheck = new QCheckBox(QStringLiteral("storage"), inspectorPanel);
    m_linWidgetDesktopPermissionCheck = new QCheckBox(QStringLiteral("desktop"), inspectorPanel);
    permissionRow->addWidget(m_linWidgetNetworkPermissionCheck);
    permissionRow->addWidget(m_linWidgetStoragePermissionCheck);
    permissionRow->addWidget(m_linWidgetDesktopPermissionCheck);
    permissionRow->addStretch();
    inspectorLayout->addLayout(permissionRow);
    inspectorLayout->addWidget(createDivider(inspectorPanel));
    inspectorLayout->addWidget(createCardSubtitle(tr("Actions"), inspectorPanel));
    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);
    m_linWidgetLoadButton = createSettingsActionButton(tr("Load"), inspectorPanel, true);
    m_linWidgetUnloadButton = createSettingsActionButton(tr("Unload"), inspectorPanel);
    m_linWidgetReloadButton = createSettingsActionButton(tr("Reload"), inspectorPanel);
    m_linWidgetOpenFolderButton = createSettingsActionButton(tr("Open Folder"), inspectorPanel);
    m_linWidgetDeleteButton = createSettingsActionButton(tr("Uninstall"), inspectorPanel, false, true);
    actionRow->addWidget(m_linWidgetLoadButton);
    actionRow->addWidget(m_linWidgetUnloadButton);
    actionRow->addWidget(m_linWidgetReloadButton);
    actionRow->addWidget(m_linWidgetOpenFolderButton);
    actionRow->addWidget(m_linWidgetDeleteButton);
    inspectorLayout->addLayout(actionRow);
    inspectorLayout->addWidget(createDivider(inspectorPanel));
    inspectorLayout->addWidget(createCardTitle(tr("Live Log"), inspectorPanel));
    m_linWidgetsLogView = new QPlainTextEdit(inspectorPanel);
    m_linWidgetsLogView->setReadOnly(true);
    m_linWidgetsLogView->setMinimumHeight(170);
    m_linWidgetsLogView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0f172a; color: #dbeafe; border: none; border-radius: 8px; padding: 8px; font-family: Consolas, 'Cascadia Mono', monospace; font-size: 12px; }"));
    inspectorLayout->addWidget(m_linWidgetsLogView, 1);
    inspectorLayout->addWidget(createCardTitle(tr("Recent Errors"), inspectorPanel));
    m_linWidgetsErrorsLabel = createCardSubtitle(tr("No errors."), inspectorPanel);
    inspectorLayout->addWidget(m_linWidgetsErrorsLabel);
    inspectorPanel->hide();
    installedLayout->addWidget(inspectorPanel, 0, 1, 1, 1);
    installedLayout->setColumnStretch(0, 1);
    installedLayout->setColumnStretch(1, 0);
    m_linWidgetsTabs->addTab(installedTab, tr("Installed"));

    auto* advancedTab = new QWidget(page);
    auto* advancedLayout = new QGridLayout(advancedTab);
    advancedLayout->setContentsMargins(0, 16, 0, 0);
    advancedLayout->setHorizontalSpacing(16);
    advancedLayout->setVerticalSpacing(0);
    auto* developmentListPanel = createPanel(advancedTab);
    auto* developmentListLayout = new QVBoxLayout(developmentListPanel);
    developmentListLayout->setContentsMargins(22, 18, 22, 18);
    developmentListLayout->setSpacing(10);
    developmentListLayout->addWidget(createCardTitle(tr("Development"), developmentListPanel));
    developmentListLayout->addWidget(createCardSubtitle(tr("Run widgets from source code directory and export local packages."), developmentListPanel));
    m_developmentLinWidgetsList = new QListWidget(developmentListPanel);
    m_developmentLinWidgetsList->setObjectName(QStringLiteral("linWidgetSettingsList"));
    m_developmentLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_developmentLinWidgetsList->setWordWrap(true);
    m_developmentLinWidgetsList->setMinimumHeight(340);
    m_developmentLinWidgetsList->setMouseTracking(true);
    developmentListLayout->addWidget(m_developmentLinWidgetsList, 1);
    advancedLayout->addWidget(developmentListPanel, 0, 0, 1, 1);
    auto* advancedPanel = createPanel(advancedTab);
    auto* advancedPanelLayout = new QVBoxLayout(advancedPanel);
    advancedPanelLayout->setContentsMargins(22, 18, 22, 18);
    advancedPanelLayout->setSpacing(0);
    advancedPanelLayout->addWidget(createCardTitle(tr("Advanced Tools"), advancedPanel));
    advancedPanelLayout->addWidget(createCardSubtitle(tr("Create, add, run and export development widgets."), advancedPanel));
    advancedPanelLayout->addSpacing(12);
    m_linWidgetAddDevButton = createSettingsActionButton(tr("Add Development Project"), advancedPanel);
    m_linWidgetNewProjectButton = createSettingsActionButton(tr("New Widget Project"), advancedPanel);
    m_linWidgetExportZipButton = createSettingsActionButton(tr("Export Zip Package"), advancedPanel);
    m_linWidgetRemoveDevButton = createSettingsActionButton(tr("Remove Development Record"), advancedPanel, false, true);
    auto* openLogsButton = createSettingsActionButton(tr("Open Logs Directory"), advancedPanel);
    auto* openRoamingButton = createSettingsActionButton(tr("Open Roaming Directory"), advancedPanel);
    advancedPanelLayout->addWidget(m_linWidgetAddDevButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(m_linWidgetNewProjectButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(m_linWidgetExportZipButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(m_linWidgetRemoveDevButton);
    advancedPanelLayout->addWidget(createDivider(advancedPanel));
    advancedPanelLayout->addWidget(openLogsButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(openRoamingButton);
    advancedPanelLayout->addStretch();
    advancedLayout->addWidget(advancedPanel, 0, 1, 1, 1);
    advancedLayout->setColumnStretch(0, 1);
    advancedLayout->setColumnStretch(1, 1);
    advancedTab->hide();

    auto* footerPanel = createPanel(page);
    footerPanel->setGraphicsEffect(nullptr);
    auto* footerLayout = new QHBoxLayout(footerPanel);
    footerLayout->setContentsMargins(18, 12, 18, 12);
    footerLayout->setSpacing(12);
    m_linWidgetsStatusLabel = createCardSubtitle(QString(), footerPanel);
    footerLayout->addWidget(m_linWidgetsStatusLabel, 1);
    auto* footerRefreshButton = createSettingsActionButton(tr("Refresh"), footerPanel);
    footerLayout->addWidget(footerRefreshButton, 0, Qt::AlignVCenter);
    layout->addSpacing(12);
    layout->addWidget(footerPanel);

    m_linWidgetsLogWatcher = new QFileSystemWatcher(page);
    m_linWidgetsLogReloadTimer = new QTimer(page);
    m_linWidgetsLogReloadTimer->setSingleShot(true);
    m_linWidgetsLogReloadTimer->setInterval(120);
    connect(m_linWidgetsLogWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (!path.isEmpty() && QFileInfo::exists(path) && !m_linWidgetsLogWatcher->files().contains(path)) {
            m_linWidgetsLogWatcher->addPath(path);
        }
        if (m_linWidgetsLogReloadTimer) {
            m_linWidgetsLogReloadTimer->start();
        }
    });
    connect(m_linWidgetsLogReloadTimer, &QTimer::timeout, this, &SettingUI::refreshLinWidgetSettingsInspector);

    connect(m_linWidgetsRuntimeToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(settingsConfigPath(), QSettings::IniFormat);
        s.setValue("LinWidgets/Enabled", checked);
        s.sync();
        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (bridge.isLoaded()) {
            bridge.setEnabled(checked);
            if (checked) {
                bridge.restoreFromConfig(QString());
            }
        }
        reloadLinWidgetsSettingsPage();
    });
    connect(footerRefreshButton, &QPushButton::clicked, this, &SettingUI::reloadLinWidgetsSettingsPage);
    connect(openLogsButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::linWidgetLogsDir()));
    });
    connect(openRoamingButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
    });
    connect(m_installedLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_developmentLinWidgetsList) {
            m_developmentLinWidgetsList->clearSelection();
        }
        refreshLinWidgetSettingsButtons();
        refreshLinWidgetSettingsInspector();
    });
    connect(m_developmentLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_installedLinWidgetsList) {
            m_installedLinWidgetsList->clearSelection();
        }
        refreshLinWidgetSettingsButtons();
        refreshLinWidgetSettingsInspector();
    });
    connect(m_installedLinWidgetsList, &QListWidget::itemClicked, this, &SettingUI::selectAndOpenLinWidgetSettingsItem);
    connect(m_developmentLinWidgetsList, &QListWidget::itemClicked, this, &SettingUI::selectAndOpenLinWidgetSettingsItem);
    connect(m_officialLinWidgetsList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        selectAndOpenLinWidgetSettingsItem(item);
    });
    connect(m_marketplaceLinWidgetsList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        selectAndOpenLinWidgetSettingsItem(item);
    });
    connect(m_linWidgetLoadButton, &QPushButton::clicked, this, &SettingUI::loadSelectedSettingsLinWidget);
    connect(m_linWidgetUnloadButton, &QPushButton::clicked, this, &SettingUI::unloadSelectedSettingsLinWidget);
    connect(m_linWidgetReloadButton, &QPushButton::clicked, this, &SettingUI::reloadSelectedSettingsLinWidget);
    connect(m_linWidgetOpenFolderButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedSettingsLinWidgetPackagePath();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });
    connect(m_linWidgetDeleteButton, &QPushButton::clicked, this, &SettingUI::uninstallSelectedSettingsLinWidget);
    connect(m_linWidgetAddDevButton, &QPushButton::clicked, this, &SettingUI::addDevelopmentSettingsLinWidget);
    connect(m_linWidgetNewProjectButton, &QPushButton::clicked, this, &SettingUI::createDevelopmentSettingsLinWidget);
    connect(m_linWidgetExportZipButton, &QPushButton::clicked, this, &SettingUI::exportDevelopmentSettingsLinWidget);
    connect(m_linWidgetRemoveDevButton, &QPushButton::clicked, this, [this]() {
        auto& registry = LinWidget::WidgetRegistryManager::instance();
        registry.load();
        registry.removeDevelopmentProject(selectedSettingsLinWidgetRegistryId());
        registry.save();
        reloadLinWidgetsSettingsPage();
    });
    connect(m_linWidgetNetworkPermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedSettingsLinWidgetPermission(QStringLiteral("network"), checked);
    });
    connect(m_linWidgetStoragePermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedSettingsLinWidgetPermission(QStringLiteral("storage"), checked);
    });
    connect(m_linWidgetDesktopPermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedSettingsLinWidgetPermission(QStringLiteral("desktop"), checked);
    });

    reloadLinWidgetsSettingsPage();
    return page;
}

QWidget* SettingUI::createWallpaperSubsystemPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 10, 0, 0);
    layout->setSpacing(16);

    auto* activePanel = createPanel(page);
    activePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* activeLayout = new QVBoxLayout(activePanel);
    activeLayout->setContentsMargins(22, 18, 22, 18);
    activeLayout->setSpacing(12);

    auto* activeTopRow = new QHBoxLayout();
    activeTopRow->setContentsMargins(0, 0, 0, 0);
    activeTopRow->setSpacing(14);

    m_wallpaperPreviewLabel = new QLabel(activePanel);
    m_wallpaperPreviewLabel->setAlignment(Qt::AlignCenter);
    m_wallpaperPreviewLabel->setFixedSize(52, 52);
    m_wallpaperPreviewLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #7c3aed;"
        "  background: #f5f3ff;"
        "  border: 1px solid #ddd6fe;"
        "  border-radius: 12px;"
        "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "  font-size: 24px;"
        "}"));
    m_wallpaperPreviewLabel->setText(glyph(0xEB9F));
    activeTopRow->addWidget(m_wallpaperPreviewLabel, 0, Qt::AlignVCenter);

    auto* activeTextLayout = new QVBoxLayout();
    activeTextLayout->setContentsMargins(0, 0, 0, 0);
    activeTextLayout->setSpacing(3);

    m_wallpaperActiveTitle = createCardTitle(tr("Live Wallpaper"), activePanel);
    m_wallpaperActiveDesc = createCardSubtitle(tr("No live wallpaper currently active."), activePanel);
    activeTextLayout->addWidget(m_wallpaperActiveTitle);
    activeTextLayout->addWidget(m_wallpaperActiveDesc);
    activeTopRow->addLayout(activeTextLayout, 1);

    m_wallpaperStopBtn = createSettingsActionButton(tr("Stop Wallpaper"), activePanel, false, true);
    m_wallpaperStopBtn->setEnabled(false);
    connect(m_wallpaperStopBtn, &QPushButton::clicked, this, &SettingUI::stopActiveWallpaper);
    activeTopRow->addWidget(m_wallpaperStopBtn, 0, Qt::AlignVCenter);
    activeLayout->addLayout(activeTopRow);

    layout->addWidget(activePanel, 0);

    auto* galleryPanel = createPanel(page);
    galleryPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* galleryLayout = new QVBoxLayout(galleryPanel);
    galleryLayout->setContentsMargins(22, 18, 22, 18);
    galleryLayout->setSpacing(10);

    galleryLayout->addWidget(createCardTitle(tr("Wallpaper Library"), galleryPanel));
    galleryLayout->addWidget(createCardSubtitle(tr("Select an official or imported live wallpaper to apply to your desktop."), galleryPanel));

    m_wallpaperList = new QListWidget(galleryPanel);
    m_wallpaperList->setObjectName(QStringLiteral("linWidgetSettingsList"));
    m_wallpaperList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_wallpaperList->setMinimumHeight(280);
    m_wallpaperList->setWordWrap(true);
    m_wallpaperList->setMouseTracking(true);
    galleryLayout->addWidget(m_wallpaperList, 1);

    layout->addWidget(galleryPanel, 1);
    return page;
}

void SettingUI::refreshWallpaperSubsystemState()
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool hasActive = bridge.isLoaded() && bridge.hasActiveWallpaper();
    const QString activeId = hasActive ? bridge.activeWallpaperId() : QString();
    const QString activePath = hasActive ? bridge.activeWallpaperPackagePath() : QString();

    if (m_wallpaperStopBtn) {
        m_wallpaperStopBtn->setEnabled(hasActive);
    }

    if (m_wallpaperActiveTitle && m_wallpaperActiveDesc) {
        if (hasActive) {
            LinWidget::WidgetManifest manifest;
            QString name = activeId;
            if (!activePath.isEmpty() && LinWidget::WidgetPackage::loadManifest(activePath, &manifest, nullptr)) {
                name = manifest.name.isEmpty() ? activeId : manifest.name;
            }
            m_wallpaperActiveTitle->setText(tr("%1 (Active)").arg(name));
            m_wallpaperActiveDesc->setText(tr("Live wallpaper is currently running on the background layer."));
        } else {
            m_wallpaperActiveTitle->setText(tr("Live Wallpaper (Inactive)"));
            m_wallpaperActiveDesc->setText(tr("No live wallpaper currently active. Your desktop uses Windows wallpaper."));
        }
    }
}

void SettingUI::applyWallpaperPackage(const QString& packagePath, bool isDevelopment)
{
    Q_UNUSED(isDevelopment);
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        return;
    }
    bridge.startWallpaper(packagePath);
    refreshWallpaperSubsystemState();
    reloadLinWidgetsSettingsPage();
}

void SettingUI::stopActiveWallpaper()
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        bridge.stopWallpaper(true);
    }
    refreshWallpaperSubsystemState();
    reloadLinWidgetsSettingsPage();
}

void SettingUI::reloadLinWidgetsSettingsPage()
{
    if (!m_officialLinWidgetsList || !m_marketplaceLinWidgetsList || !m_installedLinWidgetsList || !m_developmentLinWidgetsList) {
        return;
    }

    const QString selectedKey = selectedSettingsLinWidgetRegistryKey();
    settingsEnsureDefaultLinWidgetsInstalled();
    if (!m_preserveLinWidgetDrawerOnReload) {
        closeDrawer();
    }

    m_officialLinWidgetsList->clear();
    m_marketplaceLinWidgetsList->clear();
    m_installedLinWidgetsList->clear();
    m_developmentLinWidgetsList->clear();
    if (m_wallpaperList) {
        m_wallpaperList->clear();
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString registryError;
    registry.load(&registryError);
    registry.syncOfficialWidgetsIfOutdated(runtimeLinWidgetsDirForSettings(), AppPaths::userLinWidgetsDir());
    registry.syncFromFolder(AppPaths::userLinWidgetsDir(), &registryError);
    registry.save(&registryError);

    refreshLinWidgetSettingsRunningMaps();

    QHash<QString, LinWidget::WidgetRegistryEntry> installedEntriesById;
    int installedCount = 0;
    int developmentCount = 0;
    for (const LinWidget::WidgetRegistryEntry& entry : registry.entries()) {
        if (!entry.enabled || !QFileInfo::exists(entry.packagePath)) {
            continue;
        }

        if (!entry.isDevelopment) {
            installedEntriesById.insert(entry.id.trimmed().toLower(), entry);
        }

        LinWidget::WidgetManifest manifest;
        QString manifestError;
        const bool manifestOk = LinWidget::WidgetPackage::loadManifest(entry.packagePath, &manifest, &manifestError);
        const QString iconPath = manifestOk
            ? settingsResolveWidgetIconPath(entry.packagePath, manifest.icon)
            : settingsResolveWidgetIconPath(entry.packagePath, entry.icon);
        const QString registryKey = settingsLinWidgetRegistryKey(entry.id, entry.isDevelopment);
        const QString packageKey = settingsLinWidgetPackageKey(entry.packagePath);
        const QString state = settingsWidgetStateText(m_linWidgetInstancesById,
                                                      m_linWidgetInstancesByPackage,
                                                      m_linWidgetInstanceCountsById,
                                                      registryKey,
                                                      packageKey,
                                                      manifestOk && manifest.multiInstance);

        auto* item = new QListWidgetItem();
        item->setData(SettingsLinWidgetPackagePathRole, entry.packagePath);
        item->setData(SettingsLinWidgetRegistryIdRole, entry.id);
        item->setData(SettingsLinWidgetRuntimeTypeRole, entry.runtimeType);
        item->setData(SettingsLinWidgetDefaultSizeRole, entry.defaultSize);
        item->setData(SettingsLinWidgetDevelopmentRole, entry.isDevelopment);
        item->setData(SettingsLinWidgetNameRole, entry.name);
        item->setData(SettingsLinWidgetVersionRole, entry.version);
        item->setData(SettingsLinWidgetAuthorRole, entry.author);
        item->setData(SettingsLinWidgetDescriptionRole, entry.description);
        item->setData(SettingsLinWidgetAutoLoadRole, entry.autoLoad);
        item->setData(SettingsLinWidgetGrantedPermissionsRole, entry.permissionsGranted);
        item->setData(SettingsLinWidgetRequestedPermissionsRole, manifest.permissions);
        item->setData(SettingsLinWidgetSourcePathRole, entry.sourcePath);
        item->setData(SettingsLinWidgetManifestValidRole, manifestOk);
        item->setData(SettingsLinWidgetManifestErrorRole, manifestError);
        item->setData(SettingsLinWidgetIconPathRole, iconPath);
        item->setData(SettingsLinWidgetMultiInstanceRole, manifestOk && manifest.multiInstance);
        const QString displayText = manifestOk
            ? settingsWidgetItemText(entry.name,
                                     entry.id,
                                     entry.version,
                                     entry.isDevelopment,
                                     entry.autoLoad,
                                     entry.packagePath,
                                     state)
            : tr("%1\nManifest error: %2\n%3").arg(entry.name.isEmpty() ? entry.id : entry.name, manifestError, entry.packagePath);
        item->setText(QString());
        item->setToolTip(displayText);

        const bool isWallpaper = (entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) ||
                                 (manifestOk && manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0);
        if (isWallpaper) {
            // Avoid letting stale AppData copy shadow the official bundled wallpaper gallery item
            if (entry.id.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0 &&
                !entry.isDevelopment &&
                !entry.packagePath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
                continue;
            }
            if (m_wallpaperList) {
                m_wallpaperList->addItem(item);
                auto* row = createSettingsLinWidgetRow(m_wallpaperList,
                                                       item,
                                                       entry.id,
                                                       entry.name,
                                                       entry.version,
                                                       entry.description,
                                                       iconPath,
                                                       entry.isDevelopment ? tr("Development") : tr("Installed"),
                                                       m_wallpaperList,
                                                       nullptr);
                auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
                auto& bridge = LinWidgetRuntimeBridge::instance();
                const bool isCurrentlyActive = bridge.isLoaded() && bridge.hasActiveWallpaper() &&
                    (bridge.activeWallpaperId().compare(entry.id, Qt::CaseInsensitive) == 0 ||
                     bridge.activeWallpaperId().compare(QStringLiteral("wallpaper:") + entry.id, Qt::CaseInsensitive) == 0);

                auto* applyButton = createSettingsActionButton(isCurrentlyActive ? tr("Active") : tr("Apply Wallpaper"), row, !isCurrentlyActive);
                applyButton->setEnabled(!isCurrentlyActive);
                rowLayout->addWidget(applyButton);
                connect(applyButton, &QPushButton::clicked, this, [this, path = entry.packagePath, isDev = entry.isDevelopment]() {
                    applyWallpaperPackage(path, isDev);
                });

                if (!entry.packagePath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
                    auto* uninstallButton = createSettingsActionButton(tr("Uninstall"), row, false, true);
                    rowLayout->addWidget(uninstallButton);
                    connect(uninstallButton, &QPushButton::clicked, this, [this, id = entry.id, path = entry.packagePath]() {
                        auto& bridge = LinWidgetRuntimeBridge::instance();
                        if (bridge.isLoaded() && bridge.hasActiveWallpaper()) {
                            const QString activeId = bridge.activeWallpaperId();
                            if (activeId.compare(id, Qt::CaseInsensitive) == 0 ||
                                activeId.compare(QStringLiteral("wallpaper:") + id, Qt::CaseInsensitive) == 0) {
                                bridge.stopWallpaper(true);
                            }
                        }
                        auto& reg = LinWidget::WidgetRegistryManager::instance();
                        reg.load();
                        reg.removeById(id);
                        reg.save();
                        QString error;
                        settingsRemoveDirectory(path, &error);
                        reloadLinWidgetsSettingsPage();
                    });
                }

                item->setSizeHint(QSize(0, 88));
                m_wallpaperList->setItemWidget(item, row);
            }
            continue;
        }

        if (entry.isDevelopment) {
            m_developmentLinWidgetsList->addItem(item);
            auto* row = createSettingsLinWidgetRow(m_developmentLinWidgetsList,
                                                   item,
                                                   entry.id,
                                                   entry.name,
                                                   entry.version,
                                                   entry.description,
                                                   iconPath,
                                                   tr("Development"),
                                                   m_developmentLinWidgetsList,
                                                   [this](QListWidgetItem* clickedItem) {
                                                       selectAndOpenLinWidgetSettingsItem(clickedItem);
                                                   });
            item->setSizeHint(QSize(0, 88));
            m_developmentLinWidgetsList->setItemWidget(item, row);
            ++developmentCount;
            if (!selectedKey.isEmpty() && selectedKey == registryKey) {
                m_developmentLinWidgetsList->setCurrentItem(item);
            }
        } else {
            m_installedLinWidgetsList->addItem(item);
            auto* row = createSettingsLinWidgetRow(m_installedLinWidgetsList,
                                                   item,
                                                   entry.id,
                                                   entry.name,
                                                   entry.version,
                                                   entry.description,
                                                   iconPath,
                                                   tr("Installed"),
                                                   m_installedLinWidgetsList,
                                                   [this](QListWidgetItem* clickedItem) {
                                                       selectAndOpenLinWidgetSettingsItem(clickedItem);
                                                   });
            auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
            const int instanceCount = m_linWidgetInstanceCountsById.value(registryKey, 0);
            const bool rowRunning = state == QStringLiteral("running");
            const QString closeButtonText = rowRunning ? tr("Close") : tr("Load");
            auto* closeButton = createSettingsActionButton(closeButtonText, row);
            auto* uninstallButton = createSettingsActionButton(tr("Uninstall"), row, false, true);
            closeButton->setEnabled(manifestOk &&
                                    (!m_linWidgetsRuntimeToggle || m_linWidgetsRuntimeToggle->checked()) &&
                                    (manifest.multiInstance ? instanceCount < 3 : true));
            uninstallButton->setEnabled(true);
            rowLayout->addWidget(closeButton);
            rowLayout->addWidget(uninstallButton);
            connect(closeButton, &QPushButton::clicked, this, [this, item, rowRunning]() {
                if (m_installedLinWidgetsList) {
                    m_installedLinWidgetsList->setCurrentItem(item);
                }
                if (rowRunning) {
                    unloadSelectedSettingsLinWidget();
                } else {
                    loadSelectedSettingsLinWidget();
                }
            });
            connect(uninstallButton, &QPushButton::clicked, this, [this, item]() {
                if (m_installedLinWidgetsList) {
                    m_installedLinWidgetsList->setCurrentItem(item);
                }
                uninstallSelectedSettingsLinWidget();
            });
            item->setSizeHint(QSize(0, 88));
            m_installedLinWidgetsList->setItemWidget(item, row);
            ++installedCount;
            if (!selectedKey.isEmpty() && selectedKey == registryKey) {
                m_installedLinWidgetsList->setCurrentItem(item);
            }
        }
    }

    const QDir officialDir(runtimeLinWidgetsDirForSettings());
    QFileInfoList officialEntries = officialDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    std::stable_sort(officialEntries.begin(), officialEntries.end(), [&installedEntriesById](const QFileInfo& left, const QFileInfo& right) {
        LinWidget::WidgetManifest leftManifest;
        LinWidget::WidgetManifest rightManifest;
        const bool leftOk = LinWidget::WidgetPackage::loadManifest(left.absoluteFilePath(), &leftManifest, nullptr);
        const bool rightOk = LinWidget::WidgetPackage::loadManifest(right.absoluteFilePath(), &rightManifest, nullptr);
        const bool leftInstalled = leftOk && installedEntriesById.contains(leftManifest.id.trimmed().toLower());
        const bool rightInstalled = rightOk && installedEntriesById.contains(rightManifest.id.trimmed().toLower());
        if (leftInstalled != rightInstalled) {
            return leftInstalled;
        }
        const QString leftName = leftOk && !leftManifest.name.trimmed().isEmpty() ? leftManifest.name : left.fileName();
        const QString rightName = rightOk && !rightManifest.name.trimmed().isEmpty() ? rightManifest.name : right.fileName();
        return QString::localeAwareCompare(leftName, rightName) < 0;
    });
    for (const QFileInfo& entry : officialEntries) {
        LinWidget::WidgetManifest manifest;
        QString errorMessage;
        if (!LinWidget::WidgetPackage::loadManifest(entry.absoluteFilePath(), &manifest, &errorMessage)) {
            continue;
        }
        if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
            if (m_wallpaperList) {
                bool alreadyInWallpaperList = false;
                for (int i = 0; i < m_wallpaperList->count(); ++i) {
                    if (m_wallpaperList->item(i)->data(SettingsLinWidgetRegistryIdRole).toString().compare(manifest.id, Qt::CaseInsensitive) == 0) {
                        alreadyInWallpaperList = true;
                        break;
                    }
                }
                if (!alreadyInWallpaperList) {
                    const QString wallIconPath = settingsResolveWidgetIconPath(entry.absoluteFilePath(), manifest.icon);
                    auto* wallItem = new QListWidgetItem();
                    wallItem->setData(SettingsLinWidgetPackagePathRole, entry.absoluteFilePath());
                    wallItem->setData(SettingsLinWidgetRegistryIdRole, manifest.id);
                    wallItem->setData(SettingsLinWidgetNameRole, manifest.name);
                    wallItem->setData(SettingsLinWidgetVersionRole, manifest.version);
                    wallItem->setData(SettingsLinWidgetAuthorRole, manifest.author);
                    wallItem->setData(SettingsLinWidgetDescriptionRole, manifest.description);
                    wallItem->setData(SettingsLinWidgetIconPathRole, wallIconPath);
                    m_wallpaperList->addItem(wallItem);
                    auto* row = createSettingsLinWidgetRow(m_wallpaperList,
                                                           wallItem,
                                                           manifest.id,
                                                           manifest.name,
                                                           manifest.version,
                                                           manifest.description,
                                                           wallIconPath,
                                                           tr("Official"),
                                                           m_wallpaperList,
                                                           nullptr);
                    auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
                    auto& bridge = LinWidgetRuntimeBridge::instance();
                    const bool isCurrentlyActive = bridge.isLoaded() && bridge.hasActiveWallpaper() &&
                        (bridge.activeWallpaperId().compare(manifest.id, Qt::CaseInsensitive) == 0 ||
                         bridge.activeWallpaperId().compare(QStringLiteral("wallpaper:") + manifest.id, Qt::CaseInsensitive) == 0);

                    auto* applyButton = createSettingsActionButton(isCurrentlyActive ? tr("Active") : tr("Apply Wallpaper"), row, !isCurrentlyActive);
                    applyButton->setEnabled(!isCurrentlyActive);
                    rowLayout->addWidget(applyButton);
                    connect(applyButton, &QPushButton::clicked, this, [this, path = entry.absoluteFilePath()]() {
                        applyWallpaperPackage(path, false);
                    });
                    wallItem->setSizeHint(QSize(0, 88));
                    m_wallpaperList->setItemWidget(wallItem, row);
                }
            }
            continue;
        }
        const QString iconPath = settingsResolveWidgetIconPath(entry.absoluteFilePath(), manifest.icon);
        const LinWidget::WidgetRegistryEntry installed = installedEntriesById.value(manifest.id.trimmed().toLower());
        const bool isInstalled = !installed.id.isEmpty() && QFileInfo::exists(installed.packagePath);
        const QString installedKey = settingsLinWidgetRegistryKey(manifest.id, false);
        const QString packageKey = isInstalled ? settingsLinWidgetPackageKey(installed.packagePath) : QString();
        const QString state = isInstalled
            ? settingsWidgetStateText(m_linWidgetInstancesById,
                                      m_linWidgetInstancesByPackage,
                                      m_linWidgetInstanceCountsById,
                                      installedKey,
                                      packageKey,
                                      manifest.multiInstance)
            : QStringLiteral("not installed");

        auto* item = new QListWidgetItem();
        item->setData(SettingsLinWidgetPackagePathRole, entry.absoluteFilePath());
        item->setData(SettingsLinWidgetRegistryIdRole, manifest.id);
        item->setData(SettingsLinWidgetRuntimeTypeRole, LinWidget::runtimeTypeToString(manifest.type));
        item->setData(SettingsLinWidgetDefaultSizeRole, QSize(manifest.width, manifest.height));
        item->setData(SettingsLinWidgetDevelopmentRole, false);
        item->setData(SettingsLinWidgetNameRole, manifest.name);
        item->setData(SettingsLinWidgetVersionRole, manifest.version);
        item->setData(SettingsLinWidgetAuthorRole, manifest.author);
        item->setData(SettingsLinWidgetDescriptionRole, manifest.description);
        item->setData(SettingsLinWidgetAutoLoadRole, manifest.autoLoad);
        item->setData(SettingsLinWidgetRequestedPermissionsRole, manifest.permissions);
        item->setData(SettingsLinWidgetOfficialInstalledPathRole, installed.packagePath);
        item->setData(SettingsLinWidgetIconPathRole, iconPath);
        item->setData(SettingsLinWidgetMultiInstanceRole, manifest.multiInstance);
        const QString officialDisplayText = QStringLiteral("%1\nid: %2\nversion: %3    author: %4\npermissions: %5\nstate: %6\n%7")
            .arg(manifest.name)
            .arg(manifest.id)
            .arg(manifest.version.isEmpty() ? QStringLiteral("unknown") : manifest.version)
            .arg(manifest.author.isEmpty() ? QStringLiteral("Unknown") : manifest.author)
            .arg(settingsPermissionsText(manifest.permissions))
            .arg(state)
            .arg(manifest.description.isEmpty() ? entry.absoluteFilePath() : manifest.description);
        item->setText(QString());
        item->setToolTip(officialDisplayText);
        m_officialLinWidgetsList->addItem(item);

        auto* row = createSettingsLinWidgetRow(m_officialLinWidgetsList,
                                               item,
                                               manifest.id,
                                               manifest.name,
                                               manifest.version,
                                               manifest.description,
                                               iconPath,
                                               tr("Official"),
                                               m_officialLinWidgetsList,
                                               [this](QListWidgetItem* clickedItem) {
                                                   selectAndOpenLinWidgetSettingsItem(clickedItem);
                                               });
        auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
        if (!isInstalled) {
            auto* installButton = createSettingsActionButton(tr("Install"), row, true);
            rowLayout->addWidget(installButton);
            connect(installButton, &QPushButton::clicked, this, [this, path = entry.absoluteFilePath()]() {
                installOfficialSettingsLinWidget(path);
            });
        } else {
            auto* uninstallButton = createSettingsActionButton(tr("Uninstall"), row, false, true);
            uninstallButton->setEnabled(true);
            rowLayout->addWidget(uninstallButton);
            connect(uninstallButton, &QPushButton::clicked, this, [this, id = manifest.id]() {
                m_linWidgetsTabs->setCurrentIndex(2);
                for (int i = 0; i < m_installedLinWidgetsList->count(); ++i) {
                    QListWidgetItem* installedItem = m_installedLinWidgetsList->item(i);
                    if (installedItem && installedItem->data(SettingsLinWidgetRegistryIdRole).toString().compare(id, Qt::CaseInsensitive) == 0) {
                        m_installedLinWidgetsList->setCurrentItem(installedItem);
                        break;
                    }
                }
                uninstallSelectedSettingsLinWidget();
            });
        }
        item->setSizeHint(QSize(0, 88));
        m_officialLinWidgetsList->setItemWidget(item, row);
    }

    QFile marketplaceFile(marketplaceFilePathForSettings());
    if (!marketplaceFile.exists() || !marketplaceFile.open(QIODevice::ReadOnly)) {
        auto* item = new QListWidgetItem(tr("Marketplace is coming soon."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        m_marketplaceLinWidgetsList->addItem(item);
    } else {
        const QJsonDocument document = QJsonDocument::fromJson(marketplaceFile.readAll());
        const QJsonArray widgets = document.isArray()
            ? document.array()
            : document.object().value(QStringLiteral("widgets")).toArray();
        if (widgets.isEmpty()) {
            auto* item = new QListWidgetItem(tr("Marketplace is coming soon."));
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            m_marketplaceLinWidgetsList->addItem(item);
        }
        for (const QJsonValue& value : widgets) {
            const QJsonObject object = value.toObject();
            const QString id = object.value(QStringLiteral("id")).toString();
            const QString sourcePath = object.value(QStringLiteral("sourcePath")).toString();
            const QString marketplaceIconPath = object.value(QStringLiteral("icon")).toString();
            QStringList permissions;
            const QJsonArray permissionsArray = object.value(QStringLiteral("permissions")).toArray();
            for (const QJsonValue& permissionValue : permissionsArray) {
                const QString permission = permissionValue.toString().trimmed();
                if (!permission.isEmpty()) {
                    permissions.append(permission);
                }
            }

            auto* item = new QListWidgetItem();
            item->setData(SettingsLinWidgetPackagePathRole, sourcePath);
            item->setData(SettingsLinWidgetRegistryIdRole, id);
            item->setData(SettingsLinWidgetNameRole, object.value(QStringLiteral("name")).toString(id));
            item->setData(SettingsLinWidgetVersionRole, object.value(QStringLiteral("version")).toString());
            item->setData(SettingsLinWidgetAuthorRole, object.value(QStringLiteral("author")).toString());
            item->setData(SettingsLinWidgetDescriptionRole, object.value(QStringLiteral("description")).toString());
            item->setData(SettingsLinWidgetRequestedPermissionsRole, permissions);
            item->setData(SettingsLinWidgetCategoryRole, object.value(QStringLiteral("category")).toString());
            item->setData(SettingsLinWidgetTrustedRole, object.value(QStringLiteral("trusted")).toBool());
            item->setData(SettingsLinWidgetIconPathRole, settingsResolveWidgetIconPath(sourcePath, marketplaceIconPath));
            const QString marketplaceDisplayText = QStringLiteral("%1\nid: %2\nversion: %3    category: %4    trusted: %5\npermissions: %6\n%7")
                .arg(item->data(SettingsLinWidgetNameRole).toString())
                .arg(id)
                .arg(item->data(SettingsLinWidgetVersionRole).toString().isEmpty() ? QStringLiteral("unknown") : item->data(SettingsLinWidgetVersionRole).toString())
                .arg(item->data(SettingsLinWidgetCategoryRole).toString().isEmpty() ? QStringLiteral("General") : item->data(SettingsLinWidgetCategoryRole).toString())
                .arg(item->data(SettingsLinWidgetTrustedRole).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(settingsPermissionsText(permissions))
                .arg(item->data(SettingsLinWidgetDescriptionRole).toString().isEmpty() ? sourcePath : item->data(SettingsLinWidgetDescriptionRole).toString());
            item->setText(QString());
            item->setToolTip(marketplaceDisplayText);
            m_marketplaceLinWidgetsList->addItem(item);

            auto* row = createSettingsLinWidgetRow(m_marketplaceLinWidgetsList,
                                                   item,
                                                   id,
                                                   item->data(SettingsLinWidgetNameRole).toString(),
                                                   item->data(SettingsLinWidgetVersionRole).toString(),
                                                   item->data(SettingsLinWidgetDescriptionRole).toString(),
                                                   item->data(SettingsLinWidgetIconPathRole).toString(),
                                                   tr("Marketplace"),
                                                   m_marketplaceLinWidgetsList,
                                                   [this](QListWidgetItem* clickedItem) {
                                                       selectAndOpenLinWidgetSettingsItem(clickedItem);
                                                   });
            auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
            auto* installButton = createSettingsActionButton(tr("Install"), row, true);
            installButton->setEnabled(QFileInfo(sourcePath).isDir());
            rowLayout->addWidget(installButton);
            connect(installButton, &QPushButton::clicked, this, [this, sourcePath]() {
                LinWidget::WidgetManifest manifest;
                QString errorMessage;
                if (!LinWidget::WidgetPackage::loadManifest(sourcePath, &manifest, &errorMessage)) {
                    if (m_linWidgetsStatusLabel) {
                        m_linWidgetsStatusLabel->setText(tr("Marketplace install failed: %1").arg(errorMessage));
                    }
                    return;
                }
                bool accepted = false;
                const QStringList grantedPermissions = promptLinWidgetInstallPermissions(this, manifest, &accepted);
                if (!accepted) {
                    if (m_linWidgetsStatusLabel) {
                        m_linWidgetsStatusLabel->setText(tr("Marketplace install canceled: %1").arg(manifest.name));
                    }
                    return;
                }
                const QString targetPath = settingsInstalledTargetForManifest(manifest);
                if (!settingsRemoveDirectory(targetPath, &errorMessage) ||
                    !settingsCopyLinWidgetDirectory(sourcePath, targetPath, &errorMessage)) {
                    if (m_linWidgetsStatusLabel) {
                        m_linWidgetsStatusLabel->setText(tr("Marketplace install failed: %1").arg(errorMessage));
                    }
                    return;
                }
                auto& reg = LinWidget::WidgetRegistryManager::instance();
                reg.load();
                reg.upsertFromPackage(targetPath, &errorMessage);
                reg.setPermissionsGranted(manifest.id, false, grantedPermissions);
                reg.save();
                QString instanceId;
                QString launchError;
                const bool launched = launchInstalledSettingsLinWidget(targetPath, manifest, &instanceId, &launchError);
                if (m_linWidgetsStatusLabel) {
                    m_linWidgetsStatusLabel->setText(launched
                        ? tr("Installed and loaded marketplace widget: %1").arg(manifest.name)
                        : tr("Installed marketplace widget: %1\nLoad skipped: %2").arg(manifest.name, launchError));
                }
                reloadLinWidgetsSettingsPage();
            });
            item->setSizeHint(QSize(0, 88));
            m_marketplaceLinWidgetsList->setItemWidget(item, row);
        }
    }

    if (m_linWidgetsStatusLabel) {
        const QString summary = tr("Installed %1    Development %2")
            .arg(installedCount)
            .arg(developmentCount);
        m_linWidgetsStatusLabel->setText(registryError.isEmpty()
            ? summary
            : tr("%1\nManifest errors:\n%2").arg(summary, registryError));
    }
    refreshLinWidgetSettingsButtons();
    refreshLinWidgetSettingsInspector();
}

void SettingUI::refreshLinWidgetSettingsButtons()
{
    refreshLinWidgetSettingsRunningMaps();
    refreshWallpaperSubsystemState();
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    const bool hasSelection = !packagePath.isEmpty();
    const bool running = !selectedSettingsLinWidgetInstanceId().isEmpty();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    const bool runtimeEnabled = !m_linWidgetsRuntimeToggle || m_linWidgetsRuntimeToggle->checked();
    QListWidget* activeList = activeSettingsLinWidgetList();
    QListWidgetItem* activeItem = activeList ? activeList->currentItem() : nullptr;
    const bool manifestValid = !activeItem || activeItem->data(SettingsLinWidgetManifestValidRole).toBool();
    const bool multiInstance = activeItem && activeItem->data(SettingsLinWidgetMultiInstanceRole).toBool();
    const int instanceCount = m_linWidgetInstanceCountsById.value(selectedSettingsLinWidgetRegistryKey(), 0);

    if (m_linWidgetLoadButton) {
        m_linWidgetLoadButton->setText(multiInstance ? tr("Add") : tr("Load"));
        m_linWidgetLoadButton->setEnabled(hasSelection &&
                                          runtimeEnabled &&
                                          manifestValid &&
                                          ((multiInstance && instanceCount < 3) || (!multiInstance && !running)));
    }
    if (m_linWidgetUnloadButton) {
        m_linWidgetUnloadButton->setEnabled(hasSelection && running && runtimeEnabled);
        m_linWidgetUnloadButton->setText(multiInstance ? tr("Close One") : tr("Close"));
    }
    if (m_linWidgetReloadButton) {
        m_linWidgetReloadButton->setEnabled(hasSelection && running && runtimeEnabled);
    }
    if (m_linWidgetOpenFolderButton) {
        m_linWidgetOpenFolderButton->setEnabled(hasSelection);
    }
    if (m_linWidgetDeleteButton) {
        m_linWidgetDeleteButton->setEnabled(hasSelection && !isDevelopment);
    }
    if (m_linWidgetExportZipButton) {
        m_linWidgetExportZipButton->setEnabled(hasSelection && isDevelopment && !running);
    }
    if (m_linWidgetRemoveDevButton) {
        m_linWidgetRemoveDevButton->setEnabled(hasSelection && isDevelopment && !running);
    }

    const QStringList granted = activeItem
        ? activeItem->data(SettingsLinWidgetGrantedPermissionsRole).toStringList()
        : QStringList();
    auto updatePermissionCheck = [&](QCheckBox* check, const QString& permission) {
        if (!check) {
            return;
        }
        const QSignalBlocker blocker(check);
        check->setChecked(isDevelopment || granted.contains(permission));
        check->setEnabled(hasSelection && !isDevelopment);
    };
    updatePermissionCheck(m_linWidgetNetworkPermissionCheck, QStringLiteral("network"));
    updatePermissionCheck(m_linWidgetStoragePermissionCheck, QStringLiteral("storage"));
    updatePermissionCheck(m_linWidgetDesktopPermissionCheck, QStringLiteral("desktop"));
}

void SettingUI::refreshLinWidgetSettingsInspector()
{
    QListWidget* list = activeSettingsLinWidgetList();
    QListWidgetItem* item = list ? list->currentItem() : nullptr;
    if (!item) {
        if (m_linWidgetsInspectorLabel) {
            m_linWidgetsInspectorLabel->setText(tr("Select an installed widget or development project."));
        }
        if (m_linWidgetsLogView) {
            m_linWidgetsLogView->setPlainText(tr("Select a widget to view its log."));
        }
        if (m_linWidgetsErrorsLabel) {
            m_linWidgetsErrorsLabel->setText(tr("No errors."));
        }
        return;
    }

    const QString id = item->data(SettingsLinWidgetRegistryIdRole).toString();
    const QString name = item->data(SettingsLinWidgetNameRole).toString();
    const QString version = item->data(SettingsLinWidgetVersionRole).toString();
    const QString author = item->data(SettingsLinWidgetAuthorRole).toString();
    const QString description = item->data(SettingsLinWidgetDescriptionRole).toString();
    const QString packagePath = item->data(SettingsLinWidgetPackagePathRole).toString();
    const QString iconPath = item->data(SettingsLinWidgetIconPathRole).toString();
    const bool isDevelopment = item->data(SettingsLinWidgetDevelopmentRole).toBool();
    const QStringList requested = item->data(SettingsLinWidgetRequestedPermissionsRole).toStringList();
    const QStringList granted = item->data(SettingsLinWidgetGrantedPermissionsRole).toStringList();
    QStringList missing;
    for (const QString& permission : requested) {
        if (!granted.contains(permission)) {
            missing.append(permission);
        }
    }
    const QString instanceId = selectedSettingsLinWidgetInstanceId();
    const QString logInstanceId = instanceId.isEmpty()
        ? settingsLinWidgetRegistryKey(id, isDevelopment)
        : instanceId;

    if (m_linWidgetsInspectorLabel) {
        m_linWidgetsInspectorLabel->setText(QStringLiteral(
            "name: %1\n"
            "id: %2\n"
            "version: %3\n"
            "mode: %4\n"
            "state: %5\n"
            "autoLoad: %6\n"
            "requested permissions: %7\n"
            "granted permissions: %8\n"
            "missing permissions: %9\n"
            "author: %10\n"
            "description: %11\n"
            "path: %12")
            .arg(name.isEmpty() ? id : name)
            .arg(id)
            .arg(version.isEmpty() ? QStringLiteral("unknown") : version)
            .arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"))
            .arg(instanceId.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("running"))
            .arg(item->data(SettingsLinWidgetAutoLoadRole).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(settingsPermissionsText(requested))
            .arg(isDevelopment ? QStringLiteral("network, storage, desktop") : settingsPermissionsText(granted))
            .arg(isDevelopment || missing.isEmpty() ? QStringLiteral("none") : missing.join(QStringLiteral(", ")))
            .arg(author.isEmpty() ? QStringLiteral("Unknown") : author)
            .arg(description.isEmpty() ? QStringLiteral("No description") : description)
            .arg(packagePath));
    }

    const QString logPath = settingsLinWidgetLogPath(logInstanceId);
    if (m_linWidgetsLogWatcher && m_currentLinWidgetLogPath != logPath) {
        const QStringList watchedFiles = m_linWidgetsLogWatcher->files();
        if (!watchedFiles.isEmpty()) {
            m_linWidgetsLogWatcher->removePaths(watchedFiles);
        }
        m_currentLinWidgetLogPath = logPath;
        if (!logPath.isEmpty() && QFileInfo::exists(logPath)) {
            m_linWidgetsLogWatcher->addPath(logPath);
        }
    }

    QString logText;
    if (logPath.isEmpty()) {
        logText = tr("No log instance.");
    } else {
        QFile file(logPath);
        if (!file.exists()) {
            const QString sharedLogPath = settingsLinWidgetLogPath(QStringLiteral("__shared__"));
            if (QFileInfo::exists(sharedLogPath)) {
                file.setFileName(sharedLogPath);
            } else {
                logText = tr("Log file has not been created yet:\n%1").arg(logPath);
            }
        }
        if (logText.isEmpty()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                logText = tr("Cannot open log file:\n%1").arg(file.fileName());
            } else {
                QByteArray content = file.readAll();
                constexpr int maxLogBytes = 200 * 1024;
                if (content.size() > maxLogBytes) {
                    content = content.right(maxLogBytes);
                }
                logText = QString::fromUtf8(content);
            }
        }
    }
    if (m_linWidgetsLogView) {
        m_linWidgetsLogView->setPlainText(logText);
        QTextCursor cursor = m_linWidgetsLogView->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_linWidgetsLogView->setTextCursor(cursor);
    }

    QStringList errorLines;
    const QStringList lines = logText.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString lower = line.toLower();
        if (lower.contains(QStringLiteral("permission denied")) ||
            lower.contains(QStringLiteral("error")) ||
            lower.contains(QStringLiteral("failed")) ||
            lower.contains(QStringLiteral("js.rejection"))) {
            errorLines.append(line);
        }
    }
    if (m_linWidgetsErrorsLabel) {
        if (errorLines.isEmpty()) {
            m_linWidgetsErrorsLabel->setText(tr("No errors."));
        } else {
            while (errorLines.size() > 8) {
                errorLines.removeFirst();
            }
            m_linWidgetsErrorsLabel->setText(errorLines.join(QLatin1Char('\n')));
        }
    }
}

void SettingUI::openLinWidgetSettingsDrawer(QListWidgetItem* item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }

    QListWidget* ownerList = item->listWidget();
    const bool isOfficial = ownerList == m_officialLinWidgetsList;
    const bool isMarketplace = ownerList == m_marketplaceLinWidgetsList;
    const QString id = item->data(SettingsLinWidgetRegistryIdRole).toString();
    const QString name = item->data(SettingsLinWidgetNameRole).toString();
    const QString version = item->data(SettingsLinWidgetVersionRole).toString();
    const QString author = item->data(SettingsLinWidgetAuthorRole).toString();
    const QString description = item->data(SettingsLinWidgetDescriptionRole).toString();
    const QString packagePath = item->data(SettingsLinWidgetPackagePathRole).toString();
    const QString iconPath = item->data(SettingsLinWidgetIconPathRole).toString();
    const bool isDevelopment = item->data(SettingsLinWidgetDevelopmentRole).toBool();
    const QStringList requested = item->data(SettingsLinWidgetRequestedPermissionsRole).toStringList();
    const QStringList granted = item->data(SettingsLinWidgetGrantedPermissionsRole).toStringList();
    const bool multiInstance = item->data(SettingsLinWidgetMultiInstanceRole).toBool();
    const QString registryKey = settingsLinWidgetRegistryKey(id, isDevelopment);
    const QString packageKey = settingsLinWidgetPackageKey(isOfficial ? item->data(SettingsLinWidgetOfficialInstalledPathRole).toString() : packagePath);
    const QString instanceId = m_linWidgetInstancesById.value(registryKey, m_linWidgetInstancesByPackage.value(packageKey));
    const bool running = !instanceId.isEmpty();
    const bool installed = !item->data(SettingsLinWidgetOfficialInstalledPathRole).toString().isEmpty() || (!isOfficial && !isMarketplace);

    auto* content = new QWidget();
    content->setMinimumWidth(0);
    content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* hero = new QWidget(content);
    hero->setMinimumWidth(0);
    hero->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(18);
    heroLayout->addWidget(createWidgetIconLabel(id, iconPath, hero, 64));
    auto* heroText = new QVBoxLayout();
    heroText->setContentsMargins(0, 0, 0, 0);
    heroText->setSpacing(6);
    auto* title = new QLabel(name.isEmpty() ? id : name, hero);
    title->setWordWrap(true);
    title->setMinimumWidth(0);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setStyleSheet(QStringLiteral("color:#101827;font-size:24px;font-weight:800;background:transparent;"));
    auto* subtitle = new QLabel(description.isEmpty() ? tr("LinWidget 桌面小组件。") : description, hero);
    subtitle->setWordWrap(true);
    subtitle->setMinimumWidth(0);
    subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    subtitle->setStyleSheet(QStringLiteral("color:#66738c;font-size:14px;font-weight:500;background:transparent;line-height:150%;"));
    heroText->addWidget(title);
    heroText->addWidget(subtitle);
    heroLayout->addLayout(heroText, 1);

    QPushButton* primaryButton = nullptr;
    if (isOfficial && !installed) {
        primaryButton = createSettingsActionButton(tr("install"), hero, true);
        connect(primaryButton, &QPushButton::clicked, this, [this, packagePath]() {
            installOfficialSettingsLinWidget(packagePath);
            closeDrawer();
        });
    } else if (isOfficial && installed) {
        primaryButton = createSettingsActionButton(tr("manage"), hero, true);
        connect(primaryButton, &QPushButton::clicked, this, [this, id]() {
            if (m_installedLinWidgetsList) {
                for (int i = 0; i < m_installedLinWidgetsList->count(); ++i) {
                    QListWidgetItem* installedItem = m_installedLinWidgetsList->item(i);
                    if (installedItem && installedItem->data(SettingsLinWidgetRegistryIdRole).toString().compare(id, Qt::CaseInsensitive) == 0) {
                        m_linWidgetsTabs->setCurrentIndex(2);
                        m_installedLinWidgetsList->setCurrentItem(installedItem);
                        openLinWidgetSettingsDrawer(installedItem);
                        return;
                    }
                }
            }
        });
    } else if (isMarketplace) {
        primaryButton = createSettingsActionButton(tr("install"), hero, true);
        primaryButton->setEnabled(QFileInfo(packagePath).isDir());
    } else {
        primaryButton = createSettingsActionButton(multiInstance ? tr("add") : (running ? tr("close") : tr("load")), hero, true);
        primaryButton->setEnabled(!multiInstance || m_linWidgetInstanceCountsById.value(registryKey, 0) < 3);
        connect(primaryButton, &QPushButton::clicked, this, [this, running, multiInstance]() {
            m_preserveLinWidgetDrawerOnReload = true;
            if (multiInstance) {
                loadSelectedSettingsLinWidget();
            } else {
                running ? unloadSelectedSettingsLinWidget() : loadSelectedSettingsLinWidget();
            }
            m_preserveLinWidgetDrawerOnReload = false;
        });
    }
    heroLayout->addWidget(primaryButton);
    layout->addWidget(hero);

    auto* details = createPanel(content);
    details->setGraphicsEffect(nullptr);
    details->setMinimumWidth(0);
    details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(22, 20, 22, 20);
    detailsLayout->setSpacing(0);
    detailsLayout->addWidget(createCardTitle(tr("Details"), details));

    auto addDetailRow = [&](const QString& label, const QString& value) {
        auto* row = new QWidget(details);
        row->setMinimumWidth(0);
        row->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 11, 0, 11);
        rowLayout->setSpacing(16);
        auto* left = new QLabel(label, row);
        left->setStyleSheet(QStringLiteral("color:#7a8498;font-size:13px;font-weight:600;background:transparent;"));
        left->setFixedWidth(110);
        auto* right = new QLabel(value.isEmpty() ? QStringLiteral("unknown") : value, row);
        right->setWordWrap(true);
        right->setMinimumWidth(0);
        right->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        right->setTextInteractionFlags(Qt::TextSelectableByMouse);
        right->setStyleSheet(QStringLiteral("color:#1f2a44;font-size:13px;font-weight:600;background:transparent;"));
        rowLayout->addWidget(left);
        rowLayout->addWidget(right, 1);
        detailsLayout->addWidget(row);
        detailsLayout->addWidget(createDivider(details));
    };

    addDetailRow(tr("version"), version.isEmpty() ? QStringLiteral("unknown") : version);
    addDetailRow(tr("author"), author.isEmpty() ? QStringLiteral("LinDesk") : author);
    addDetailRow(tr("permission"), settingsPermissionsText(requested));
    addDetailRow(tr("status"), multiInstance && running
        ? tr("running %1/3").arg(m_linWidgetInstanceCountsById.value(registryKey, 0))
        : (running ? tr("running") : (installed ? tr("installed") : tr("uninstall"))));
    addDetailRow(tr("description"), description.isEmpty() ? tr("no description") : description);

    if (isOfficial && installed) {
        layout->addSpacing(4);
        auto* note = createCardSubtitle(tr("This widget is already installed. Open the installed instance to manage permissions and runtime state."), details);
        note->setWordWrap(true);
        note->setMinimumWidth(0);
        note->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        detailsLayout->addWidget(note);
    }

    QString logTextForDrawer;
    QString errorsTextForDrawer = tr("no error");
    QStringList deniedPermissions;
    if (!isOfficial && !isMarketplace) {
        const QString logInstanceId = instanceId.isEmpty()
            ? settingsLinWidgetRegistryKey(id, isDevelopment)
            : instanceId;
        const QString logPath = settingsLinWidgetLogPath(logInstanceId);
        QFile logFile(logPath);
        if (!logFile.exists()) {
            const QString sharedLogPath = settingsLinWidgetLogPath(QStringLiteral("__shared__"));
            if (QFileInfo::exists(sharedLogPath)) {
                logFile.setFileName(sharedLogPath);
            } else {
                logTextForDrawer = tr("log file not created: \n%1").arg(logPath);
            }
        }
        if (logTextForDrawer.isEmpty()) {
            if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                logTextForDrawer = tr("failed to open log file: \n%1").arg(logFile.fileName());
            } else {
                QByteArray contentBytes = logFile.readAll();
                constexpr int maxDrawerLogBytes = 120 * 1024;
                if (contentBytes.size() > maxDrawerLogBytes) {
                    contentBytes = contentBytes.right(maxDrawerLogBytes);
                }
                logTextForDrawer = QString::fromUtf8(contentBytes);
            }
        }
        QStringList errorLines;
        for (const QString& line : logTextForDrawer.split(QLatin1Char('\n'))) {
            const QString lower = line.toLower();
            if (lower.contains(QStringLiteral("permission denied")) ||
                lower.contains(QStringLiteral("error")) ||
                lower.contains(QStringLiteral("failed")) ||
                lower.contains(QStringLiteral("js.rejection"))) {
                errorLines.append(line);
            }
        }
        while (errorLines.size() > 8) {
            errorLines.removeFirst();
        }
        if (!errorLines.isEmpty()) {
            errorsTextForDrawer = errorLines.join(QLatin1Char('\n'));
        }
        QStringList missingRequestedPermissions;
        for (const QString& permission : requested) {
            const QString normalizedPermission = permission.trimmed().toLower();
            if (!normalizedPermission.isEmpty() &&
                !granted.contains(normalizedPermission) &&
                !missingRequestedPermissions.contains(normalizedPermission)) {
                missingRequestedPermissions.append(normalizedPermission);
            }
        }
        deniedPermissions = missingRequestedPermissions;
    }

    if (!isOfficial && !isMarketplace) {
        if (!isDevelopment && !deniedPermissions.isEmpty()) {
            auto* hint = new QWidget(details);
            hint->setObjectName(QStringLiteral("linWidgetPermissionHint"));
            hint->setAttribute(Qt::WA_StyledBackground, true);
            hint->setStyleSheet(QStringLiteral(
                "QWidget#linWidgetPermissionHint {"
                " background:#fff8e6;"
                " border:1px solid #fde7a5;"
                " border-radius:12px;"
                "}"));
            auto* hintLayout = new QHBoxLayout(hint);
            hintLayout->setContentsMargins(14, 12, 14, 12);
            hintLayout->setSpacing(12);

            auto* icon = new QLabel(glyph(0xE7BA), hint);
            icon->setAlignment(Qt::AlignCenter);
            icon->setFixedSize(30, 30);
            icon->setStyleSheet(QStringLiteral(
                "QLabel {"
                " color:#b45309;"
                " background:#ffedd5;"
                " border-radius:8px;"
                " font-family:'Segoe Fluent Icons','Segoe MDL2 Assets';"
                " font-size:15px;"
                "}"));
            hintLayout->addWidget(icon, 0, Qt::AlignTop);

            auto* body = new QVBoxLayout();
            body->setContentsMargins(0, 0, 0, 0);
            body->setSpacing(10);
            auto* text = new QLabel(tr("The widget request was intercepted by the permission system. Please authorize %1 and reload.").arg(deniedPermissions.join(QStringLiteral(", "))), hint);
            text->setWordWrap(true);
            text->setMinimumWidth(0);
            text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            text->setStyleSheet(QStringLiteral("color:#7c4a03;font-size:13px;font-weight:600;background:transparent;"));
            body->addWidget(text);

            auto* grantRow = new QHBoxLayout();
            grantRow->setContentsMargins(0, 0, 0, 0);
            grantRow->setSpacing(8);
            for (const QString& permission : deniedPermissions) {
                auto* grantButton = createSettingsActionButton(tr("grant %1").arg(permission), hint, true);
                connect(grantButton, &QPushButton::clicked, this, [this, id, packagePath, registryKey, permission]() {
                    m_preserveLinWidgetDrawerOnReload = true;
                    setSettingsLinWidgetPermission(id, packagePath, registryKey, permission, true);
                    m_preserveLinWidgetDrawerOnReload = false;
                });
                grantRow->addWidget(grantButton, 0, Qt::AlignVCenter);
            }
            grantRow->addStretch();
            body->addLayout(grantRow);
            hintLayout->addLayout(body, 1);
            detailsLayout->addSpacing(12);
            detailsLayout->addWidget(hint);
        }
        detailsLayout->addSpacing(8);
        detailsLayout->addWidget(createCardTitle(tr("permissions"), details));
        auto* permissionGrid = new QWidget(details);
        permissionGrid->setMinimumWidth(0);
        permissionGrid->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* permissionLayout = new QVBoxLayout(permissionGrid);
        permissionLayout->setContentsMargins(0, 12, 0, 12);
        permissionLayout->setSpacing(10);
        auto makePermissionToggle = [&](const QString& permission) {
            auto* toggle = new QWidget(details);
            toggle->setMinimumWidth(0);
            toggle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto* toggleLayout = new QHBoxLayout(toggle);
            toggleLayout->setContentsMargins(0, 0, 0, 0);
            toggleLayout->setSpacing(8);
            auto* check = new FluentCheckBox(toggle);
            check->setChecked(isDevelopment || granted.contains(permission));
            check->setEnabled(!isDevelopment);
            auto* label = new QLabel(permission, toggle);
            label->setStyleSheet(QStringLiteral("color:#1f2a44;font-size:13px;font-weight:600;background:transparent;"));
            label->setEnabled(!isDevelopment);
            label->setMinimumWidth(0);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            toggleLayout->addWidget(check, 0, Qt::AlignVCenter);
            toggleLayout->addWidget(label, 1, Qt::AlignVCenter);
            connect(check, &FluentCheckBox::toggled, this, [this, id, packagePath, registryKey, permission](bool checked) {
                m_preserveLinWidgetDrawerOnReload = true;
                setSettingsLinWidgetPermission(id, packagePath, registryKey, permission, checked);
                m_preserveLinWidgetDrawerOnReload = false;
            });
            permissionLayout->addWidget(toggle);
        };
        makePermissionToggle(QStringLiteral("network"));
        makePermissionToggle(QStringLiteral("storage"));
        makePermissionToggle(QStringLiteral("desktop"));
        detailsLayout->addWidget(permissionGrid);

        auto* actionPanel = new QWidget(details);
        actionPanel->setMinimumWidth(0);
        actionPanel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* actionRow = new QVBoxLayout(actionPanel);
        actionRow->setContentsMargins(0, 4, 0, 12);
        actionRow->setSpacing(8);
        auto* reloadButton = createSettingsActionButton(tr("reload"), details);
        auto* folderButton = createSettingsActionButton(tr("open folder"), details);
        auto* deleteButton = createSettingsActionButton(tr("uninstall"), details, false, true);
        reloadButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        folderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        deleteButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        reloadButton->setEnabled(running);
        deleteButton->setEnabled(!isDevelopment);
        connect(reloadButton, &QPushButton::clicked, this, [this]() {
            m_preserveLinWidgetDrawerOnReload = true;
            reloadSelectedSettingsLinWidget();
            m_preserveLinWidgetDrawerOnReload = false;
        });
        connect(folderButton, &QPushButton::clicked, this, [packagePath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(packagePath));
        });
        connect(deleteButton, &QPushButton::clicked, this, &SettingUI::uninstallSelectedSettingsLinWidget);
        actionRow->addWidget(reloadButton);
        actionRow->addWidget(folderButton);
        actionRow->addWidget(deleteButton);
        detailsLayout->addWidget(actionPanel);
    }

    auto* previewTitle = createCardTitle(tr("preview"), details);
    detailsLayout->addWidget(previewTitle);
    auto* preview = new QLabel(details);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(150);
    preview->setMinimumWidth(0);
    preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    preview->setWordWrap(true);
    preview->setText(tr("%1 preview").arg(name.isEmpty() ? id : name));
    preview->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background:#ffffff;"
        " border:1px solid #e5edf7;"
        " border-radius:14px;"
        " color:#66738c;"
        " font-size:13px;"
        " font-weight:600;"
        "}"));
    detailsLayout->addSpacing(10);
    detailsLayout->addWidget(preview);

    if (!isOfficial && !isMarketplace) {
        detailsLayout->addSpacing(12);
        detailsLayout->addWidget(createCardTitle(tr("Recent errors"), details));
        auto* errorsLabel = createCardSubtitle(errorsTextForDrawer, details);
        errorsLabel->setMinimumWidth(0);
        errorsLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        detailsLayout->addWidget(errorsLabel);
    }

    layout->addWidget(details);
    layout->addStretch();
    openDrawer(content, QString(), 620);
}

void SettingUI::selectAndOpenLinWidgetSettingsItem(QListWidgetItem* item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }

    QListWidget* list = item->listWidget();
    if (!list) {
        return;
    }

    if (list == m_installedLinWidgetsList && m_developmentLinWidgetsList) {
        m_developmentLinWidgetsList->clearSelection();
        m_developmentLinWidgetsList->setCurrentRow(-1);
    } else if (list == m_developmentLinWidgetsList && m_installedLinWidgetsList) {
        m_installedLinWidgetsList->clearSelection();
        m_installedLinWidgetsList->setCurrentRow(-1);
    } else if (list != m_installedLinWidgetsList && list != m_developmentLinWidgetsList) {
        if (m_installedLinWidgetsList) {
            m_installedLinWidgetsList->clearSelection();
            m_installedLinWidgetsList->setCurrentRow(-1);
        }
        if (m_developmentLinWidgetsList) {
            m_developmentLinWidgetsList->clearSelection();
            m_developmentLinWidgetsList->setCurrentRow(-1);
        }
    }

    if (list->currentItem() != item) {
        list->setCurrentItem(item);
    }
    refreshLinWidgetSettingsButtons();
    refreshLinWidgetSettingsInspector();
    openLinWidgetSettingsDrawer(item);
}

void SettingUI::refreshLinWidgetSettingsRunningMaps()
{
    m_linWidgetInstancesByPackage.clear();
    m_linWidgetInstancesById.clear();
    m_linWidgetInstanceCountsById.clear();

    auto& bridge = LinWidgetRuntimeBridge::instance();
    bool loadedFromRuntime = false;
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("list"));
        const QJsonObject resp = bridge.queryWidgetManager(req);
        const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();
        if (!widgets.isEmpty()) {
            loadedFromRuntime = true;
            for (const QJsonValue& val : widgets) {
                const QJsonObject obj = val.toObject();
                const QString regId = obj.value(QStringLiteral("id")).toString().trimmed().toLower();
                const QString packagePath = obj.value(QStringLiteral("packagePath")).toString();
                const bool isDev = obj.value(QStringLiteral("isDevelopment")).toBool();
                const QString key = settingsLinWidgetRegistryKey(regId, isDev);
                const QJsonArray runningIds = obj.value(QStringLiteral("runningInstanceIds")).toArray();
                if (!runningIds.isEmpty()) {
                    for (const QJsonValue& idVal : runningIds) {
                        const QString instId = idVal.toString();
                        m_linWidgetInstancesById.insert(key, instId);
                        if (!packagePath.isEmpty()) {
                            m_linWidgetInstancesByPackage.insert(settingsLinWidgetPackageKey(packagePath), instId);
                        }
                    }
                    m_linWidgetInstanceCountsById.insert(key, runningIds.size());
                }
            }
        }
    }

    if (!loadedFromRuntime) {
        QVector<LinWidget::WidgetLayoutState> states;
        LinWidget::ConfigManager::loadLayout(LinWidget::ConfigManager::defaultConfigPath(), &states, nullptr);

        for (const LinWidget::WidgetLayoutState& state : states) {
            if (!state.visible) {
                continue;
            }
            m_linWidgetInstancesByPackage.insert(settingsLinWidgetPackageKey(state.packagePath), state.instanceId);
            const bool isDevelopment = state.isDevelopment || state.instanceId.startsWith(QStringLiteral("dev:"));
            QString registryId = state.registryId;
            LinWidget::WidgetManifest manifest;
            if (registryId.isEmpty() && LinWidget::WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
                registryId = manifest.id;
            }
            if (!registryId.isEmpty()) {
                const QString key = settingsLinWidgetRegistryKey(registryId, isDevelopment);
                m_linWidgetInstancesById.insert(key, state.instanceId);
                m_linWidgetInstanceCountsById.insert(key, m_linWidgetInstanceCountsById.value(key, 0) + 1);
            }
        }
    }

    if (bridge.isLoaded() && bridge.hasActiveWallpaper()) {
        const QString activePath = bridge.activeWallpaperPackagePath();
        const QString activeId = bridge.activeWallpaperId();
        const bool isDev = bridge.activeWallpaperIsDevelopment();
        const QString wallInstanceId = QStringLiteral("wallpaper:") + activeId;
        m_linWidgetInstancesByPackage.insert(settingsLinWidgetPackageKey(activePath), wallInstanceId);
        const QString key = settingsLinWidgetRegistryKey(activeId, isDev);
        m_linWidgetInstancesById.insert(key, wallInstanceId);
        m_linWidgetInstanceCountsById.insert(key, m_linWidgetInstanceCountsById.value(key, 0) + 1);
    }
}

void SettingUI::loadSelectedSettingsLinWidget()
{
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    const QString registryId = selectedSettingsLinWidgetRegistryId();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    if (packagePath.isEmpty()) {
        return;
    }
    if (QListWidget* list = activeSettingsLinWidgetList()) {
        if (QListWidgetItem* item = list->currentItem()) {
            if (!item->data(SettingsLinWidgetManifestValidRole).toBool()) {
                if (m_linWidgetsStatusLabel) {
                    m_linWidgetsStatusLabel->setText(tr("Load blocked: %1").arg(item->data(SettingsLinWidgetManifestErrorRole).toString()));
                }
                refreshLinWidgetSettingsButtons();
                return;
            }
        }
    }

    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Load failed: %1").arg(errorMessage));
        }
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Runtime not loaded"));
        }
        return;
    }
    const bool launched = bridge.startWidgetPackage(packagePath);
    if (!launched) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Load failed"));
        }
        return;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.setLastRunState(registryId, isDevelopment, QStringLiteral("running"));
    registry.save();

    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Loaded widget: %1").arg(manifest.name));
    }
    reloadLinWidgetsSettingsPage();
}

void SettingUI::unloadSelectedSettingsLinWidget()
{
    const QString registryId = selectedSettingsLinWidgetRegistryId();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    const QString registryKey = selectedSettingsLinWidgetRegistryKey();
    QString instanceId = selectedSettingsLinWidgetInstanceId();
    const QString packagePath = selectedSettingsLinWidgetPackagePath();

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("list"));
        const QJsonObject resp = bridge.queryWidgetManager(req);
        const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();
        QStringList instancesToStop;
        for (const QJsonValue& val : widgets) {
            const QJsonObject obj = val.toObject();
            if (obj.value(QStringLiteral("id")).toString().compare(registryId, Qt::CaseInsensitive) == 0 &&
                obj.value(QStringLiteral("isDevelopment")).toBool() == isDevelopment) {
                const QJsonArray runningIds = obj.value(QStringLiteral("runningInstanceIds")).toArray();
                for (const QJsonValue& idVal : runningIds) {
                    instancesToStop.append(idVal.toString());
                }
            }
        }
        if (instancesToStop.isEmpty() && !instanceId.isEmpty()) {
            instancesToStop.append(instanceId);
        }
        if (instancesToStop.isEmpty() && !registryKey.isEmpty()) {
            instancesToStop.append(registryKey);
        }

        for (const QString& id : instancesToStop) {
            bridge.stopWidget(id);
        }
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    if (!registryId.isEmpty()) {
        registry.setLastRunState(registryId, isDevelopment, QStringLiteral("stopped"));
        registry.save();
    }

    m_linWidgetInstancesById.remove(registryKey);
    m_linWidgetInstanceCountsById.remove(registryKey);
    if (!packagePath.isEmpty()) {
        m_linWidgetInstancesByPackage.remove(settingsLinWidgetPackageKey(packagePath));
    }

    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Unloaded widget: %1").arg(!registryId.isEmpty() ? registryId : instanceId));
    }
    reloadLinWidgetsSettingsPage();
}

void SettingUI::reloadSelectedSettingsLinWidget()
{
    const QString instanceId = selectedSettingsLinWidgetInstanceId();
    if (instanceId.isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        if (bridge.restartWidget(instanceId)) {
            if (m_linWidgetsStatusLabel) {
                m_linWidgetsStatusLabel->setText(tr("Reloaded widget: %1").arg(instanceId));
            }
        }
    }
    refreshLinWidgetSettingsInspector();
    refreshLinWidgetSettingsButtons();
}

void SettingUI::uninstallSelectedSettingsLinWidget()
{
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    const QString registryId = selectedSettingsLinWidgetRegistryId();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    if (packagePath.isEmpty() || registryId.isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    bool confirmed = true;
    if (!isDevelopment) {
        FluentDialog dialog(this);
        dialog.setDialogTitle(tr("卸载小组件"));
        dialog.setMessage(tr("确定要卸载这个小组件和它的保存记录吗？\n%1").arg(packagePath));
        dialog.addActionButton(tr("卸载"), FluentDialog::ActionType::Primary, [&confirmed]() {
            confirmed = true;
        });
        dialog.addActionButton(tr("取消"), FluentDialog::ActionType::Secondary);
        confirmed = false;
        dialog.exec();
    }
    if (!confirmed) {
        return;
    }

    const QString stableInstanceId = settingsLinWidgetRegistryKey(registryId, isDevelopment);
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        bridge.removeWidgetState(stableInstanceId);
    }

    if (isDevelopment) {
        auto& registry = LinWidget::WidgetRegistryManager::instance();
        registry.load();
        registry.removeDevelopmentProject(registryId);
        registry.save();
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Removed development record: %1").arg(registryId));
        }
        reloadLinWidgetsSettingsPage();
        return;
    }

    settingsRemoveLinWidgetLayoutState(stableInstanceId, packagePath);

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.removeById(registryId);
    registry.save();

    QString errorMessage;
    if (!settingsRemoveDirectory(packagePath, &errorMessage) && m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Widget record removed, folder delete failed: %1").arg(errorMessage));
    } else if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Uninstalled installed widget: %1").arg(registryId));
    }
    reloadLinWidgetsSettingsPage();
}

void SettingUI::installOfficialSettingsLinWidget(const QString& packagePath)
{
    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official install failed: %1").arg(errorMessage));
        }
        return;
    }
    bool accepted = false;
    const QStringList grantedPermissions = promptLinWidgetInstallPermissions(this, manifest, &accepted);
    if (!accepted) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official install canceled: %1").arg(manifest.name));
        }
        return;
    }
    const QString targetPath = settingsInstalledTargetForManifest(manifest);
    if (!QFileInfo::exists(targetPath) &&
        !settingsCopyLinWidgetDirectory(packagePath, targetPath, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official install failed: %1").arg(errorMessage));
        }
        return;
    }
    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.upsertFromPackage(targetPath, &errorMessage);
    registry.setPermissionsGranted(manifest.id, false, grantedPermissions);
    registry.save();
    QString instanceId;
    QString launchError;
    const bool launched = launchInstalledSettingsLinWidget(targetPath, manifest, &instanceId, &launchError);
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(launched
            ? tr("Installed and loaded official widget: %1").arg(manifest.name)
            : tr("Installed official widget: %1\nLoad skipped: %2").arg(manifest.name, launchError));
    }
    reloadLinWidgetsSettingsPage();
}

bool SettingUI::launchInstalledSettingsLinWidget(const QString& packagePath,
                                                 const LinWidget::WidgetManifest& manifest,
                                                 QString* instanceId,
                                                 QString* errorMessage)
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    if (!settings.value("LinWidgets/Enabled", true).toBool()) {
        if (errorMessage) {
            *errorMessage = tr("LinWidget runtime is disabled.");
        }
        return false;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        if (errorMessage) {
            *errorMessage = tr("LinWidget runtime is not available.");
        }
        return false;
    }

    const bool launched = bridge.startWidgetPackage(packagePath);
    if (!launched) {
        if (errorMessage) {
            *errorMessage = tr("Failed to launch widget package.");
        }
        return false;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.setLastRunState(manifest.id, false, QStringLiteral("running"));
    registry.save();
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void SettingUI::resetOfficialSettingsLinWidget(const QString& packagePath)
{
    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official reset failed: %1").arg(errorMessage));
        }
        return;
    }
    const QString targetPath = settingsInstalledTargetForManifest(manifest);
    if (!settingsRemoveDirectory(targetPath, &errorMessage) ||
        !settingsCopyLinWidgetDirectory(packagePath, targetPath, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official reset failed: %1").arg(errorMessage));
        }
        return;
    }
    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.upsertFromPackage(targetPath, &errorMessage);
    registry.save();
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Reset official widget: %1").arg(manifest.name));
    }
    reloadLinWidgetsSettingsPage();
}

void SettingUI::addDevelopmentSettingsLinWidget()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select LinWidget development project"));
    if (dir.isEmpty()) {
        return;
    }
    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString errorMessage;
    registry.load();
    if (!registry.addDevelopmentProject(dir, &errorMessage) || !registry.save(&errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Add development project failed: %1").arg(errorMessage));
        }
        return;
    }
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Added development project: %1").arg(dir));
    }
    reloadLinWidgetsSettingsPage();
}

void SettingUI::createDevelopmentSettingsLinWidget()
{
    const QString targetDir = QFileDialog::getExistingDirectory(this, tr("Choose parent folder for new widget project"));
    if (targetDir.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString id = QInputDialog::getText(this, tr("Widget Id"), tr("Use reverse domain style, for example com.example.weather"), QLineEdit::Normal, QStringLiteral("com.example.widget"), &ok).trimmed();
    if (!ok || id.isEmpty()) {
        return;
    }
    const QString name = QInputDialog::getText(this, tr("Widget Name"), tr("Display name"), QLineEdit::Normal, QStringLiteral("New Widget"), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const QString author = QInputDialog::getText(this, tr("Author"), tr("Author"), QLineEdit::Normal, qEnvironmentVariable("USERNAME"), &ok).trimmed();
    if (!ok) {
        return;
    }
    const QString description = QInputDialog::getText(this, tr("Description"), tr("Short description"), QLineEdit::Normal, QStringLiteral("A LinWidget project."), &ok).trimmed();
    if (!ok) {
        return;
    }

    LinWidget::WidgetProjectMetadata metadata;
    metadata.id = id;
    metadata.name = name;
    metadata.author = author;
    metadata.description = description;

    const QString projectDir = QDir(targetDir).filePath(safeLinWidgetFileName(id));
    QString errorMessage;
    if (!LinWidget::LinWidgetProjectTools::createProject(projectDir, metadata, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Create project failed: %1").arg(errorMessage));
        }
        return;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.addDevelopmentProject(projectDir, &errorMessage);
    registry.save();
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Created development project: %1").arg(projectDir));
    }
    reloadLinWidgetsSettingsPage();
}

void SettingUI::exportDevelopmentSettingsLinWidget()
{
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    if (packagePath.isEmpty() || !selectedSettingsLinWidgetIsDevelopment() || !selectedSettingsLinWidgetInstanceId().isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }
    const QString id = selectedSettingsLinWidgetRegistryId();
    const QString outputPath = QFileDialog::getSaveFileName(this,
                                                           tr("Export LinWidget Zip"),
                                                           QDir::home().filePath(safeLinWidgetFileName(id) + QStringLiteral(".zip")),
                                                           tr("Zip package (*.zip)"));
    if (outputPath.isEmpty()) {
        return;
    }
    QString errorMessage;
    if (!LinWidget::LinWidgetProjectTools::exportProjectZip(packagePath, outputPath, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Export zip failed: %1").arg(errorMessage));
        }
        return;
    }
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Exported zip package: %1").arg(outputPath));
    }
}

void SettingUI::deleteSelectedSettingsLinWidget()
{
    uninstallSelectedSettingsLinWidget();
}

void SettingUI::setSelectedSettingsLinWidgetPermission(const QString& permission, bool granted)
{
    QListWidget* list = activeSettingsLinWidgetList();
    QListWidgetItem* item = list ? list->currentItem() : nullptr;
    if (!item) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    const QString registryId = item->data(SettingsLinWidgetRegistryIdRole).toString();
    const QString packagePath = item->data(SettingsLinWidgetPackagePathRole).toString();
    const bool isDevelopment = item->data(SettingsLinWidgetDevelopmentRole).toBool();
    if (registryId.trimmed().isEmpty() || isDevelopment) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    setSettingsLinWidgetPermission(registryId,
                                   packagePath,
                                   settingsLinWidgetRegistryKey(registryId, false),
                                   permission,
                                   granted);
}

void SettingUI::setSettingsLinWidgetPermission(const QString& registryId,
                                               const QString& packagePath,
                                               const QString& selectedKey,
                                               const QString& permission,
                                               bool granted)
{
    const QString normalizedId = registryId.trimmed().toLower();
    if (normalizedId.isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString registryError;
    if (!registry.load(&registryError)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Save permission failed: %1").arg(registryError));
        }
        refreshLinWidgetSettingsButtons();
        return;
    }

    LinWidget::WidgetRegistryEntry entry = registry.entryById(normalizedId);
    QStringList permissions = entry.permissionsGranted;
    const QString normalized = permission.trimmed().toLower();
    if (granted && !permissions.contains(normalized)) {
        permissions.append(normalized);
    } else if (!granted) {
        permissions.removeAll(normalized);
    }
    permissions.sort();

    if (!registry.setPermissionsGranted(normalizedId, false, permissions)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Save permission failed: widget record not found."));
        }
        refreshLinWidgetSettingsButtons();
        return;
    }
    if (!registry.save(&registryError)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Save permission failed: %1").arg(registryError));
        }
        refreshLinWidgetSettingsButtons();
        return;
    }

    const QString stableSelectedKey = selectedKey.isEmpty()
        ? settingsLinWidgetRegistryKey(normalizedId, false)
        : selectedKey;
    const QString packageKey = settingsLinWidgetPackageKey(packagePath.isEmpty() ? entry.packagePath : packagePath);
    const QString instanceId = m_linWidgetInstancesById.value(stableSelectedKey, m_linWidgetInstancesByPackage.value(packageKey));
    if (!instanceId.isEmpty()) {
        LinWidgetRuntimeBridge::instance().restartWidget(instanceId);
    }
    reloadLinWidgetsSettingsPage();
    if (!stableSelectedKey.isEmpty()) {
        auto restoreSelection = [this, stableSelectedKey](QListWidget* targetList) -> QListWidgetItem* {
            if (!targetList) {
                return nullptr;
            }
            for (int i = 0; i < targetList->count(); ++i) {
                QListWidgetItem* candidate = targetList->item(i);
                if (!candidate) {
                    continue;
                }
                const QString candidateKey = settingsLinWidgetRegistryKey(
                    candidate->data(SettingsLinWidgetRegistryIdRole).toString(),
                    candidate->data(SettingsLinWidgetDevelopmentRole).toBool());
                if (candidateKey == stableSelectedKey) {
                    targetList->setCurrentItem(candidate);
                    return candidate;
                }
            }
            return nullptr;
        };
        QListWidgetItem* restoredItem = restoreSelection(m_installedLinWidgetsList);
        if (!restoredItem) {
            restoredItem = restoreSelection(m_developmentLinWidgetsList);
        }
        if (m_preserveLinWidgetDrawerOnReload && restoredItem) {
            openLinWidgetSettingsDrawer(restoredItem);
        }
    }
    refreshLinWidgetSettingsButtons();
    refreshLinWidgetSettingsInspector();
}

QListWidget* SettingUI::activeSettingsLinWidgetList() const
{
    if (m_linWidgetSubsystemStack && m_linWidgetSubsystemStack->currentIndex() == 1) {
        if (m_wallpaperList && m_wallpaperList->currentItem()) {
            return m_wallpaperList;
        }
    }

    const int tabIndex = m_linWidgetsTabs ? m_linWidgetsTabs->currentIndex() : -1;
    if (tabIndex == 0 && m_officialLinWidgetsList && m_officialLinWidgetsList->currentItem()) {
        return m_officialLinWidgetsList;
    }
    if (tabIndex == 1 && m_marketplaceLinWidgetsList && m_marketplaceLinWidgetsList->currentItem()) {
        return m_marketplaceLinWidgetsList;
    }
    if (tabIndex == 2 && m_installedLinWidgetsList && m_installedLinWidgetsList->currentItem()) {
        return m_installedLinWidgetsList;
    }
    if (tabIndex == 3 && m_developmentLinWidgetsList && m_developmentLinWidgetsList->currentItem()) {
        return m_developmentLinWidgetsList;
    }

    if (m_installedLinWidgetsList && !m_installedLinWidgetsList->selectedItems().isEmpty() && m_installedLinWidgetsList->currentItem()) {
        return m_installedLinWidgetsList;
    }
    if (m_officialLinWidgetsList && !m_officialLinWidgetsList->selectedItems().isEmpty() && m_officialLinWidgetsList->currentItem()) {
        return m_officialLinWidgetsList;
    }
    if (m_developmentLinWidgetsList && !m_developmentLinWidgetsList->selectedItems().isEmpty() && m_developmentLinWidgetsList->currentItem()) {
        return m_developmentLinWidgetsList;
    }
    if (m_wallpaperList && !m_wallpaperList->selectedItems().isEmpty() && m_wallpaperList->currentItem()) {
        return m_wallpaperList;
    }
    if (m_marketplaceLinWidgetsList && !m_marketplaceLinWidgetsList->selectedItems().isEmpty() && m_marketplaceLinWidgetsList->currentItem()) {
        return m_marketplaceLinWidgetsList;
    }

    if (m_installedLinWidgetsList && m_installedLinWidgetsList->currentItem()) {
        return m_installedLinWidgetsList;
    }
    if (m_officialLinWidgetsList && m_officialLinWidgetsList->currentItem()) {
        return m_officialLinWidgetsList;
    }
    if (m_developmentLinWidgetsList && m_developmentLinWidgetsList->currentItem()) {
        return m_developmentLinWidgetsList;
    }
    if (m_wallpaperList && m_wallpaperList->currentItem()) {
        return m_wallpaperList;
    }
    return nullptr;
}

QString SettingUI::selectedSettingsLinWidgetPackagePath() const
{
    QListWidget* list = activeSettingsLinWidgetList();
    return list && list->currentItem()
        ? list->currentItem()->data(SettingsLinWidgetPackagePathRole).toString()
        : QString();
}

QString SettingUI::selectedSettingsLinWidgetRegistryId() const
{
    QListWidget* list = activeSettingsLinWidgetList();
    return list && list->currentItem()
        ? list->currentItem()->data(SettingsLinWidgetRegistryIdRole).toString()
        : QString();
}

bool SettingUI::selectedSettingsLinWidgetIsDevelopment() const
{
    QListWidget* list = activeSettingsLinWidgetList();
    return list && list->currentItem() && list->currentItem()->data(SettingsLinWidgetDevelopmentRole).toBool();
}

QString SettingUI::selectedSettingsLinWidgetInstanceId() const
{
    const QString registryKey = selectedSettingsLinWidgetRegistryKey();
    if (m_linWidgetInstancesById.contains(registryKey)) {
        return m_linWidgetInstancesById.value(registryKey);
    }

    const QString packageKey = settingsLinWidgetPackageKey(selectedSettingsLinWidgetPackagePath());
    if (m_linWidgetInstancesByPackage.contains(packageKey)) {
        return m_linWidgetInstancesByPackage.value(packageKey);
    }
    return QString();
}

QString SettingUI::selectedSettingsLinWidgetRegistryKey() const
{
    return settingsLinWidgetRegistryKey(selectedSettingsLinWidgetRegistryId(), selectedSettingsLinWidgetIsDevelopment());
}

QString SettingUI::settingsLinWidgetRegistryKey(const QString& id, bool isDevelopment) const
{
    return QStringLiteral("%1:%2").arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"),
                                       id.trimmed().toLower());
}

QString SettingUI::settingsLinWidgetPackageKey(const QString& packagePath) const
{
    const QFileInfo info(packagePath);
    const QString path = info.exists() ? info.absoluteFilePath() : packagePath;
    return QDir::cleanPath(path).toLower();
}

QString SettingUI::settingsLinWidgetLogPath(const QString& instanceId) const
{
    if (instanceId.isEmpty()) {
        return QString();
    }
    return QDir(AppPaths::linWidgetLogsDir()).filePath(safeLinWidgetInstanceFileNameForSettings(instanceId) + QStringLiteral(".log"));
}




QWidget* SettingUI::createAboutPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setAlignment(Qt::AlignCenter);

    QLabel* icon = new QLabel(u8"\uE713", page);
    icon->setStyleSheet(
        "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; "
        "font-size: 72px; "
        "color: #0078d4; "
        "background: transparent;"
    );
    layout->addWidget(icon, 0, Qt::AlignCenter);

    m_aboutNameLabel = new QLabel(tr("LinDesk"), page);
    m_aboutNameLabel->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 28px; "
        "font-weight: 600; "
        "color: #1f2937; "
        "letter-spacing: -0.01em;"
    );
    layout->addWidget(m_aboutNameLabel, 0, Qt::AlignCenter);

    m_aboutVersionLabel = new QLabel(tr("Version %1").arg(LINDESK_VERSION_STRING_WITH_TAG), page);
    m_aboutVersionLabel->setStyleSheet(
        "font-family: 'Microsoft YaHei UI'; "
        "font-size: 13px; "
        "color: #6b7280;"
    );
    layout->addWidget(m_aboutVersionLabel, 0, Qt::AlignCenter);
    
    layout->addSpacing(24);

    QWidget* updatePanel = createPanel(page);
    QVBoxLayout* updateLayout = new QVBoxLayout(updatePanel);
    updateLayout->setContentsMargins(22, 18, 22, 18);
    updateLayout->setSpacing(10);

    updateLayout->addWidget(createCardTitle(tr("Online Update"), updatePanel));

    m_updateStatusLabel = new QLabel(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG), updatePanel);
    m_updateStatusLabel->setObjectName("rowSubtitle");
    m_updateStatusLabel->setWordWrap(true);
    updateLayout->addWidget(m_updateStatusLabel);

    m_updateNotesLabel = new QLabel(updatePanel);
    m_updateNotesLabel->setObjectName("rowSubtitle");
    m_updateNotesLabel->setWordWrap(true);
    m_updateNotesLabel->hide();
    updateLayout->addWidget(m_updateNotesLabel);

    m_updateProgressBar = new QProgressBar(updatePanel);
    m_updateProgressBar->setObjectName("updateProgressBar");
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->hide();
    updateLayout->addWidget(m_updateProgressBar);

    QHBoxLayout* updateButtons = new QHBoxLayout();
    updateButtons->setContentsMargins(0, 4, 0, 0);
    updateButtons->setSpacing(10);

    m_checkUpdateButton = new QPushButton(tr("Check for Updates"), updatePanel);
    m_checkUpdateButton->setObjectName("updatePrimaryButton");
    m_checkUpdateButton->setFixedHeight(34);
    updateButtons->addWidget(m_checkUpdateButton);

    updateButtons->addStretch(1);
    updateLayout->addLayout(updateButtons);

    layout->addWidget(updatePanel);

    connect(m_checkUpdateButton, &QPushButton::clicked, this, &SettingUI::startUpdateCheck);
    
    layout->addStretch();
    return page;
}

void SettingUI::startUpdateCheck()
{
    if (!m_updateChecker) {
        m_updateChecker = new UpdateChecker(this);
        connect(m_updateChecker, &UpdateChecker::checkSucceeded, this, [this](const UpdateInfo& info, bool updateAvailable) {
            if (!m_checkUpdateButton) {
                return;
            }

            m_pendingUpdate = info;
            m_downloadedInstallerPath.clear();
            m_checkUpdateButton->setEnabled(true);

            if (!updateAvailable) {
                m_updateStatusLabel->setText(tr("Already up to date. Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
                m_updateNotesLabel->hide();
                return;
            }

            m_updateStatusLabel->setText(tr("New version found: %1").arg(info.version));
            m_updateNotesLabel->hide();

            FluentDialog* dialog = new FluentDialog(this);
            dialog->setDialogTitle(tr("New Version Available"));
            dialog->setMessage(tr("Version: %1\n\n%2").arg(info.version, info.notes));
            dialog->addActionButton(tr("Update Now"), FluentDialog::ActionType::Primary, [this, info]() {
                m_pendingUpdate = info;
                startUpdateDownload();
            });
            dialog->addActionButton(tr("Cancel"), FluentDialog::ActionType::Close, [this]() {
                if (m_updateStatusLabel) {
                    m_updateStatusLabel->setText(tr("Update cancelled."));
                }
            });
            dialog->exec();
            dialog->deleteLater();
        });
        connect(m_updateChecker, &UpdateChecker::checkFailed, this, [this](const QString& errorMessage) {
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Update check failed: %1").arg(errorMessage));
            }
            if (m_checkUpdateButton) {
                m_checkUpdateButton->setEnabled(true);
            }
        });
    }

    if (m_updateChecker->isChecking()) {
        return;
    }

    m_pendingUpdate = UpdateInfo();
    m_downloadedInstallerPath.clear();
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Checking for updates..."));
    }
    if (m_updateNotesLabel) {
        m_updateNotesLabel->clear();
        m_updateNotesLabel->hide();
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->hide();
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(false);
    }

    m_updateChecker->checkForUpdates();
}

void SettingUI::startUpdateDownload()
{
    if (m_pendingUpdate.url.isEmpty()) {
        return;
    }

    if (!m_updateDownloader) {
        m_updateDownloader = new UpdateDownloader(this);
        connect(m_updateDownloader, &UpdateDownloader::progressChanged, this, [this](qint64 received, qint64 total) {
            if (!m_updateProgressBar) {
                return;
            }
            m_updateProgressBar->show();
            if (total <= 0) {
                m_updateProgressBar->setRange(0, 0);
                return;
            }
            m_updateProgressBar->setRange(0, 100);
            m_updateProgressBar->setValue(static_cast<int>((received * 100) / total));
        });
        connect(m_updateDownloader, &UpdateDownloader::downloadSucceeded, this, [this](const QString& installerPath) {
            m_downloadedInstallerPath = installerPath;
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Download complete. Starting installer..."));
            }
            if (m_updateProgressBar) {
                m_updateProgressBar->setRange(0, 100);
                m_updateProgressBar->setValue(100);
            }
            installDownloadedUpdate();
        });
        connect(m_updateDownloader, &UpdateDownloader::downloadFailed, this, [this](const QString& errorMessage) {
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Download failed: %1").arg(errorMessage));
            }
            if (m_updateProgressBar) {
                m_updateProgressBar->setRange(0, 100);
                m_updateProgressBar->setValue(0);
            }
            if (m_checkUpdateButton) {
                m_checkUpdateButton->setEnabled(true);
            }
        });
    }

    if (m_updateDownloader->isDownloading()) {
        return;
    }

    m_downloadedInstallerPath.clear();
    QString cachedInstallerPath;
    if (m_updateDownloader->hasValidCachedInstaller(m_pendingUpdate, &cachedInstallerPath)) {
        m_downloadedInstallerPath = cachedInstallerPath;
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("Using cached installer: %1").arg(cachedInstallerPath));
        }
        if (m_updateProgressBar) {
            m_updateProgressBar->setRange(0, 100);
            m_updateProgressBar->setValue(100);
            m_updateProgressBar->show();
        }
        installDownloadedUpdate();
        return;
    }

    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Downloading update to: %1").arg(m_updateDownloader->cachedInstallerPath(m_pendingUpdate)));
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
        m_updateProgressBar->show();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(false);
    }

    m_updateDownloader->download(m_pendingUpdate);
}

void SettingUI::installDownloadedUpdate()
{
    if (m_downloadedInstallerPath.isEmpty() || !QFileInfo::exists(m_downloadedInstallerPath)) {
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("Installer file is missing. Please download again."));
        }
        return;
    }

    const QString updaterPath = QCoreApplication::applicationDirPath() + QStringLiteral("/LinDeskUpdate.exe");
    if (!QFileInfo::exists(updaterPath)) {
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("LinDeskUpdate.exe was not found in the application directory."));
        }
        return;
    }

    QStringList args;
    const QString languageArg = LanguageManager::configuredLanguageCode().startsWith(QStringLiteral("zh"))
                                    ? QStringLiteral("zh")
                                    : QStringLiteral("en");
    args << QStringLiteral("--installer") << m_downloadedInstallerPath
         << QStringLiteral("--app") << QCoreApplication::applicationFilePath()
         << QStringLiteral("--process") << QStringLiteral("LinDesk.exe")
         << QStringLiteral("--language") << languageArg
         << QStringLiteral("--restart") << QStringLiteral("true");

    if (!QProcess::startDetached(updaterPath, args)) {
        if (m_updateStatusLabel) {
            m_updateStatusLabel->setText(tr("Failed to start updater."));
        }
        return;
    }

    QCoreApplication::quit();
}

void SettingUI::resetUpdatePanel()
{
    m_pendingUpdate = UpdateInfo();
    m_downloadedInstallerPath.clear();
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(tr("Current version: %1").arg(LINDESK_VERSION_STRING_WITH_TAG));
    }
    if (m_updateNotesLabel) {
        m_updateNotesLabel->clear();
        m_updateNotesLabel->hide();
    }
    if (m_updateProgressBar) {
        m_updateProgressBar->setRange(0, 100);
        m_updateProgressBar->setValue(0);
        m_updateProgressBar->hide();
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
    }
}

QWidget* SettingUI::createPluginsPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* outerLayout = new QVBoxLayout(page);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent;");
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* content = new QWidget(scroll);
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(54, 34, 54, 36);
    mainLayout->setSpacing(0);
    mainLayout->setAlignment(Qt::AlignTop);

    m_pluginsPageTitle = createPageTitle(tr("Plugins"), content);
    m_pluginsPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    mainLayout->addWidget(m_pluginsPageTitle, 0, Qt::AlignLeft);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(createPageSubtitle(tr("Extend LinDesk with powerful plugins and integrations."), content));
    mainLayout->addSpacing(8);

    QLabel* installedTitle = createCardTitle(tr("Installed Plugins"), content);
    mainLayout->addWidget(installedTitle);
    mainLayout->addSpacing(8);

    auto allPlugins = PluginManager::instance()->allPlugins();

    if (allPlugins.isEmpty()) {
        QWidget* emptyPanel = createPanel(content);
        QHBoxLayout* emptyLayout = new QHBoxLayout(emptyPanel);
        emptyLayout->setContentsMargins(22, 22, 22, 22);
        emptyLayout->setSpacing(16);
        emptyLayout->addWidget(createGlyphLabel(glyph(0xE74C), emptyPanel, 38, QColor("#8b96aa")), 0, Qt::AlignVCenter);
        QVBoxLayout* emptyText = new QVBoxLayout();
        emptyText->setContentsMargins(0, 0, 0, 0);
        emptyText->setSpacing(4);
        m_pluginsEmptyLabel = new QLabel(tr("No Installed Plugins"), emptyPanel);
        m_pluginsEmptyLabel->setObjectName("pluginName");
        QLabel* emptySub = new QLabel(tr("Install plugin packages to extend LinDesk capabilities."), emptyPanel);
        emptySub->setObjectName("rowSubtitle");
        emptySub->setWordWrap(true);
        emptyText->addWidget(m_pluginsEmptyLabel);
        emptyText->addWidget(emptySub);
        emptyLayout->addLayout(emptyText, 1);
        mainLayout->addWidget(emptyPanel);
    } else {
        for (auto* plugin : allPlugins) {
            if (!plugin) continue;

            QWidget* pluginPanel = createPanel(content);
            QVBoxLayout* pluginLayout = new QVBoxLayout(pluginPanel);
            pluginLayout->setContentsMargins(24, 22, 24, 22);
            pluginLayout->setSpacing(18);

            QHBoxLayout* topLayout = new QHBoxLayout();
            topLayout->setContentsMargins(0, 0, 0, 0);
            topLayout->setSpacing(18);

            QLabel* pluginIcon = createGlyphLabel(glyph(0xE74C), pluginPanel, 42);
            pluginIcon->setObjectName("pluginLargeIcon");
            topLayout->addWidget(pluginIcon, 0, Qt::AlignTop);

            QVBoxLayout* pluginText = new QVBoxLayout();
            pluginText->setContentsMargins(0, 0, 0, 0);
            pluginText->setSpacing(6);
            QLabel* pluginName = new QLabel(plugin->pluginName(), pluginPanel);
            pluginName->setObjectName("pluginName");
            QLabel* pluginDescription = new QLabel(plugin->pluginDescription(), pluginPanel);
            pluginDescription->setObjectName("pluginDescription");
            pluginDescription->setWordWrap(true);
            pluginText->addWidget(pluginName);
            pluginText->addWidget(pluginDescription);

            QHBoxLayout* badgeLayout = new QHBoxLayout();
            badgeLayout->setContentsMargins(0, 0, 0, 0);
            badgeLayout->setSpacing(8);
            const bool pluginEnabled = PluginManager::instance()->isPluginEnabled(plugin->pluginId());
            badgeLayout->addWidget(createPluginBadge(glyph(0xE73E), tr("Installed"), pluginPanel, QColor("#16a34a")));
            badgeLayout->addWidget(createPluginBadge(glyph(0xEA3B), pluginEnabled ? tr("Enabled") : tr("Disabled"), pluginPanel, pluginEnabled ? QColor("#0078d4") : QColor("#6b7280")));
            badgeLayout->addStretch();
            pluginText->addLayout(badgeLayout);
            topLayout->addLayout(pluginText, 1);

            FluentToggleSwitch* toggle = new FluentToggleSwitch(pluginPanel);
            toggle->setChecked(pluginEnabled, false);
            QString pId = plugin->pluginId();
            connect(toggle, &FluentToggleSwitch::toggled, this, [pId](bool checked) {
                PluginManager::instance()->setPluginEnabled(pId, checked);
            });
            topLayout->addWidget(toggle, 0, Qt::AlignVCenter);

            QPushButton* settingsBtn = new QPushButton(tr("Settings"), pluginPanel);
            settingsBtn->setObjectName("pluginSettingsButton");
            settingsBtn->setFixedSize(86, 36);
            settingsBtn->setCursor(Qt::PointingHandCursor);
            connect(settingsBtn, &QPushButton::clicked, this, [plugin]() {
                plugin->onSettingsRequested();
                PluginManager::instance()->registerPluginHotkey(plugin);
            });
            topLayout->addWidget(settingsBtn, 0, Qt::AlignVCenter);
            pluginLayout->addLayout(topLayout);

            mainLayout->addWidget(pluginPanel);
            mainLayout->addSpacing(16);
        }
    }

    mainLayout->addSpacing(12);
    QLabel* availableTitle = createCardTitle(tr("Available Plugins"), content);
    mainLayout->addWidget(availableTitle);
    mainLayout->addSpacing(8);

    QWidget* comingPanel = new QWidget(content);
    comingPanel->setObjectName("comingSoonPanel");
    QHBoxLayout* comingLayout = new QHBoxLayout(comingPanel);
    comingLayout->setContentsMargins(26, 22, 26, 22);
    comingLayout->setSpacing(18);
    comingLayout->addWidget(createGlyphLabel(glyph(0xE74C), comingPanel, 34, QColor("#8b96aa")), 0, Qt::AlignVCenter);
    QVBoxLayout* comingText = new QVBoxLayout();
    comingText->setContentsMargins(0, 0, 0, 0);
    comingText->setSpacing(4);
    QLabel* comingTitle = new QLabel(tr("More Plugins Coming Soon"), comingPanel);
    comingTitle->setObjectName("rowTitle");
    QLabel* comingSubtitle = new QLabel(tr("We're working on more powerful plugins and integrations to enhance your LinDesk experience."), comingPanel);
    comingSubtitle->setObjectName("rowSubtitle");
    comingSubtitle->setWordWrap(true);
    comingText->addWidget(comingTitle);
    comingText->addWidget(comingSubtitle);
    comingLayout->addLayout(comingText, 1);
    mainLayout->addWidget(comingPanel);
    scroll->setWidget(content);
    outerLayout->addWidget(scroll);

    return page;
}

void SettingUI::setupStyles() {
    setStyleSheet(
        "SettingUI, QWidget#sidebarContainer, QWidget#rightArea { "
        "   background-color: transparent; "
        "   border: none; "
        "   color: #142033; "
        "   font-family: '" + AppFonts::uiFamily() + "', 'Segoe UI', 'Microsoft YaHei UI'; "
        "} "
        "QWidget#fluentBaseContainer { "
        "   background-color: #f9fbff; "
        "   border: 1px solid #cfd8e6; "
        "   border-radius: 12px; "
        "} "
        "SettingUI { "
        "   background-color: transparent; "
        "} "
        "QWidget#sidebarBrand { background: transparent; } "
        "QLabel#sidebarBrandTitle { font-size: 16px; font-weight: 700; color: #111827; } "
        "QLabel#sidebarBrandIcon { background: transparent; border-radius: 8px; } "
        "QWidget#sidebarContainer { "
        "   background: rgba(252, 254, 255, 0.92); "
        "   border-right: 1px solid rgba(218, 226, 238, 0.78); "
        "   border-top-left-radius: 12px; "
        "   border-bottom-left-radius: 12px; "
        "} "
        "QWidget#rightArea { "
        "   background: rgba(255, 255, 255, 0.72); "
        "   border-top-right-radius: 12px; "
        "   border-bottom-right-radius: 12px; "
        "} "
        "QWidget#settingsWindowControls { "
        "   background: transparent; "
        "} "
        "QPushButton#settingsMinBtn, QPushButton#settingsMaxBtn, QPushButton#settingsCloseBtn { "
        "   background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #26324a; "
        "} "
        "QPushButton#settingsCloseBtn { border-top-right-radius: 12px; } "
        "QPushButton#settingsMinBtn:hover, QPushButton#settingsMaxBtn:hover { background: rgba(33, 45, 69, 0.07); } "
        "QPushButton#settingsCloseBtn:hover { background: #dc2626; color: white; border-top-right-radius: 12px; } "
        "QListWidget#sidebar { "
        "   background: transparent; "
        "   outline: none; "
        "   border: none; "
        "   padding: 0; "
        "} "
        "QListWidget#sidebar::item { "
        "   border: none; "
        "   background: transparent; "
        "   padding: 0; "
        "   margin: 0; "
        "} "
        "QListWidget#sidebar::item:hover { background: transparent; } "
        "QListWidget#sidebar::item:selected { background: transparent; } "
        "QListWidget#sidebar::item:focus { background: transparent; outline: none; } "
        "QScrollBar:vertical { "
        "   border: none; background: transparent; width: 6px; margin: 12px 4px 12px 0; "
        "} "
        "QScrollBar::handle:vertical { "
        "   background: rgba(64, 76, 96, 0.18); border-radius: 3px; min-height: 24px; "
        "} "
        "QScrollBar::handle:vertical:hover { background: rgba(64, 76, 96, 0.30); } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "
        "QLabel#pageTitle { font-size: 26px; font-weight: 800; color: #101827; margin-bottom: 0; letter-spacing: 0; } "
        "QLabel#pageSubtitle { font-size: 15px; font-weight: 500; color: #66738c; margin-bottom: 8px; } "
        "QLabel#cardTitle { font-size: 16px; font-weight: 800; color: #101827; } "
        "QLabel#cardSubtitle { font-size: 14px; font-weight: 500; color: #63708a; } "
        "QLabel#rowTitle { font-size: 14px; font-weight: 800; color: #142033; } "
        "QLabel#rowSubtitle { font-size: 13px; font-weight: 500; color: #63708a; } "
        "QLabel#valueLabel { color: #63708a; font-size: 14px; font-weight: 700; } "
        "QFrame#shellExcludeBody { "
        "   background: rgba(247, 250, 255, 0.82); "
        "   border: 1px solid rgba(207, 216, 230, 0.82); "
        "   border-radius: 10px; "
        "} "
        "QWidget#shellExcludeTypePanel { background: transparent; border: none; } "
        "QWidget#shellExcludeDetail { background: transparent; border: none; } "
        "QWidget#shellExcludePane { background: transparent; border: none; } "
        "QWidget#shellExcludeTransferPane { background: transparent; border: none; } "
        "QWidget#shellExcludeActionPane { background: transparent; border: none; } "
        "QLabel#shellExcludeGroupTitle { "
        "   color: #5e6c84; "
        "   font-size: 12px; "
        "   font-weight: 800; "
        "   padding: 2px 4px; "
        "} "
        "QListWidget#shellExcludeTypeList { "
        "   background: rgba(255, 255, 255, 0.72); "
        "   border: 1px solid rgba(214, 223, 236, 0.76); "
        "   border-radius: 8px; "
        "   padding: 6px; "
        "   outline: none; "
        "} "
        "QListWidget#shellExcludeTypeList::item { "
        "   min-height: 34px; "
        "   padding: 0 10px; "
        "   border-radius: 6px; "
        "   color: #28344a; "
        "   font-size: 13px; "
        "   font-weight: 700; "
        "} "
        "QListWidget#shellExcludeTypeList::item:hover { background: #eef6ff; } "
        "QListWidget#shellExcludeTypeList::item:selected { "
        "   background: #e6f1ff; "
        "   color: #005fb8; "
        "} "
        "QListWidget#shellExcludeTreeList { "
        "   background: rgba(255, 255, 255, 0.74); "
        "   border: 1px solid rgba(214, 223, 236, 0.76); "
        "   border-radius: 8px; "
        "   padding: 6px; "
        "   outline: none; "
        "} "
        "QListWidget#shellExcludeTreeList::item { "
        "   background: transparent; "
        "   border: none; "
        "   padding: 0; "
        "} "
        "QListWidget#shellExcludeTreeList::item:selected { "
        "   background: rgba(0, 120, 212, 0.10); "
        "   border-radius: 5px; "
        "} "
        "QListWidget#shellExcludeTreeList::item:hover { "
        "   background: rgba(238, 246, 255, 0.92); "
        "   border-radius: 5px; "
        "} "
        "QLabel#shellExcludeEmptyLabel { "
        "   color: #63708a; "
        "   font-size: 13px; "
        "   font-weight: 600; "
        "   padding: 28px; "
        "} "
        "QWidget#shellExcludeRow { "
        "   background: transparent; "
        "   border: none; "
        "   border-radius: 5px; "
        "} "
        "QWidget#shellExcludeRow:hover { "
        "   background: rgba(238, 246, 255, 0.92); "
        "   border: none; "
        "} "
        "QWidget#shellExcludeRow[selected=\"true\"] { "
        "   background: rgba(0, 120, 212, 0.10); "
        "   border: none; "
        "} "
        "QLabel#shellExcludeRowIcon { "
        "   background: transparent; "
        "   border: none; "
        "} "
        "QLabel#shellExcludeBranchIcon { "
        "   background: transparent; "
        "   border: none; "
        "   color: #7a879b; "
        "   font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; "
        "   font-size: 11px; "
        "   font-weight: 700; "
        "} "
        "QLabel#shellExcludeRowTitle { font-size: 13px; font-weight: 600; color: #142033; } "
        "QLabel#shellExcludeRowSubtitle { font-size: 12px; font-weight: 600; color: #63708a; } "
        "QLabel#homeTileTitle { font-size: 15px; font-weight: 800; color: #101827; } "
        "QLabel#homeTileSubtitle { font-size: 13px; font-weight: 500; color: #56647d; } "
        "QLabel#pluginName { font-size: 19px; font-weight: 800; color: #101827; } "
        "QLabel#pluginDescription { font-size: 14px; font-weight: 500; color: #63708a; } "
        "QLabel#badgeLabel { font-size: 12px; font-weight: 700; } "
        "QLabel#learnMoreLabel { color: #0078d4; font-size: 13px; font-weight: 700; font-family: 'Segoe Fluent Icons', 'Segoe UI'; } "
        "QLabel#cardLabel { font-size: 13px; font-weight: 500; color: #1f2937; } "
        "QWidget#homeTile { "
        "   background: rgba(255, 255, 255, 0.86); "
        "   border: 1px solid rgba(203, 213, 229, 0.86); "
        "   border-radius: 10px; "
        "} "
        "QWidget#quickAction { "
        "   background: rgba(255, 255, 255, 0.52); "
        "   border: none; "
        "} "
        "QWidget#settingsPanel { "
        "   background: rgba(255, 255, 255, 0.94); "
        "   border: 1px solid rgba(203, 213, 229, 0.82); "
        "   border-radius: 13px; "
        "} "
        "QWidget#settingsInnerRow { "
        "   background: rgba(255, 255, 255, 0.62); "
        "   border: none; "
        "} "
        "QWidget#settingsRow { "
        "   background: rgba(255, 255, 255, 0.58); "
        "   border: none; "
        "} "
        "QFrame#softDivider { "
        "   background: rgba(203, 213, 229, 0.68); "
        "   border: none; "
        "} "
        "QFrame#verticalDivider { "
        "   background: rgba(203, 213, 229, 0.72); "
        "   border: none; "
        "   min-width: 1px; "
        "   max-width: 1px; "
        "} "
        "QLabel#pluginLargeIcon { "
        "   background: rgba(255, 255, 255, 0.92); "
        "   border: 1px solid #dbe4f1; "
        "   border-radius: 12px; "
        "   color: #0078d4; "
        "   min-width: 74px; "
        "   min-height: 74px; "
        "} "
        "QWidget#pluginFeatureBox { "
        "   background: rgba(255, 255, 255, 0.76); "
        "   border: 1px solid rgba(203, 213, 229, 0.72); "
        "   border-radius: 10px; "
        "} "
        "QWidget#pluginFeature { background: transparent; border: none; } "
        "QWidget#comingSoonPanel { "
        "   background: rgba(255, 255, 255, 0.44); "
        "   border: 1px dashed #c8d3e3; "
        "   border-radius: 12px; "
        "} "
        "QLabel#previewSurface { "
        "   background: #edf4ff; "
        "   border-radius: 10px; "
        "} "
        "QLineEdit#valueEdit {"
        "   background: #ffffff;"
        "   border: 1px solid #d2dbe8;"
        "   border-radius: 7px;"
        "   color: #28344a;"
        "   font-family: 'Consolas', 'Segoe UI Mono';"
        "   font-size: 13px;"
        "   font-weight: 700;"
        "   padding: 0 8px;"
        "} "
        "QPushButton#colorPickerButton {"
        "   color: white;"
        "   border-radius: 7px;"
        "   border: 2px solid rgba(255,255,255,0.96);"
        "   font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "   font-size: 15px;"
        "   background: qconicalgradient(cx:0.5, cy:0.5, angle:90, stop:0 #ff005c, stop:0.16 #ff8a00, stop:0.32 #ffe500, stop:0.48 #00d86f, stop:0.64 #00a2ff, stop:0.82 #7c3cff, stop:1 #ff005c);"
        "} "
        "QPushButton#colorPickerButton:hover { border: 2px solid #0078d4; } "
        "QPushButton#pluginSettingsButton { "
        "   background: #ffffff; "
        "   border: 1px solid #cfd8e6; "
        "   border-radius: 8px; "
        "   color: #111827; "
        "   font-size: 13px; "
        "   font-weight: 700; "
        "} "
        "QPushButton#pluginSettingsButton:hover { background: #f5f9ff; border-color: #a9c7ef; } "
        "QTabWidget#linWidgetsTabs::pane { "
        "   border: none; "
        "   background: transparent; "
        "} "
        "QTabWidget#linWidgetsTabs QTabBar::tab { "
        "   height: 36px; "
        "   min-width: 88px; "
        "   padding: 0 18px; "
        "   margin-right: 8px; "
        "   background: rgba(255, 255, 255, 0.44); "
        "   border: 1px solid rgba(214, 223, 236, 0.64); "
        "   border-radius: 18px; "
        "   color: #66738c; "
        "   font-size: 13px; "
        "   font-weight: 800; "
        "} "
        "QTabWidget#linWidgetsTabs QTabBar::tab:hover { background: #f7fbff; color: #142033; } "
        "QTabWidget#linWidgetsTabs QTabBar::tab:selected { "
        "   background: #eaf3ff; "
        "   border-color: #cfe2ff; "
        "   color: #0078d4; "
        "} "
        "QListWidget#linWidgetSettingsList { "
        "   background: transparent; "
        "   border: none; "
        "   padding: 0; "
        "   outline: none; "
        "} "
        "QListWidget#linWidgetSettingsList::item { "
        "   background: transparent; "
        "   border: none; "
        "   padding: 0; "
        "   margin: 5px 0; "
        "} "
        "QListWidget#linWidgetSettingsList::item:hover { background: transparent; } "
        "QListWidget#linWidgetSettingsList::item:selected { "
        "   background: transparent; "
        "} "
        "QWidget#linWidgetStoreRow { "
        "   background: rgba(255, 255, 255, 0.92); "
        "   border: 1px solid rgba(225, 232, 242, 0.86); "
        "   border-radius: 14px; "
        "} "
        "QWidget#linWidgetStoreRow:hover { background: #f7fbff; border-color: #cfe2ff; } "
        "QPushButton#updatePrimaryButton { "
        "   background: #0078d4; "
        "   border: 1px solid #0078d4; "
        "   border-radius: 8px; "
        "   color: #ffffff; "
        "   font-size: 13px; "
        "   font-weight: 800; "
        "   padding: 0 16px; "
        "} "
        "QPushButton#updatePrimaryButton:hover { background: #0b86e8; border-color: #0b86e8; } "
        "QPushButton#updatePrimaryButton:disabled { background: #d5dde9; border-color: #d5dde9; color: #7d8798; } "
        "QProgressBar#updateProgressBar { "
        "   background: #e6edf6; "
        "   border: none; "
        "   border-radius: 5px; "
        "   height: 10px; "
        "   text-align: center; "
        "   color: transparent; "
        "} "
        "QProgressBar#updateProgressBar::chunk { "
        "   background: #0078d4; "
        "   border-radius: 5px; "
        "} "
        "QPushButton#minBtn, QPushButton#maxBtn, QPushButton#closeBtn { "
        "   background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #26324a; "
        "} "
        "QPushButton#minBtn:hover, QPushButton#maxBtn:hover { background: rgba(33, 45, 69, 0.07); } "
        "QPushButton#closeBtn:hover { background: #dc2626; color: white; } "
        "QSlider::groove:horizontal { height: 5px; background: #d8dee8; border-radius: 3px; } "
        "QSlider::sub-page:horizontal { height: 5px; background: #0078d4; border-radius: 3px; } "
        "QSlider::handle:horizontal { background: #0078d4; border: 3px solid #ffffff; width: 15px; height: 15px; margin: -7px 0; border-radius: 9px; } "
        "QCheckBox::indicator { "
        "   width: 40px; "
        "   height: 20px; "
        "   background: #6b7280; "
        "   border: 1px solid #6b7280; "
        "   border-radius: 10px; "
        "} "
        "QCheckBox::indicator:checked { "
        "   background-color: #0078d4; "
        "   border-color: #0078d4; "
        "} "
    );
}

// Legacy mouse events moved to base class logic

// Legacy mouse events removed - handled by FluentBaseWidget

void SettingUI::setTargetBox(LinBox* target) {
    m_targetBox = target;
    if (target && m_sidebar) {
        m_sidebar->setCurrentRow(3);
    }
    if (m_pageCache[3]) {
        updateControlsFromTarget();
    }
}

void SettingUI::updateControlsFromTarget() {
    LinBox* sourceBox = controlSourceBox();
    if (!sourceBox) {
        return;
    }
    if (m_opacitySlider) {
        m_opacitySlider->setValue(qBound(m_opacitySlider->minimum(),
                                         qRound(sourceBox->backgroundOpacity() * 100.0f),
                                         m_opacitySlider->maximum()));
        m_opacityLabelValue->setText(QString("%1%").arg(m_opacitySlider->value()));
    }
    if (m_glassToggle) {
        m_glassToggle->setChecked(sourceBox->glassEffect());
    }
    if (m_boxColorValueEdit) {
        m_boxColorValueEdit->setText(sourceBox->backgroundColor().name().toUpper());
    }
    updateColorPresetIndicators(m_boxColorPresetsRow, sourceBox->backgroundColor());
    // Sync preview state from target box
    m_previewColor = sourceBox->backgroundColor();
    m_previewOpacity = sourceBox->backgroundOpacity();
    m_previewGlass = sourceBox->glassEffect();
    updatePreviewStyle();
}

LinBox* SettingUI::controlSourceBox() const {
    if (m_targetBox) {
        return m_targetBox.data();
    }

    for (const QPointer<LinBox>& boxPtr : m_allBoxes) {
        if (LinBox* box = boxPtr.data()) {
            return box;
        }
    }

    for (const QPointer<LinBox>& boxPtr : BoxManager::instance()->allBoxes()) {
        if (LinBox* box = boxPtr.data()) {
            return box;
        }
    }
    return nullptr;
}

void SettingUI::updateDesktopSelectionPreview()
{
    if (!m_desktopSelectionPreview) {
        return;
    }

    const QSize previewSize = m_desktopSelectionPreview->contentsRect().size();
    if (previewSize.width() < 2 || previewSize.height() < 2) {
        return;
    }
    QPixmap canvas(previewSize);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const int minSide = qMin(previewSize.width(), previewSize.height());
    const int cornerRadius = qBound(6, minSide / 18, 14);
    const QRect imageRect = centeredPreviewImageRect(previewSize);
    QPainterPath clipPath;
    clipPath.addRoundedRect(imageRect.adjusted(0, 0, -1, -1), cornerRadius, cornerRadius);
    painter.setClipPath(clipPath);

    QRect contentRect = imageRect;
    QPixmap bgPixmap(":/Assets/DesktopLivePreview.png");
    if (!bgPixmap.isNull()) {
        const QPixmap scaledBg = bgPixmap.scaled(imageRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap(imageRect, scaledBg);
        painter.fillRect(contentRect, QColor(0, 0, 0, 22));
    } else {
        QLinearGradient background(imageRect.topLeft(), imageRect.bottomRight());
        background.setColorAt(0.0, QColor(221, 235, 255));
        background.setColorAt(0.52, QColor(241, 247, 255));
        background.setColorAt(1.0, QColor(178, 205, 243));
        painter.fillRect(imageRect, background);

        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 5; ++i) {
            QColor ribbon(255, 255, 255, 72 - i * 8);
            painter.setBrush(ribbon);
            QPainterPath ribbonPath;
            const qreal y = imageRect.top() + imageRect.height() * (0.18 + i * 0.12);
            ribbonPath.moveTo(imageRect.left() - 60, y + 70);
            ribbonPath.cubicTo(imageRect.left() + imageRect.width() * 0.25, y - 35,
                               imageRect.left() + imageRect.width() * 0.52, y + 90,
                               imageRect.right() + 70, y - 15);
            ribbonPath.lineTo(imageRect.right() + 70, y + 54);
            ribbonPath.cubicTo(imageRect.left() + imageRect.width() * 0.54, y + 145,
                               imageRect.left() + imageRect.width() * 0.20, y + 20,
                               imageRect.left() - 60, y + 118);
            ribbonPath.closeSubpath();
            painter.fillPath(ribbonPath, ribbon);
        }
        painter.fillRect(imageRect, QColor(0, 0, 0, 48));
    }

    const QColor fillColor = desktopSelectionFillColor(m_selectionPreviewColor);
    const QColor borderColor = desktopSelectionBorderColor(m_selectionPreviewColor);
    const QRect selectionRect(contentRect.left() + qRound(contentRect.width() * 0.36),
                              contentRect.top() + qRound(contentRect.height() * 0.12),
                              qRound(contentRect.width() * 0.32),
                              qRound(contentRect.height() * 0.64));
    painter.fillRect(selectionRect, fillColor);
    const int selectionBorderWidth = qBound(1, minSide / 90, 3);
    painter.setPen(QPen(borderColor, selectionBorderWidth));
    painter.drawRect(selectionRect.adjusted(0, 0, -1, -1));

    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 32), 1));
    painter.drawRoundedRect(imageRect.adjusted(0, 0, -1, -1), cornerRadius, cornerRadius);

    painter.setPen(QPen(QColor(0, 0, 0, 38), 1));
    painter.setBrush(QColor(255, 255, 255, 242));
    const int handle = qBound(7, minSide / 14, 14);
    const int handleRadius = qBound(2, handle / 3, 5);
    const QVector<QPoint> handlePoints = {
        selectionRect.topLeft(),
        QPoint(selectionRect.center().x(), selectionRect.top()),
        selectionRect.topRight(),
        QPoint(selectionRect.left(), selectionRect.center().y()),
        QPoint(selectionRect.right(), selectionRect.center().y()),
        selectionRect.bottomLeft(),
        QPoint(selectionRect.center().x(), selectionRect.bottom()),
        selectionRect.bottomRight()
    };
    for (const QPoint& point : handlePoints) {
        painter.drawRoundedRect(QRect(point.x() - handle / 2, point.y() - handle / 2, handle, handle), handleRadius, handleRadius);
    }

    painter.end();
    m_desktopSelectionPreview->setPixmap(canvas);
}

void SettingUI::updatePreviewStyle() {
    if (!m_previewBackground) return;

    QSize previewSize = m_previewBackground->contentsRect().size();
    if (previewSize.width() < 2 || previewSize.height() < 2) {
        previewSize = QSize(540, 202);
    }
    const int canvasW = previewSize.width();
    const int canvasH = previewSize.height();
    const QRect imageRect = centeredPreviewImageRect(previewSize);

    QPixmap canvas(canvasW, canvasH);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clipPath;
    clipPath.addRoundedRect(imageRect.adjusted(0, 0, -1, -1), 10, 10);
    painter.setClipPath(clipPath);

    QPixmap bgPixmap(":/Assets/BoxPreview.png");
    if (!bgPixmap.isNull()) {
        painter.drawPixmap(imageRect, bgPixmap.scaled(imageRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
        QLinearGradient grad(imageRect.topLeft(), imageRect.bottomRight());
        grad.setColorAt(0, QColor(140, 205, 255));
        grad.setColorAt(0.52, QColor(102, 178, 226));
        grad.setColorAt(1, QColor(34, 118, 172));
        painter.fillRect(imageRect, grad);
    }

    const int boxMaxW = qMin(430, qMax(1, imageRect.width() - 24));
    const int boxMinW = qMin(220, boxMaxW);
    const int boxMaxH = qMin(160, qMax(1, imageRect.height() - 18));
    const int boxMinH = qMin(92, boxMaxH);
    const int boxW = qBound(boxMinW, imageRect.width() * 58 / 100, boxMaxW);
    const int boxH = qBound(boxMinH, imageRect.height() * 58 / 100, boxMaxH);
    const int boxX = imageRect.left() + (imageRect.width() - boxW) / 2;
    const int boxY = imageRect.top() + (imageRect.height() - boxH) / 2;
    const int titleBarH = 38;

    QRect boxRect(boxX, boxY, boxW, boxH);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 23, 42, 54));
    painter.drawRoundedRect(boxRect.adjusted(0, 8, 0, 12), 12, 12);

    QColor boxColor = m_previewColor.isValid() ? m_previewColor : QColor(40, 40, 40);
    boxColor.setAlpha(m_previewGlass ? 156 : qBound(70, static_cast<int>(m_previewOpacity * 255), 220));
    painter.setBrush(boxColor);
    painter.drawRoundedRect(boxRect, 12, 12);

    QColor titleColor = boxColor.darker(128);
    titleColor.setAlpha(qBound(120, boxColor.alpha() + 34, 235));
    QPainterPath titlePath;
    titlePath.addRoundedRect(QRect(boxX, boxY, boxW, titleBarH), 12, 12);
    titlePath.addRect(boxX, boxY + titleBarH - 12, boxW, 12);
    painter.fillPath(titlePath, titleColor);

    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.drawRoundedRect(boxRect.adjusted(0, 0, -1, -1), 12, 12);

    QFont iconFont("Segoe Fluent Icons");
    if (!QFontInfo(iconFont).exactMatch()) {
        iconFont.setFamily("Segoe MDL2 Assets");
    }
    iconFont.setPixelSize(15);
    painter.setFont(iconFont);
    painter.setPen(QColor(255, 255, 255, 210));
    painter.drawText(QRect(boxX + 12, boxY + 7, 24, 24), Qt::AlignCenter, glyph(0xE70E));
    painter.drawText(QRect(boxX + boxW - 38, boxY + 7, 24, 24), Qt::AlignCenter, glyph(0xE712));

    painter.setPen(Qt::white);
    QFont titleFont(AppFonts::uiFamily(), 11, QFont::Bold);
    painter.setFont(titleFont);
    QFontMetrics fm(titleFont);
    QString title = tr("Preview Box");
    QString elidedTitle = fm.elidedText(title, Qt::ElideRight, boxW - 60);
    painter.drawText(QRect(boxX + 44, boxY + 7, boxW - 88, 24), Qt::AlignCenter, elidedTitle);

    painter.setPen(QColor(255, 255, 255, 200));
    iconFont.setPixelSize(18);
    painter.setFont(iconFont);

    const int iconSize = 48;
    const int iconSpacing = 62;
    const int startX = boxX + (boxW - iconSpacing * 3 - iconSize) / 2;
    const int iconY = boxY + titleBarH + (boxH - titleBarH - iconSize) / 2;

    QStringList iconSymbols = {glyph(0xE7C3), glyph(0xE8A5), glyph(0xE91B), glyph(0xE721)};
    for (int i = 0; i < iconSymbols.size(); ++i) {
        int ix = startX + i * iconSpacing;
        if (ix + iconSize <= boxX + boxW - 10) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 30));
            painter.drawRoundedRect(ix, iconY, iconSize, iconSize, 10, 10);
            painter.setPen(QColor(255, 255, 255, 200));
            painter.setFont(iconFont);
            painter.drawText(QRect(ix, iconY, iconSize, iconSize), Qt::AlignCenter, iconSymbols[i]);
        }
    }

    painter.setClipping(false);
    painter.setPen(QPen(QColor(0, 0, 0, 36), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(imageRect.adjusted(0, 0, -1, -1), 10, 10);
    painter.end();

    m_previewBackground->setPixmap(canvas);
}

void SettingUI::closeEvent(QCloseEvent* event) {
    // 2. Persistence - Save Window Size only
    QString configPath = settingsConfigPath();
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue("SettingUI/Size", size());
    settings.sync();

    const bool forceQuit = QCoreApplication::instance() &&
        QCoreApplication::instance()->property("LinDeskForceQuit").toBool();
    if (forceQuit || m_isQuittingFromTray) {
        event->accept();
        QWidget::closeEvent(event);
        return;
    }

    if (shouldMinimizeToTray() && m_trayIcon) {
        event->ignore();
        hide();
        m_trayIcon->show();
        if (!m_hasShownTrayHint) {
            m_trayIcon->showMessage(tr("LinDesk"), tr("Settings is still available in the system tray."), QSystemTrayIcon::Information, 3000);
            m_hasShownTrayHint = true;
        }
        return;
    }

    event->ignore();
    showMinimized();
}

void SettingUI::changeEvent(QEvent* event) {
    FluentBaseWidget::changeEvent(event);
    if (event && event->type() == QEvent::WindowStateChange) {
        updateWindowControlButtons();
    }
}

void SettingUI::showEvent(QShowEvent* event) {
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    FluentBaseWidget::showEvent(event);
    scheduleResizeRefresh(0);
}

void SettingUI::resizeEvent(QResizeEvent* event)
{
    FluentBaseWidget::resizeEvent(event);
    updateSidebarItemHeights();
    scheduleResizeRefresh();
}

void SettingUI::scheduleResizeRefresh(int delayMs)
{
    if (!m_resizeRefreshTimer) {
        m_resizeRefreshTimer = new QTimer(this);
        m_resizeRefreshTimer->setSingleShot(true);
        connect(m_resizeRefreshTimer, &QTimer::timeout, this, &SettingUI::refreshAfterResize);
    }

    m_resizeRefreshTimer->start(delayMs);
}

void SettingUI::refreshAfterResize()
{
    updateLivePreviewHeights();
    syncScrollablePageWidths();
    updateDesktopSelectionPreview();
    updatePreviewStyle();
}

int SettingUI::livePreviewHeightForWindow() const
{
    constexpr int kBasePreviewHeight = 220;
    constexpr int kTallWindowThreshold = 760;
    constexpr int kMaxExtraHeight = 36;

    const int extraHeight = qBound(0, (height() - kTallWindowThreshold) / 8, kMaxExtraHeight);
    return kBasePreviewHeight + extraHeight;
}

void SettingUI::updateLivePreviewHeights()
{
    const int previewHeight = livePreviewHeightForWindow();

    if (m_desktopPreviewContainer && m_desktopPreviewContainer->height() != previewHeight) {
        m_desktopPreviewContainer->setFixedHeight(previewHeight);
    }
    if (m_boxPreviewContainer && m_boxPreviewContainer->height() != previewHeight) {
        m_boxPreviewContainer->setFixedHeight(previewHeight);
    }
}

void SettingUI::updateWindowControlButtons()
{
    if (m_settingsMaxBtn) {
        m_settingsMaxBtn->setText((windowState() & Qt::WindowMaximized)
            ? QString::fromUtf8(u8"\uE923")
            : QString::fromUtf8(u8"\uE922"));
    }
}

void SettingUI::syncScrollablePageWidths()
{
    if (!m_contentStack) {
        return;
    }

    QWidget* current = m_contentStack->currentWidget();
    if (!current) {
        return;
    }

    QScrollArea* scroll = qobject_cast<QScrollArea*>(current);
    if (!scroll || !scroll->widget() || !scroll->viewport()) {
        return;
    }

    QWidget* content = scroll->widget();
    const int viewportWidth = scroll->viewport()->width();
    if (viewportWidth <= 0) {
        return;
    }

    content->setMinimumWidth(0);
    content->setMaximumWidth(QWIDGETSIZE_MAX);
    content->updateGeometry();
    if (QLayout* layout = content->layout()) {
        layout->activate();
    }
}

void SettingUI::updateSidebarItemHeights()
{
    if (!m_sidebar || m_sidebar->count() <= 0) {
        return;
    }

    const int itemCount = m_sidebar->count();
    const int totalSpacing = qMax(0, itemCount - 1) * m_sidebar->spacing();
    const int availableHeight = qMax(0, m_sidebar->viewport()->height() - totalSpacing - 2);
    const int itemHeight = qBound(48, availableHeight / itemCount, 52);

    for (int i = 0; i < itemCount; ++i) {
        if (QListWidgetItem* item = m_sidebar->item(i)) {
            item->setSizeHint(QSize(230, itemHeight));
        }
    }
}

// Legacy paint event removed - handled by FluentBaseWidget

// NativeEvent has been gracefully moved to FluentBaseWidget
