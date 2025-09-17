// OS.cpp : Source file for your target.
//

#include "OS.h"
#include <iostream>
#include <wil/resource.h>
namespace OS_Module
{
	void printHi() {
		//wil示例 
        // 测试 WIL 的 unique_handle
        wil::unique_handle h(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (h) {
            std::wcout << L"CreateEvent 成功，句柄: " << h.get() << std::endl;
        }
        else {
            std::wcout << L"CreateEvent 失败" << std::endl;
        }
		std::cout << "\nHi from OS module!" << std::endl;
	}
}