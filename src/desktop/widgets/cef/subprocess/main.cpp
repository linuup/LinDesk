#ifdef LINDESK_HAS_CEF
#include <include/cef_app.h>
#include <include/cef_scheme.h>
#include <windows.h>

class SubprocessApp : public CefApp {
public:
    SubprocessApp() = default;

    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
        registrar->AddCustomScheme("linwidget",
            CEF_SCHEME_OPTION_STANDARD |
            CEF_SCHEME_OPTION_SECURE |
            CEF_SCHEME_OPTION_CORS_ENABLED |
            CEF_SCHEME_OPTION_FETCH_ENABLED);
    }

    IMPLEMENT_REFCOUNTING(SubprocessApp);
};

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    CefMainArgs main_args(hInstance);
    CefRefPtr<SubprocessApp> app = new SubprocessApp();
    return CefExecuteProcess(main_args, app, nullptr);
}
#else
#include <windows.h>
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    return 0;
}
#endif
