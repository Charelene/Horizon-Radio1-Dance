; BBC Radio 1 Dance for Forza Horizon 2: the Windows setup wizard (Inno Setup 6, https://jrsoftware.org/isinfo.php).
; The wizard picks the Azimuth folder; the engine (r1d-installer.exe, built by ../build.sh) does the work: it fetches
; ffmpeg and builds the station from the player's own game files.
; Build: iscc windows\radio1-dance.iss   -> dist\BBCRadio1Dance-Setup.exe  (add /DAppVersion=1.2.3 to set the version)

#define AppName "BBC Radio 1 Dance"
#ifndef AppVersion
  #define AppVersion "1.0.1"
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
; always ask: by default the folder page is skipped once the mod has been installed, and a wrong or moved Azimuth
; folder could then never be changed
DisableDirPage=no
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
function HasAzimuth(Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + 'azimuth.exe');
end;

{ Azimuth in Place or in one of its subfolders. Its zip unpacks as "azimuth-windows" and people rename it, so the
  folder's name says nothing. }
function AzimuthIn(Place: String): String;
var
  Rec: TFindRec;
begin
  Result := '';
  if HasAzimuth(Place) then
    Result := Place
  else if FindFirst(AddBackslash(Place) + '*', Rec) then
  begin
    try
      repeat
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0) and (Rec.Name <> '.') and (Rec.Name <> '..') then
          if HasAzimuth(AddBackslash(Place) + Rec.Name) then
            Result := AddBackslash(Place) + Rec.Name;
      until (Result <> '') or not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

function FindAzimuth(): String;
var
  Home: String;
begin
  Home := ExpandConstant('{%USERPROFILE}');
  Result := AzimuthIn(ExpandConstant('{src}'));
  if Result = '' then Result := AzimuthIn(Home + '\Games');
  if Result = '' then Result := AzimuthIn(Home + '\Downloads');
  if Result = '' then Result := AzimuthIn(Home + '\Desktop');
  if Result = '' then Result := AzimuthIn(Home + '\Documents');
  if Result = '' then Result := AzimuthIn(Home);
  if Result = '' then Result := AzimuthIn(ExpandConstant('{sd}\Games'));
end;

procedure InitializeWizard();
var
  Found: String;
begin
  { the folder of an earlier install stays while Azimuth is still in it; otherwise offer the one found }
  if not HasAzimuth(WizardForm.PrevAppDir) then
  begin
    Found := FindAzimuth();
    if Found <> '' then
      WizardForm.DirEdit.Text := Found;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    { the path to azimuth.exe itself is as good as its folder }
    Dir := RemoveBackslashUnlessRoot(Trim(WizardForm.DirEdit.Text));
    if (CompareText(ExtractFileName(Dir), 'azimuth.exe') = 0) and FileExists(Dir) then
      WizardForm.DirEdit.Text := ExtractFileDir(Dir);
    if not HasAzimuth(WizardDirValue) then
    begin
      MsgBox('There is no azimuth.exe in that folder. Choose the folder Azimuth is installed in.', mbError, MB_OK);
      Result := False;
    end;
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
