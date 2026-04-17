#pragma once
#include "RightMenu.h"
#include "Fluent/FluentDialog.h"
#include <QMenu>
#include <QDebug>
#include "../SystemApi/SystemApi.h"
#include <QProcess>
#include <qstandardpaths.h>
#include "../SystemApi/Tool.h"
#include <QtWinExtras/QtWin>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QStyle>

namespace {
constexpr DWORD kClipboardPreferredDropEffectMove = 0x00000002;
}
  
QMenu* RightMenu::createSortBySubMenu()
{
    QMenu* sortByMenu = new QMenu("Sort by", m_customMenu);
    QActionGroup* sortGroup = new QActionGroup(sortByMenu);
    
    // 2.1-Name
    this->m_sortByNameAction = new QAction("Name", sortByMenu);
    m_sortByNameAction->setCheckable(true);
    m_sortByNameAction->setActionGroup(sortGroup);
    connect(m_sortByNameAction, &QAction::triggered, this, &RightMenu::onSortByName);
    sortByMenu->addAction(m_sortByNameAction);
    
    // 2.2-Size
    this->m_sortBySizeAction = new QAction("Size", sortByMenu);
    m_sortBySizeAction->setCheckable(true);
    m_sortBySizeAction->setActionGroup(sortGroup);
    connect(m_sortBySizeAction, &QAction::triggered, this, &RightMenu::onSortBySize);
    sortByMenu->addAction(m_sortBySizeAction);
    
    // 2.3-Item type
    this->m_sortByTypeAction = new QAction("Item type", sortByMenu);
    m_sortByTypeAction->setCheckable(true);
    m_sortByTypeAction->setActionGroup(sortGroup);
    connect(m_sortByTypeAction, &QAction::triggered, this, &RightMenu::onSortByType);
    sortByMenu->addAction(m_sortByTypeAction);
    
    // 2.4-Date modified
    this->m_sortByDateAction = new QAction("Date modified", sortByMenu);
    m_sortByDateAction->setCheckable(true);
    m_sortByDateAction->setActionGroup(sortGroup);
    connect(m_sortByDateAction, &QAction::triggered, this, &RightMenu::onSortByDate);
    sortByMenu->addAction(m_sortByDateAction);
    
    // 不默认选中任何选项（Default 状态下均不勾选）
    
    return sortByMenu;
}

QMenu* RightMenu::createViewSubMenu()
{
    QMenu* viewMenu = new QMenu("View", m_customMenu);
    QActionGroup* viewGroup = new QActionGroup(viewMenu);

    // 2.1-Large icons
    this->m_largeIconsAction = new QAction("Large icons", viewMenu);
    m_largeIconsAction->setCheckable(true);
    m_largeIconsAction->setActionGroup(viewGroup);
    connect(m_largeIconsAction, &QAction::triggered, this, &RightMenu::onLargeIcons);
    viewMenu->addAction(m_largeIconsAction);

    //1.2 Medium icons
    this->m_mediumIconsAction = new QAction("Medium icons", viewMenu);
    m_mediumIconsAction->setCheckable(true);
    m_mediumIconsAction->setActionGroup(viewGroup);
    connect(m_mediumIconsAction, &QAction::triggered, this, &RightMenu::onMediumIcons);
    viewMenu->addAction(m_mediumIconsAction);

    //1.3 Small icons
    this->m_smallIconsAction = new QAction("Small icons", viewMenu);
    m_smallIconsAction->setCheckable(true);
    m_smallIconsAction->setActionGroup(viewGroup);
    connect(m_smallIconsAction, &QAction::triggered, this, &RightMenu::onSmallIcons);
    viewMenu->addAction(m_smallIconsAction);

    //1.4 Separator
    viewMenu->addSeparator();

    //1.5 Auto arrange icons
    this->m_autoArrangeIconsAction = new QAction("Auto arrange icons", viewMenu);
    m_autoArrangeIconsAction->setCheckable(true);
    connect(m_autoArrangeIconsAction, &QAction::triggered, this, &RightMenu::onAutoArrangeIcons);
    viewMenu->addAction(m_autoArrangeIconsAction);

    //1.6 Align icons to grid
    this->m_alignIconsToGridAction = new QAction("Align icons to grid", viewMenu);
    m_alignIconsToGridAction->setCheckable(true);
    //m_alignIconsToGridAction->setChecked(true); // 默认开启网格对齐
    connect(m_alignIconsToGridAction, &QAction::triggered, this, &RightMenu::onAlignIconsToGrid);
    viewMenu->addAction(m_alignIconsToGridAction);

    //1.4 Separator
    viewMenu->addSeparator();

    //1.7 Show desktop icons
    this->m_showDesktopIconsAction = new QAction("Show desktop icons", viewMenu);
    m_showDesktopIconsAction->setCheckable(true);
    connect(m_showDesktopIconsAction, &QAction::triggered, this, &RightMenu::onShowDesktopIcons);
    viewMenu->addAction(m_showDesktopIconsAction);

    //1.8 Double-click to hide desktop icons
    this->m_doubleClickedHideIconsAction = new QAction("Double-click to hide icons", viewMenu);
    m_doubleClickedHideIconsAction->setCheckable(true);
    connect(m_doubleClickedHideIconsAction, &QAction::triggered, this, &RightMenu::onDoubleClickedHideIcons);
    viewMenu->addAction(m_doubleClickedHideIconsAction);

    return viewMenu;
}

