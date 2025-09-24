#pragma once
#include "RightMenu.h"
#include <qmenu.h>
#include <qdebug.h>
#include "../SystemApi/SystemApi.h"

#include <QProcess>
#include <qstandardpaths.h>
#include "../SystemApi/Tool.h"

//bool RightMenu::setupMenuForFileType(FileWidget* fileWidget)
//{
//	bool res = false;
//	try
//	{
//		auto iconStruct = fileWidget->getFileIconStruct();
//		fileType = iconStruct->fileType;
//		this->sf = iconStruct->sf;
//		currentObjPath = iconStruct->filePath.replace("/", "\\");
//		res = true;
//		// 根据文件类型设置菜单
//		if (fileType == FileType::SpecialFolder) {
//			
//		}
//		else if (fileType == FileType::DiskLetter) {
//
//		}
//	}
//	catch (const std::exception&)
//	{
//		res == false;
//	}
//	// 其他文件类型...
//	return res;
//}

//bool RightMenu::setupMenuForFileType(QMap<QString, FileWidget*>* &allChooseFiles)
//{
//	qDebug() << "Multiple files";
//
//	return false;
//}

void RightMenu::showMenu(QPoint pos) const
{
	POINT p;
	p.x = pos.x();
	p.y = pos.y();
	const POINT* position = &p;
	ShellContextMenu scm;
	
	if (fileType == ObjectType::BackGround) {
		scm.DisplayDesktopContextMenu();
	}else if (fileType == ObjectType::File) {
		scm.DisplayFileContextMenu(objPath());
	}
	else if (fileType == ObjectType::ZFolder) {
		scm.DisplayFileContextMenu(objPath());
	}
	else if (fileType == ObjectType::Link) {
		QString str = QString(currentObjPath).append(".lnk");
		scm.DisplayLinkContextMenu(objPath(str));
	}
	else if (fileType == ObjectType::SpecialFolder) {
		scm.DisplaySpecialFolderContextMenu(this->sf);
	}
	else if (fileType == ObjectType::DiskLetter) {

	}
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




//==========RightMenu

//std::vector<std::unique_ptr<RightMenu>> RightMenu::rightMenus;
//std::vector<std::unique_ptr<RightMenuCreateFile>> RightMenu::createFileRegs;

//==========CreatRightMenu

//std::vector<QString> RightMenuCreateFile::excludeCreatMenus = { "Library Folder", ".library-ms" };



///**
//桌面右键
//个性化 control /name Microsoft.Personalization
//显示设置  control /name Microsoft.Display
//**/
//QMenu* RightMenu::GetDesktopBackMenu()
//{
//    RightMenu::rightMenus = RightMenu::rightMenu_Program();
//    //RightMenu::createFileRegs = RightMenuCreateFile::allRightNewFile_Regedit();
//    // 设置字体
//    QFont font(QString("Microsoft YaHei UI"));
//    menu = new QMenu();
//    //menu->setFont(font);
//    // 创建主菜单并设置宽度
//    menu->setMinimumWidth(rightMenuWidth);
//    // 创建子菜单“查看”
//    auto second_checkMenu = new QMenu("View");
//    second_checkMenu->addAction("Large icons");
//    second_checkMenu->addAction("Medium icons");
//    second_checkMenu->addAction("Small icons");
//    second_checkMenu->addSeparator();
//    second_checkMenu->addAction("自动排列图标");
//    second_checkMenu->addAction("将图标与网格对齐");
//    second_checkMenu->addAction("显示桌面图标");
//    menu->addMenu(second_checkMenu);
//
//    // 创建子菜单“排序方式”
//    auto second_sorttype = new QMenu("Sort by");
//    second_sorttype->addAction("名称");
//    second_sorttype->addAction("大小");
//    second_sorttype->addAction("项目类型");
//    second_sorttype->addAction("修改日期");
//    menu->addMenu(second_sorttype);
//    QAction* refresh = new QAction("Refresh");
//    menu->addAction(refresh);
//    menu->addSeparator();
//    QAction* paste = new QAction("Paste");
//    paste->setDisabled(true);
//    menu->addAction(paste);
//    QAction* pasteShortcut = new QAction("Paste shortcut");
//    pasteShortcut->setDisabled(true);
//    menu->addAction(pasteShortcut);
//
//    //// 添加自定义右键菜单项
//    //menu->addSeparator();
//    //for (const auto& val : RightMenu::rightMenus) {
//    //    QAction *action = new QAction(val->menuName);
//    //    action->setIcon(SysInfoUtil::ExtractIconFromExe(val->iconPath,val->iconIndex));
//    //    connect(action, &QAction::triggered, [this, command = val->command]() {
//    //        openThirdPartySoftwareMenu_Slot(command);
//    //    });
//    //    //qDebug() << "rightmenu: " << val->menuName;
//    //    menu->addAction(action);
//    //}
//    //menu->addSeparator();
//
//    //// 创建子菜单“新建”
//    //auto second_createmenu = new QMenu("新建");
//    //second_createmenu->setMinimumWidth(rightMenuWidth);
//
//    //for (const auto& reg : createFileRegs) {
//    //    //qDebug() << reg ->menuName;
//    //    auto action = new QAction(reg->menuName);
//    //    action->setIcon(SysInfoUtil::ExtractIconFromExe(reg->iconPath,reg->iconIndex));
//    //    second_createmenu->addAction(action);
//
//    //    if (reg->menuName == "Shortcut") {
//    //        second_createmenu->addSeparator();
//    //    }
//    //}
//    //menu->addMenu(second_createmenu);
//    menu->addSeparator();
//    //Personalize
//    QAction* personalize = new QAction("显示设置");
//    personalize->setIcon(SysInfoUtil::ExtractSystemIcon(PERSONALIZE));
//    personalize->connect(personalize, &QAction::triggered, this, &RightMenu::openPersonalize_Slot);
//    menu->addAction(personalize);
//    
//    //Display Settings
//    QAction *display = new QAction("个性化");
//    display->setIcon(SysInfoUtil::ExtractSystemIcon(DISPLAY_SETTINGS));
//    display->connect(display, &QAction::triggered, this, &RightMenu::openDisplay_Slot);
//    menu->addAction(display);
//    
//    
//    return menu;
//}
//
//
//
//std::vector<std::unique_ptr<RightMenu>> RightMenu::rightMenu_Program()
//{
//    // 在 std::vector 中存储 std::unique_ptr<RightMenu>
//    std::vector<std::unique_ptr<RightMenu>> r_Menus;
//    QString path = QStringLiteral("%1Directory\\Background\\shell\\").arg(HKEY_ROOT);
//    QSettings qs(path, QSettings::NativeFormat);
//    // 遍历键值对（假设键下是字符串类型的值）
//    for (auto& name : qs.childGroups()) {
//        //剔除不需要的菜单
//        if (std::find(excludeMenus.begin(), excludeMenus.end(), name) != excludeMenus.end()) {
//            
//            continue;
//        }
//        
//        
//        QSettings nameS(path + name, QSettings::NativeFormat);
//        QSettings cmdS(path + name + "\\Command", QSettings::NativeFormat);
//        
//        std::string menuName = nameS.value(".").toString().toStdString();//菜单名称或路径
//        std::string menuIcon = nameS.value("Icon").toString().toStdString();//图标路径
//        // 创建 std::unique_ptr<RightMenu> 并初始化
//        auto rightMenu = std::make_unique<RightMenu>();
//        //如果菜单名称在dll或exe里提取
//        if (Tool::IsExistResourceFromDll(menuName)) {
//            //qDebug() << tmp;
//            const char * name = CSystemApi::GetTextFromDll(menuName.c_str());
//            rightMenu->menuName = QString::fromStdString(std::string(name));
//        }else{
//            
//            rightMenu->menuName = QString::fromStdString(menuName);
//        }
//        //////如果菜单图标在dll或exe里提取
//        //if (Tool::IsExistResourceFromDll(menuIcon)) {
//        //    ResourceInfo ri = Tool::GetResourceInfo(menuIcon.c_str());
//        //    rightMenu->iconPath = QStringLiteral("%1,%2")
//        //        .arg(QString::fromStdString(ri.path)
//        //        .arg(ri.index));
//        //}
//        //else {
//        //    rightMenu->iconPath = nameS.value("Icon").toString();
//        //}
//        
//        //判断图标是否有索引
//        QStringList list = nameS.value("Icon").toString().split(",");
//        if (list.length() > 1) {
//            rightMenu->iconPath = list[0];
//            rightMenu->iconIndex = list[1].toInt();
//        }
//        else {
//            rightMenu->iconPath = list[0];
//        }
//        
//        rightMenu->command = cmdS.value(".").toString();
//        // cmd = cmd.split("%")[0].replace("\"", "")
//        r_Menus.push_back(std::move(rightMenu));
//    }
//    return r_Menus;
//}
//
//void RightMenu::getChildsItemPath(QSettings& qs, std::vector<QString>& items)
//{
//    QString fullPath = qs.fileName().replace("HKEY_LOCAL_MACHINE\\", HKEY_ROOT).replace("HKEY_LOCAL_MACHINE\\", HKEY_ROOT);
//    for (auto& child : qs.childGroups()) {
//        QString childPath(fullPath + "\\" + child);
//        items.push_back(childPath);
//        QSettings q(childPath, QSettings::NativeFormat);
//        getChildsItemPath(q, items);
//    }
//}
//
//void RightMenu::openPersonalize_Slot()
//{
//    system("control /name Microsoft.Personalization");
//    //QProcess::startDetached("control.exe", QStringList() << "/name" << "Microsoft.Personalization");
//}
//
//void RightMenu::openDisplay_Slot()
//{
//    system("control /name Microsoft.Display");
//    //QProcess::startDetached("control.exe", QStringList() << "/name" << "Microsoft.Display");
//}
//
//void RightMenu::openThirdPartySoftwareMenu_Slot(QString command) {
//    QProcess q;
//    command = command.replace(QRegExp("%v", Qt::CaseInsensitive), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
//    q.startDetached(command);
//}



//QString RightMenuCreateFile::findTemplatePath(QString regPath)
//{
//
//    return QString();
//}
//
//std::vector<std::unique_ptr<RightMenuCreateFile>> RightMenuCreateFile::sortRightMenu(std::vector<std::unique_ptr<RightMenuCreateFile>>& menus)
//{
//    std::vector<std::unique_ptr<RightMenuCreateFile>> regList;
//    // 首先找到Folder类型的菜单项
//    for (auto& menu : menus) {
//        if (menu->menuName == "Folder") {
//            regList.insert(regList.begin(), std::move(menu));
//            break; // 只保留第一个Folder（假设只需要一个）
//        }
//    }
//    // 然后找到Shortcut类型的菜单项
//    for (auto& menu : menus) {
//        if (menu->menuName == "Shortcut" && regList.size() < 2) { // 只在regList中还没有第二个元素时插入
//            regList.insert(++regList.begin(), std::move(menu)); // 插入到第一个元素后面
//            break; // 只保留第一个Shortcut（假设只需要一个）
//        }
//    }
//    // 最后，将剩余的其他所有元素添加到最后
//    for (auto& menu : menus) {
//        if (menu) {
//            regList.push_back(std::move(menu));
//        }
//    }
//    return regList;
//}
//
//
//std::vector<std::unique_ptr<RightMenuCreateFile>> RightMenuCreateFile::allRightNewFile_Regedit()
//{
//    //存储所有新建文件对象
//    std::vector<std::unique_ptr<RightMenuCreateFile>> rightNewFiles;
//    QString path = QStringLiteral("%1Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Discardable\\PostSetup\\ShellNew").arg(HKEY_USER);
//    for (auto& rearName : QSettings(path, QSettings::NativeFormat).value("Classes").toStringList()) {
//        QString regName = QSettings(HKEY_ROOT + rearName, QSettings::NativeFormat).value(".").toString();
//        QString menuName = QSettings(HKEY_ROOT + regName, QSettings::NativeFormat).value(".").toString();
//        QString icon = QSettings(HKEY_ROOT + regName + "\\DefaultIcon", QSettings::NativeFormat).value(".").toString();
//        //short cut Icon
//        if (icon.isEmpty()) {
//            icon = QSettings(HKEY_ROOT + rearName + "\\ShellNew", QSettings::NativeFormat).value("IconPath").toString();
//        }
//        //剔除不需要的菜单
//        if (std::find(excludeCreatMenus.begin(), excludeCreatMenus.end(), rearName) != excludeCreatMenus.end()) {
//            continue;
//        }
//        // 创建 std::unique_ptr<RightMenu> 并初始化
//        auto rightNewFile = std::make_unique<RightMenuCreateFile>(rearName, HKEY_ROOT + rearName);
//        //RightMenuCreateFile rightNewFile(rearName, HKEY_ROOT + rearName);
//        rightNewFile->menuName = menuName;
//        
//        //判断图标是否有索引
//        QStringList list = icon.split(",");
//        if (list.length() > 1) {
//            rightNewFile->iconPath = list[0];
//            rightNewFile->iconIndex = list[1].toInt();
//        }
//        else {
//            rightNewFile->iconPath = list[0];
//        }
//        //rMenuFile._hasTemplate = RightMenuCreateFile.findTemplatePath(HKEY_ROOT + rearName)
//        rightNewFiles.push_back(std::move(rightNewFile));
//    }
//    return RightMenuCreateFile::sortRightMenu(rightNewFiles);
//}
//
