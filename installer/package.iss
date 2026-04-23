#define AppName "LinDesk"
#define AppVersion "1.0.0"
#define AppExeName "LinDesk.exe"
#define BuildDir "D:\Desktop\LinDesk\build\build_64\Release"
#define PackageOutputDir "D:\Desktop\LinDesk\installer"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Max
AppPublisherURL=https://github.com/linuup
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir={#PackageOutputDir}
OutputBaseFilename={#AppName}_Setup_v{#AppVersion}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
CreateUninstallRegKey=yes
Uninstallable=yes
UninstallFilesDir={app}

[Languages]
Name: "zh"; MessagesFile: "ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
en.desktopIcon=Create desktop shortcut
zh.desktopIcon=创建桌面快捷方式
en.startMenuIcon=Create Start Menu shortcut
zh.startMenuIcon=创建开始菜单快捷方式
en.shortcutGroup=Shortcuts:
zh.shortcutGroup=快捷方式:
en.installRuntime=Installing VC++ Runtime...
zh.installRuntime=正在安装 VC++ 运行库...
en.launchApp=Launch {#AppName}
zh.launchApp=启动 {#AppName}
en.closeRunning={#AppName} is running. Close it and continue uninstall?
zh.closeRunning={#AppName} 正在运行。是否关闭并继续卸载？
en.closeFailed=Unable to close {#AppName}. Please close it manually and try again.
zh.closeFailed=无法关闭 {#AppName}。请手动关闭后重试。

[Files]
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Tasks]
Name: "desktopicon"; Description: "{cm:desktopIcon}"; GroupDescription: "{cm:shortcutGroup}"
Name: "startmenuicon"; Description: "{cm:startMenuIcon}"; GroupDescription: "{cm:shortcutGroup}"; Flags: checkedonce

[Icons]
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; Tasks: desktopicon
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#AppExeName}"; Tasks: startmenuicon
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"; WorkingDir: "{app}"; Tasks: startmenuicon

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "{cm:installRuntime}"
Filename: "{app}\{#AppExeName}"; Description: "{cm:launchApp}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{userdesktop}\{#AppName}.lnk"
Type: files; Name: "{autodesktop}\{#AppName}.lnk"
Type: files; Name: "{commondesktop}\{#AppName}.lnk"
Type: files; Name: "{group}\{#AppName}.lnk"
Type: files; Name: "{group}\Uninstall {#AppName}.lnk"
Type: dirifempty; Name: "{group}"
Type: filesandordirs; Name: "{app}"

[Code]
function IsLinDeskProcessRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('cmd.exe', '/C "tasklist /FI ""IMAGENAME eq {#AppExeName}"" 2>NUL | find /I ""{#AppExeName}"" >NUL"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  Result := ResultCode = 0;
end;

function IsLinDeskMutexActive(): Boolean;
begin
  Result := CheckForMutexes('Local\LinDesk_SingleInstance');
end;

function IsLinDeskRunning(): Boolean;
begin
  Result := IsLinDeskProcessRunning() or IsLinDeskMutexActive();
end;

function StopLinDesk(): Boolean;
var
  ResultCode: Integer;
  RetryCount: Integer;
begin
  Result := True;

  if not IsLinDeskRunning() then
  begin
    Exit;
  end;

  Exec('taskkill.exe', '/f /t /im {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  RetryCount := 0;
  while (IsLinDeskProcessRunning() or IsLinDeskMutexActive()) and (RetryCount < 20) do
  begin
    Sleep(500);
    RetryCount := RetryCount + 1;
  end;

  Result := not IsLinDeskProcessRunning() and not IsLinDeskMutexActive();
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;

  if IsLinDeskRunning() then
  begin
    if MsgBox(ExpandConstant('{cm:closeRunning}'), mbConfirmation, MB_YESNO) = IDYES then
    begin
      if not StopLinDesk() then
      begin
        MsgBox(ExpandConstant('{cm:closeFailed}'), mbError, MB_OK);
        Result := False;
      end;
    end
    else
    begin
      Result := False;
    end;
  end;
end;