QMenu* RightMenu::createUndoDeleteMenu() const
{
    return nullptr;
}

void RightMenu::initializeCustomMenu()
{
    m_customMenu = new QMenu();
    
    // 1-View 子菜单
    QMenu* viewMenu = createViewSubMenu();
    m_customMenu->addMenu(viewMenu);
    
    // 2-Sort by 子菜单
    QMenu* sortByMenu = createSortBySubMenu();
    m_customMenu->addMenu(sortByMenu);
    
    // 3-Refresh
    QAction* refreshAction = new QAction("Refresh", m_customMenu);
    connect(refreshAction, &QAction::triggered, this, &RightMenu::onRefresh);
    m_customMenu->addAction(refreshAction);
    
    // 4-分割线
    m_customMenu->addSeparator();
    
    // 5-Paste
    m_pasteAction = new QAction("Paste", m_customMenu);
    m_pasteAction->setEnabled(false);
    connect(m_pasteAction, &QAction::triggered, this, &RightMenu::onPaste);
    m_customMenu->addAction(m_pasteAction);

    pasteShortcutAction = new QAction("Paste shortcut", m_customMenu);
    pasteShortcutAction->setEnabled(false);
    connect(pasteShortcutAction, &QAction::triggered, this, &RightMenu::onPasteShortcut);
    m_customMenu->addAction(pasteShortcutAction);

    m_customMenu->addSeparator();
    QPixmap gridPix(16, 16);
    gridPix.fill(Qt::transparent);
    {
        QPainter p(&gridPix);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 使用更粗的线条
        p.setPen(QPen(QColor(0, 120, 212), 2.0));
        p.setBrush(Qt::NoBrush);

        // 绘制加号
        p.drawLine(3, 8, 13, 8);  // 水平线
        p.drawLine(8, 3, 8, 13);  // 垂直线
    }
    // 获取系统文件夹图标
    QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);

    // 转换为 16x16 像素图
    QPixmap pixmap = folderIcon.pixmap(16, 16);

    const QIcon linDeskIcon(":/Assets/icon.png");
    QMenu* linDeskMenu = new QMenu("LinDesk", m_customMenu);

    //linDeskMenu->setIcon(QIcon::fromTheme("application-x-executable"));
    linDeskMenu->setIcon(pixmap);
    // 7-Create Box
    m_createBoxAction = new QAction("New Box", m_customMenu);
    
    // Create a grid icon for shell menu integration
    
    m_createBoxAction->setIcon(QIcon(gridPix));

    connect(m_createBoxAction, &QAction::triggered, this, &RightMenu::onCreateBox);
    m_customMenu->addAction(m_createBoxAction);

    // 7.5-Organize Desktop
    QAction* organizeDesktopAction = new QAction("Organize Desktop", linDeskMenu);
    
    connect(organizeDesktopAction, &QAction::triggered, this, &RightMenu::onOrganizeDesktop);
    linDeskMenu->addAction(organizeDesktopAction);
    organizeDesktopAction->setIcon(gridPix);
    // 9.1-Setting
    m_linDeskAction = new QAction("Setting", linDeskMenu);
    m_linDeskAction->setIcon(linDeskIcon);
    connect(m_linDeskAction, &QAction::triggered, this, &RightMenu::onLinDesk);
    linDeskMenu->addAction(m_linDeskAction);
    
    // 8-Exit
    QAction* exitAction = new QAction("Exit LinDesk", linDeskMenu);
    exitAction->setIcon(QIcon(":/Assets/icon.png"));
    connect(exitAction, &QAction::triggered, this, &RightMenu::onQuitApp);
    linDeskMenu->addAction(exitAction);

    m_customMenu->addMenu(linDeskMenu);

    m_customMenu->setFixedWidth(150);
    m_customMenu->setStyleSheet("QMenu { font-weight: bold; font-family: 'Segoe UI', sans-serif; }");
}

