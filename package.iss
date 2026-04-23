#define AppName "LinDesk"
#define AppVersion "1.0.0"
#define AppExeName "LinDesk.exe"
#define BuildDir "D:\Desktop\LinDesk\build\build_64\Release"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Max
AppPublisherURL=https://github.com/linuup
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

[Files]
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing VC++ Runtime..."
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command ""$s=(New-Object -ComObject WScript.Shell).CreateShortcut('{autodesktop}\{#AppName}.lnk');$s.TargetPath='{app}\{#AppExeName}';$s.WorkingDirectory='{app}';$s.IconLocation='{app}\{#AppExeName},0';$s.Save()"""; Description: "Create desktop shortcut"; Flags: postinstall runhidden skipifsilent unchecked
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command ""New-Item -ItemType Directory -Force -Path '{group}' | Out-Null;$s=(New-Object -ComObject WScript.Shell).CreateShortcut('{group}\{#AppName}.lnk');$s.TargetPath='{app}\{#AppExeName}';$s.WorkingDirectory='{app}';$s.IconLocation='{app}\{#AppExeName},0';$s.Save();$u=(New-Object -ComObject WScript.Shell).CreateShortcut('{group}\Uninstall {#AppName}.lnk');$u.TargetPath='{uninstallexe}';$u.WorkingDirectory='{app}';$u.Save()"""; Description: "Create Start Menu shortcut"; Flags: postinstall runhidden skipifsilent
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeUninstall(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;

  Exec('cmd.exe', '/C "tasklist /FI ""IMAGENAME eq {#AppExeName}"" 2>NUL | find /I ""{#AppExeName}"" >NUL"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  if ResultCode = 0 then
  begin
    if MsgBox('{#AppName} is running. Close it and continue uninstall?', mbConfirmation, MB_YESNO) = IDYES then
    begin
      Exec('taskkill.exe', '/f /im {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Sleep(1000);

      Exec('cmd.exe', '/C "tasklist /FI ""IMAGENAME eq {#AppExeName}"" 2>NUL | find /I ""{#AppExeName}"" >NUL"',
        '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

      if ResultCode = 0 then
      begin
        MsgBox('Unable to close the application. Please close it manually and try again.', mbError, MB_OK);
        Result := False;
      end;
    end
    else
    begin
      Result := False;
    end;
  end;
end;
