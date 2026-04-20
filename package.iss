#define AppName "LinDesk"
#define AppVersion "1.0.0"
#define AppExeName "LinDesk.exe"
#define BuildDir "D:\Desktop\LinDesk\build\build_64\Release"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir={#BuildDir}
OutputBaseFilename={#AppName}_Setup_v{#AppVersion}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
CreateUninstallRegKey=yes
Uninstallable=yes
UninstallFilesDir={app}

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式:"
Name: "startmenuicon"; Description: "创建开始菜单快捷方式"; GroupDescription: "快捷方式:"; Flags: checkedonce

[Files]
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
Source: "{tmp}\unins000.exe"; DestDir: "{app}"; Flags: external

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: startmenuicon
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"; Tasks: startmenuicon
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "安装 VC++ 运行时..."
Filename: "{app}\{#AppExeName}"; Description: "运行 {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeUninstall(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;
  
  Exec('cmd.exe', '/C "tasklist /FI "IMAGENAME eq {#AppExeName}" 2>NUL | find /I "{#AppExeName}" >NUL"', 
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    
  if ResultCode = 0 then
  begin
    if MsgBox('{#AppName} 正在运行，是否结束程序以继续卸载？', mbConfirmation, MB_YESNO) = IDYES then
    begin
      Exec('taskkill.exe', '/f /im {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Sleep(1000);
      
      Exec('cmd.exe', '/C "tasklist /FI "IMAGENAME eq {#AppExeName}" 2>NUL | find /I "{#AppExeName}" >NUL"', 
        '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
        
      if ResultCode = 0 then
      begin
        MsgBox('无法结束程序，请手动结束后再试。', mbError, MB_OK);
        Result := False;
      end;
    end
    else
    begin
      Result := False;
    end;
  end;
end;