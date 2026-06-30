[Setup]
AppName=Shadow
AppVersion=1.0.6
AppPublisher=TQk.z
DefaultDirName={autopf}\Shadow
DefaultGroupName=Shadow
OutputDir=.\
OutputBaseFilename=ShadowWizard_Setup
SetupIconFile=..\src\shadow.ico
UninstallDisplayIcon={app}\Shadow.exe
Compression=lzma2
SolidCompression=yes
LicenseFile=Terms.txt
InfoBeforeFile=BAM_Tutorial.txt
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64

[Files]
Source: "..\bin\Debug\Shadow.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\Debug\targets.txt"; DestDir: "{app}"; Flags: onlyifdoesntexist

[Icons]
Name: "{group}\Shadow"; Filename: "{app}\Shadow.exe"
Name: "{commondesktop}\Shadow"; Filename: "{app}\Shadow.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Code]
var
  DownloadPage: TDownloadWizardPage;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), nil);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  if CurPageID = wpReady then begin
    DownloadPage.Clear;
    DownloadPage.Add('https://aka.ms/vs/17/release/vc_redist.x64.exe', 'vc_redist.x64.exe', '');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
        Result := True;
      except
        if DownloadPage.AbortedByUser then
          Log('Aborted by user.')
        else
          MsgBox('Download failed! Setup will continue, but the app might not run if Visual C++ Redistributable is missing.', mbError, MB_OK);
        Result := True; 
      end;
    finally
      DownloadPage.Hide;
    end;
  end else
    Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    if FileExists(ExpandConstant('{tmp}\vc_redist.x64.exe')) then
    begin
      Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_SHOW, ewWaitUntilTerminated, ResultCode);
    end;
  end;
end;
