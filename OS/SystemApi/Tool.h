#pragma once
#include <Windows.h>
#include <ShlObj.h>
#include <wil/com.h>
#include <string>
#include <memory>
#include <iostream>

using unique_pidl_absolute = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>>;
using unique_pidl_relative = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_RELATIVE>>;
using unique_pidl_child = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PITEMID_CHILD>>;


/// <summary>
/// 
/// Example @C:\Program Files (x86)\Common Files\Microsoft Shared\MSEnv\1033\VSLauncherUI.dll,-1002
/// </summary>
struct ResourceInfo {
	std::string path = "";
	int index = 0;
};

class Tool
{
public:
	/// <summary>
	/// //获取根节点的Pidl Desktop_Pidl
	/// </summary>
	/// <param name="pidl"></param>
	/// <returns></returns>
	static unique_pidl_absolute GetRootPidl();
	static HRESULT BindToIdl(PIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv);
	/// <summary>
	/// 获取注册表里字符串格式的dll路径和资源下标
	/// </summary>
	/// <param name="input"></param>
	/// <returns></returns>
	static ResourceInfo GetResourceInfo(const char* input);
	/// <summary>
	/// 资源是否在dll或exe里
	/// </summary>
	/// <param name="input"></param>
	/// <returns></returns>
	static bool IsExistResourceFromDll(std::string input);

	static std::string WideToUtf8(const wchar_t* wstr);
	static std::string WstrToStr(const wchar_t* wstr);		//宽字符串转WString
	static void ConvertToGUID(const std::wstring& clsidStr);
};