void RightMenu::onLargeIcons() { m_largeIconsAction->setChecked(true); emit LargeIconsTriggered(); }
void RightMenu::onMediumIcons() { m_mediumIconsAction->setChecked(true); emit MediumIconsTriggered(); }
void RightMenu::onSmallIcons() { m_smallIconsAction->setChecked(true); emit SmallIconsTriggered(); }
void RightMenu::onAutoArrangeIcons() { emit AutoArrangeIconsTriggered(); }
void RightMenu::onAlignIconsToGrid() { emit AlignIconsToGridTriggered(); }
void RightMenu::onShowDesktopIcons() { emit ShowDesktopIconsTriggered(); }
void RightMenu::onDoubleClickedHideIcons() {emit DoubleClickedHideIconsTriggered();}

void RightMenu::onSortByName() { m_sortByNameAction->setChecked(true); emit sortByNameTriggered(); }
void RightMenu::onSortBySize() { m_sortBySizeAction->setChecked(true); emit sortBySizeTriggered(); }
void RightMenu::onSortByType() { m_sortByTypeAction->setChecked(true); emit sortByTypeTriggered(); }
void RightMenu::onSortByDate() { m_sortByDateAction->setChecked(true); emit sortByDateTriggered(); }

void RightMenu::onRefresh() { emit refreshTriggered(); }
void RightMenu::onPaste() { emit pasteTriggered(); }
void RightMenu::onPasteShortcut() { emit pasteShortcutTriggered(); }
void RightMenu::onCreateBox() { emit createBoxTriggered(); }
void RightMenu::onOrganizeDesktop() { emit organizeDesktopTriggered(); }
void RightMenu::onQuitApp() { emit quitAppTriggered(); }
void RightMenu::onLinDesk() { emit linDeskTriggered(); }

bool RightMenu::clipboardHasFileItems() const
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    const QMimeData* mimeData = clipboard->mimeData();
    if (mimeData && mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                return true;
            }
        }
    }

    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(nullptr)) {
        return false;
    }

    bool hasFiles = false;
    if (const HANDLE clipboardData = GetClipboardData(CF_HDROP)) {
        const HDROP dropHandle = static_cast<HDROP>(clipboardData);
        hasFiles = DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0) > 0;
    }

    CloseClipboard();
    return hasFiles;
}

bool RightMenu::clipboardContainsCutItems() const
{
    const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (!format || !IsClipboardFormatAvailable(format) || !OpenClipboard(nullptr)) {
        return false;
    }

    bool isCutOperation = false;
    if (const HANDLE clipboardData = GetClipboardData(format)) {
        const void* locked = GlobalLock(clipboardData);
        if (locked) {
            const DWORD effect = *static_cast<const DWORD*>(locked);
            isCutOperation = (effect & kClipboardPreferredDropEffectMove) != 0;
            GlobalUnlock(clipboardData);
        }
    }

    CloseClipboard();
    return isCutOperation;
}

void RightMenu::updatePasteActionsState()
{
    const bool hasFileItems = clipboardHasFileItems();
    const bool isCutOperation = hasFileItems && clipboardContainsCutItems();

    if (m_pasteAction) {
        m_pasteAction->setEnabled(hasFileItems);
    }
    if (pasteShortcutAction) {
        // Match Explorer behavior more closely: shortcut creation is disabled for cut items.
        pasteShortcutAction->setEnabled(hasFileItems && !isCutOperation);
    }
}

void RightMenu::showMenu(QPoint pos, ListItem *listitem) const{
	ShellContextMenu scm;
	POINT p = { pos.x(),pos.y() };
	const POINT* position = &p;
	if (this->fileType == ObjectType::BackGround) {
		const_cast<RightMenu*>(this)->updatePasteActionsState();
		scm.DisplayDesktopContextMenu();

		// 将自定义菜单项注册到Shell背景菜单中
		if (m_customMenu) {
			std::vector<HBITMAP> tempBitmaps; // To be cleaned up after ShowMenu
			std::function<std::vector<ShellContextMenu::CustomMenuItem>(const QList<QAction*>&)> convertActions = 
				[&](const QList<QAction*>& actions) {
				std::vector<ShellContextMenu::CustomMenuItem> items;
				for (QAction* action : actions) {
					ShellContextMenu::CustomMenuItem item;
					if (action->isSeparator()) {
						item.isSeparator = true;
					} else {
						item.text = action->text().toStdWString();
						// Convert icon to HBITMAP with alpha.
						// For submenu entries, prefer the QAction icon and fall back to the submenu icon itself.
						QIcon icon = action->icon();
						if (icon.isNull() && action->menu()) {
							icon = action->menu()->icon();
						}
						if (!icon.isNull()) {
							QPixmap pix = icon.pixmap(16, 16);
							HBITMAP hBmp = QtWin::toHBITMAP(pix, QtWin::HBitmapAlpha);
							item.hBitmap = hBmp;
							tempBitmaps.push_back(hBmp);
						}

						if (action->menu()) {
							// 递归转换子菜单
							item.children = convertActions(action->menu()->actions());
						} else {
							// 普通项，设置触发回调
							item.checkable = action->isCheckable();
							item.checked = action->isChecked();
							item.enabled = action->isEnabled();
							QAction* capturedAction = action;
							item.callback = [capturedAction]() {
								capturedAction->trigger();
							};
						}
					}
					items.push_back(item);
				}
				return items;
			};

			scm.setCustomMenuItems(convertActions(m_customMenu->actions()));
			scm.ShowMenu(this->hwnd, position, nullptr);
			
			// Clean up bitmpas
			for (HBITMAP h : tempBitmaps) {
				DeleteObject(h);
			}
			return;
		}
	}else if (this->fileType == ObjectType::File) {
		scm.setOperationType(ShellContextMenu::OperationType::file);
	}else if (this->fileType == ObjectType::Folder_ObjectType || this->fileType == ObjectType::DiskLetter) {
		scm.setOperationType(ShellContextMenu::OperationType::folder);
	}else if (this->fileType == ObjectType::SpecialFolder) {
		scm.setOperationType(ShellContextMenu::OperationType::specialFolder);
	}
	scm.DisplayItemContextMenu(listitem->getItemData().pidl());
	scm.ShowMenu(this->hwnd, position, nullptr);
}

