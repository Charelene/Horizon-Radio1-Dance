; BBC Radio 1 Dance for Forza Horizon 2: the Windows setup wizard (Inno Setup 6, https://jrsoftware.org/isinfo.php).
; The wizard picks the Azimuth folder; the engine (r1d-installer.exe, built by ../build.sh) does the work: it fetches
; ffmpeg and builds the station from the player's own game files.
; Build: iscc windows\radio1-dance.iss   -> dist\BBCRadio1Dance-Setup.exe  (add /DAppVersion=1.2.3 to set the version)

#define AppName "BBC Radio 1 Dance"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Engine "r1d-installer.exe"

[Setup]
AppId={{6F0D2C41-7B1E-4C8A-9D35-2A41E0B7C913}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=BBC Radio 1 Dance for Forza Horizon 2 (fan mod)
; the "install folder" is the player's Azimuth folder
DefaultDirName={autopf}\Azimuth
AppendDefaultDirName=no
DirExistsWarning=no
DisableProgramGroupPage=yes
UninstallFilesDir={app}\Radio1Dance
UninstallDisplayName={#AppName} for Forza Horizon 2
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=BBCRadio1Dance-Setup

[Messages]
WizardSelectDir=Where is Azimuth?
SelectDirDesc=BBC Radio 1 Dance goes into your Azimuth folder.
SelectDirLabel3=Choose the folder that contains azimuth.exe.
SelectDirBrowseLabel=Click Next to continue, or Browse to pick a different folder.
FinishedLabel=BBC Radio 1 Dance is installed. Start the game and scroll the radio to "BBC Radio 1 Dance".

[Files]
Source: "..\dist\r1d-installer-windows-x86_64.exe"; DestDir: "{app}\Radio1Dance"; DestName: "{#Engine}"; Flags: ignoreversion

[UninstallRun]
Filename: "{app}\Radio1Dance\{#Engine}"; Parameters: "--azimuth ""{app}"" --uninstall --yes"; Flags: runhidden; RunOnceId: "RemoveStation"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Radio1Dance"

[Code]
function FindAzimuth(): String;
var
  Home: String;
begin
  Home := ExpandConstant('{%USERPROFILE}');
  Result := '';
  if FileExists(ExpandConstant('{src}\azimuth.exe')) then
    Result := ExpandConstant('{src}')
  else if FileExists(Home + '\Games\Azimuth\azimuth.exe') then
    Result := Home + '\Games\Azimuth'
  else if FileExists(Home + '\Downloads\Azimuth\azimuth.exe') then
    Result := Home + '\Downloads\Azimuth'
  else if FileExists(Home + '\Desktop\Azimuth\azimuth.exe') then
    Result := Home + '\Desktop\Azimuth'
  else if FileExists(Home + '\Documents\Azimuth\azimuth.exe') then
    Result := Home + '\Documents\Azimuth';
end;

procedure InitializeWizard();
var
  Found: String;
begin
  Found := FindAzimuth();
  if (Found <> '') and (WizardForm.PrevAppDir = '') then
    WizardForm.DirEdit.Text := Found;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
    if not FileExists(AddBackslash(WizardDirValue) + 'azimuth.exe') then
    begin
      MsgBox('There is no azimuth.exe in that folder. Choose the folder Azimuth is installed in.', mbError, MB_OK);
      Result := False;
    end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Args: String;
  Code: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    Args := '--azimuth "' + ExpandConstant('{app}') + '" --yes --pause-on-error';
    { the engine opens a console window that shows its progress }
    if (not Exec(ExpandConstant('{app}\Radio1Dance\{#Engine}'), Args, '', SW_SHOW, ewWaitUntilTerminated, Code)) or (Code <> 0) then
      MsgBox('BBC Radio 1 Dance could not finish installing (see the window that just closed, or run Setup again to retry).', mbError, MB_OK);
  end;
end;
