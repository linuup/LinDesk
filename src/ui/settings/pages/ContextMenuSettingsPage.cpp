#include "ui/settings/pages/ContextMenuSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "services/ShellMenuExclusionStore.h"
#include "desktop/RightMenu.h"
#include "ui/Fluent/FluentButton.h"
#include "ui/Fluent/FluentInput.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentCheckBox.h"
#include "ui/Fluent/FluentComboBox.h"
#include "ui/Fluent/FluentToggleSwitch.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QSettings>
#include <QTimer>
#include <QtWin>

#include <vector>
#include <string>
#include <memory>
#include <algorithm>

using namespace SettingsUICommon;

static QStringList shellMenuCustomExtensionKeys()
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    QStringList keys = settings.value(QStringLiteral("ShellMenuExclusions/CustomExtensionKeys")).toStringList();
    keys.removeDuplicates();
    keys.removeAll(QString());
    return keys;
}

static void saveShellMenuCustomExtensionKeys(QStringList keys)
{
    keys.removeDuplicates();
    keys.removeAll(QString());
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ShellMenuExclusions/CustomExtensionKeys"), keys);
    settings.sync();
}

static bool isUsableShellMenuBitmap(HBITMAP bitmap)
{
    const quintptr rawValue = reinterpret_cast<quintptr>(bitmap);
    return bitmap && rawValue > 16 && rawValue != static_cast<quintptr>(-1);
}

static QIcon shellMenuBitmapIcon(HBITMAP bitmap)
{
    if (!isUsableShellMenuBitmap(bitmap)) {
        return QIcon();
    }
    const QPixmap pixmap = QtWin::fromHBITMAP(bitmap, QtWin::HBitmapAlpha);
    return pixmap.isNull() ? QIcon() : QIcon(pixmap);
}

static QStringList wideVectorToStringList(const std::vector<std::wstring>& values)
{
    QStringList result;
    for (const std::wstring& value : values) {
        result.append(QString::fromStdWString(value));
    }
    return result;
}

static ShellMenuExclusionEntry shellMenuEntryFromItem(QListWidgetItem* item)
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

static bool shellMenuEntryMatches(const std::vector<ShellMenuExclusionEntry>& entries,
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

static QString shellMenuDisplayText(QString text)
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

static QString shellMenuTypeTitle(const QList<ShellMenuRuleTypeOption>& options, const QString& key)
{
    for (const ShellMenuRuleTypeOption& option : options) {
        if (option.key == key) {
            return option.title;
        }
    }
    return key;
}

static int shellMenuDesktopPriority(const ShellMenuSnapshotItem& item)
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

static void prioritizeDesktopMenuItems(std::vector<ShellMenuSnapshotItem>& items)
{
    std::stable_sort(items.begin(), items.end(), [](const ShellMenuSnapshotItem& left, const ShellMenuSnapshotItem& right) {
        return shellMenuDesktopPriority(left) < shellMenuDesktopPriority(right);
    });
}

ContextMenuSettingsPage::ContextMenuSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    initUI();
}

void ContextMenuSettingsPage::ensureInitialSelection()
{
    if (m_shellTypeList && !m_shellTypeList->currentItem() && m_shellTypeList->count() > 0) {
        m_shellTypeList->setCurrentRow(0);
    }
}

void ContextMenuSettingsPage::initUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    QLabel* pageTitle = createPageTitle(tr("Context Menu"), this);
    pageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(pageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    QWidget* filterTogglePanel = createPanel(this);
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

    QWidget* shellExcludePanel = createPanel(this);
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
                QListWidgetItem* item = new QListWidgetItem(shellMenuTreeList);
                item->setData(Qt::UserRole, storedRawText);
                item->setData(Qt::UserRole + 1, normalizedText);
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

        const QString newKey = QStringLiteral("file_ext:") + extension;
        QStringList customKeys = shellMenuCustomExtensionKeys();
        if (!customKeys.contains(newKey)) {
            customKeys.append(newKey);
            saveShellMenuCustomExtensionKeys(customKeys);
        }

        if (!reuseKey.isEmpty()) {
            const auto sourceEntries = ShellMenuExclusionStore::load(reuseKey);
            if (!sourceEntries.empty()) {
                ShellMenuExclusionStore::merge(newKey, sourceEntries);
            }
        }

        ruleTypeOptions->append({ extension, newKey });
        QListWidgetItem* newItem = addTypeItem(customTypeList, extension, newKey);
        customTypeList->setCurrentItem(newItem);
        shellExcludeStatusLabel->setText(tr("Added extension type: %1").arg(extension));
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
        RightMenu::getInstance()->invalidateCustomMenuCaches();

        for (int i = 0; i < ruleTypeOptions->size(); ++i) {
            if (ruleTypeOptions->at(i).key == key) {
                ruleTypeOptions->removeAt(i);
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
}