void RightMenu::showMultObjectMenu(QPoint pos, vector<ListItem*> items) const {
	vector<LPITEMIDLIST> pidls;
	for (auto it = items.begin(); it != items.end(); it++) {
		ListItem* item = *it;
		auto pidl = item->getItemData().pidl();
		pidls.push_back(pidl);
	}
	POINT p = { pos.x(),pos.y() };
	const POINT* position = &p;
	ShellContextMenu scm;
	scm.DisplayMultipleContextMenu(pidls);
	scm.ShowMenu(this->hwnd, position, nullptr);
}
const wchar_t* RightMenu::objPath() const
{
	return reinterpret_cast<const wchar_t*>(currentObjPath.utf16());
}

const wchar_t* RightMenu::objPath(QString str) const
{
	return reinterpret_cast<const wchar_t*>(str.utf16());
}

void RightMenu::setLargeIconsChecked(bool checked)
{
    if (checked) {
        m_largeIconsAction->setChecked(true);
        m_mediumIconsAction->setChecked(false);
        m_smallIconsAction->setChecked(false);
    }
}

void RightMenu::setMediumIconsChecked(bool checked)
{
    if (checked) {
        m_largeIconsAction->setChecked(false);
        m_mediumIconsAction->setChecked(true);
        m_smallIconsAction->setChecked(false);
    }
}

void RightMenu::setSmallIconsChecked(bool checked)
{
    if (checked) {
        m_largeIconsAction->setChecked(false);
        m_mediumIconsAction->setChecked(false);
        m_smallIconsAction->setChecked(true);
    }
}

void RightMenu::setShowDesktopIconsChecked(bool checked)
{
    if (checked) {
        m_showDesktopIconsAction->setChecked(true);
    } else {
        m_showDesktopIconsAction->setChecked(false);
    }
}

void RightMenu::setAutoArrangeIconsChecked(bool checked)
{
    if (checked) {
        m_autoArrangeIconsAction->setChecked(true);
    } else {
        m_autoArrangeIconsAction->setChecked(false);
    }
}

void RightMenu::setAlignIconsToGridChecked(bool checked)
{
    if (checked) {
        m_alignIconsToGridAction->setChecked(true);
    } else {
        m_alignIconsToGridAction->setChecked(false);
    }
}

bool RightMenu::isAutoArrangeIconsChecked() const
{
    return m_autoArrangeIconsAction ? m_autoArrangeIconsAction->isChecked() : false;
}

bool RightMenu::isAlignIconsToGridChecked() const
{
    return m_alignIconsToGridAction ? m_alignIconsToGridAction->isChecked() : false;
}

void RightMenu::setDoubleClickedHideIconsChecked(bool checked)
{
    if(checked) {
        m_doubleClickedHideIconsAction->setChecked(true);
    }else {
        m_doubleClickedHideIconsAction->setChecked(false);
    }
}

// Sort-by setters — QActionGroup 已保证互斥，只需 setChecked(true)
void RightMenu::setSortByNameChecked(bool checked)
{
    if (m_sortByNameAction) m_sortByNameAction->setChecked(checked);
}

void RightMenu::setSortBySizeChecked(bool checked)
{
    if (m_sortBySizeAction) m_sortBySizeAction->setChecked(checked);
}

void RightMenu::setSortByTypeChecked(bool checked)
{
    if (m_sortByTypeAction) m_sortByTypeAction->setChecked(checked);
}

void RightMenu::setSortByDateChecked(bool checked)
{
    if (m_sortByDateAction) m_sortByDateAction->setChecked(checked);
}

