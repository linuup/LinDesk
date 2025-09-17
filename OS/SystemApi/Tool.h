#pragma once
#include <string>
#include <memory>
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


	static std::string WstrToStr(const wchar_t* wstr);		//宽字符串转WString
};

