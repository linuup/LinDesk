#pragma once
#include "ListItemData.h"
#include <vector>
#include <shlobj.h>
#include <wil/com.h>
class FolderEnumerator {
public:
    // 枚举指定文件夹下的所有项
    static std::vector<ListItemData> enumerate(LPITEMIDLIST folderPidl) {
        std::vector<ListItemData> items;

        if (!folderPidl) return items;
		
        // 获取 IShellFolder 接口
        //wil::com_ptr<IShellFolder> spFolder;
        //if (FAILED(SHBindToObject(nullptr, IID_IShellFolder, folderPidl, IID_PPV_ARGS(&spFolder)))) {
        //    return items;
        //}

        //// 枚举文件夹内容
        //wil::com_ptr<IEnumIDList> spEnum;
        //if (FAILED(spFolder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &spEnum))) {
        //    return items;
        //}

        //LPITEMIDLIST child = nullptr;
        //while (spEnum->Next(1, &child, nullptr) == S_OK) {
        //    ListItemData item;
        //    // 组合完整的 PIDL
        //    LPITEMIDLIST fullPidl = ILCombine(folderPidl, child);
        //    if (fullPidl) {
        //        item.setPIDL(fullPidl);
        //        items.push_back(std::move(item));
        //        ILFree(fullPidl);
        //    }
        //    ILFree(child);
        //}

        return items;
    }
};
