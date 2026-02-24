#pragma once
#include "RightMenu.h"
#include <qmenu.h>
#include <qdebug.h>
#include "../SystemApi/SystemApi.h"
#include <QProcess>
#include <qstandardpaths.h>
#include "../SystemApi/Tool.h"
  
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

    m_sortByNameAction->setChecked(true); // 默认选中名称
    
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
    QAction* pasteAction = new QAction("Paste", m_customMenu);
    pasteAction->setEnabled(false);
    connect(pasteAction, &QAction::triggered, this, &RightMenu::onPaste);
    m_customMenu->addAction(pasteAction);
    
    // 6-Paste shortcut
    QAction* pasteShortcutAction = new QAction("Paste shortcut", m_customMenu);
    pasteShortcutAction->setEnabled(false);
    connect(pasteShortcutAction, &QAction::triggered, this, &RightMenu::onPasteShortcut);
    m_customMenu->addAction(pasteShortcutAction);

    // 7-Create Box
    m_createBoxAction = new QAction("New Grid", m_customMenu);
    connect(m_createBoxAction, &QAction::triggered, this, &RightMenu::onCreateBox);
    m_customMenu->addAction(m_createBoxAction);

    m_customMenu->setFixedWidth(150);
}

void RightMenu::onLargeIcons() { m_largeIconsAction->setChecked(true); emit LargeIconsTriggered(); }
void RightMenu::onMediumIcons() { m_mediumIconsAction->setChecked(true); emit MediumIconsTriggered(); }
void RightMenu::onSmallIcons() { m_smallIconsAction->setChecked(true); emit SmallIconsTriggered(); }
void RightMenu::onAutoArrangeIcons() { 
    emit AutoArrangeIconsTriggered(); 
}
void RightMenu::onAlignIconsToGrid() { 
    emit AlignIconsToGridTriggered(); 
}
void RightMenu::onShowDesktopIcons() { 
    emit ShowDesktopIconsTriggered(); 
}
void RightMenu::onDoubleClickedHideIcons() {
    emit DoubleClickedHideIconsTriggered();
}

void RightMenu::onSortByName() { m_sortByNameAction->setChecked(true); emit sortByNameTriggered(); }
void RightMenu::onSortBySize() { m_sortBySizeAction->setChecked(true); emit sortBySizeTriggered(); }
void RightMenu::onSortByType() { m_sortByTypeAction->setChecked(true); emit sortByTypeTriggered(); }
void RightMenu::onSortByDate() { m_sortByDateAction->setChecked(true); emit sortByDateTriggered(); }

void RightMenu::onRefresh() { emit refreshTriggered(); }
void RightMenu::onPaste() { emit pasteTriggered(); }
void RightMenu::onPasteShortcut() { emit pasteShortcutTriggered(); }
void RightMenu::onUndoDelete() { emit undoDeleteTriggered(); }
void RightMenu::onCreateBox() { emit createBoxTriggered(); }


void RightMenu::showMenu(QPoint pos, ListItem *listitem) const{
	ShellContextMenu scm;
	POINT p = { pos.x(),pos.y() };
	const POINT* position = &p;
	if (this->fileType == ObjectType::BackGround) {
		scm.DisplayDesktopContextMenu();

		// 将自定义菜单项注册到Shell背景菜单中
		if (m_customMenu) {
			std::function<std::vector<ShellContextMenu::CustomMenuItem>(const QList<QAction*>&)> convertActions = 
				[&](const QList<QAction*>& actions) {
				std::vector<ShellContextMenu::CustomMenuItem> items;
				for (QAction* action : actions) {
					ShellContextMenu::CustomMenuItem item;
					if (action->isSeparator()) {
						item.isSeparator = true;
					} else {
						item.text = action->text().toStdWString();
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
		}

		scm.ShowMenu(this->hwnd, position, nullptr);
		return;
	}else if (this->fileType == ObjectType::File) {
		scm.setOperationType(ShellContextMenu::OperationType::file);
	}else if (this->fileType == ObjectType::Folder_ObjectType) {
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
        m_smallIconsAction->setChecked(true);
    } else {
        m_smallIconsAction->setChecked(false);
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

void RightMenu::setDoubleClickedHideIconsChecked(bool checked)
{
    if(checked) {
        m_doubleClickedHideIconsAction->setChecked(true);
    }else {
        m_doubleClickedHideIconsAction->setChecked(false);
    }
}
