#include "Tool.h"
#include <cstring>
#include <stdexcept>
#include <memory>
#include <stringapiset.h>

class Tool;

ResourceInfo Tool::GetResourceInfo(const char* input)
{
    //std::unique_ptr<ResourceInfo> uptr(new ResourceInfo());
    ResourceInfo ri;
    //ResourceInfo* uptr = nullptr;
    // Find the last occurrence of ',' in the input string  
    const char* commaPos = strrchr(input, ',');
    if (commaPos != nullptr) {
        // Extract the path part (up to the comma)
        const char* commaPos = strrchr(input, ',');
        ri.path = ri.path.assign(input, commaPos).replace(0, 1, "");
        // Extract the index part (after the comma)
        char* endPtr;
        int index = std::strtol(commaPos + 1, &endPtr, 10);
        ri.index = std::abs(index); // Convert to integer
        if (*endPtr != '\0') { // Check if there's any non-digit character after the number
            throw std::invalid_argument("Invalid index format");
        }
    }
    else {
        throw std::invalid_argument("Invalid resource string format: Missing comma separator");
    }
    //return uptr.get();
    return ri;
}

bool Tool::IsExistResourceFromDll(std::string input)
{
    return !input.find("@") ? true : false;
}

std::string Tool::WstrToStr(const wchar_t* wstr)
{
    if (wstr == nullptr)
        return std::string();

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded == 0)
        return std::string();

    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}
