#ifdef LINDESK_HAS_CEF
#include <include/cef_app.h>
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    CefMainArgs main_args(hInstance);
    return CefExecuteProcess(main_args, nullptr, nullptr);
}
#else
#include <windows.h>
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    return 0;
}
#endif
