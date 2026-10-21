; PitchGuide Windows installer.

#define MyAppName "PitchGuide"
#define MyAppVersion "0.7.2"
#define MyAppPublisher "Fearnus"
#define MyAppURL "https://github.com/Fearnus/PitchGuide"
#define MyAppExeName "PitchGuide.exe"

; AAX is only built when an AAX SDK is available, so only offer it if it was.
#define HaveAAX DirExists(AddBackslash(SourcePath) + "..\..\build\PitchGuidePlugin_artefacts\Release\AAX\PitchGuide.aaxplugin")

[Setup]
; This script lives in packaging\windows. SourceDir points back at the repository
; root so every relative path below (build\..., Resources\..., Output\...) is
; resolved from the repo root, exactly as when the script sat there.
SourceDir=..\..
OutputDir=Output
AppId={{E5ABEAB8-EE01-48F0-93C3-68CCA4B2E623}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={code:getPreProgramPath}\{#MyAppName}
DefaultGroupName={#MyAppPublisher}
DisableProgramGroupPage=yes
OutputBaseFilename=PitchGuide_Installer
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=build\PitchGuide_artefacts\JuceLibraryCode\icon.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
LicenseFile=LICENSE
InfoBeforeFile=THIRD_PARTY_NOTICES.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "main"; Description: "Standalone application and runtime resources"; Types: custom; Flags: fixed
Name: "vst3"; Description: "VST3"; Types: custom
#if HaveAAX
Name: "aax"; Description: "AAX"; Types: custom
#endif

[Files]
Source: "build\PitchGuide_artefacts\Release\PitchGuide.exe"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "build\PitchGuide_artefacts\Release\onnxruntime.dll"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "build\PitchGuidePlugin_artefacts\Release\VST3\PitchGuide.vst3\*"; DestDir: "{code:getPreVST64Path}"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#if HaveAAX
Source: "build\PitchGuidePlugin_artefacts\Release\AAX\PitchGuide.aaxplugin\*"; DestDir: "{code:getPreAAXPath}"; Components: aax; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
Source: "Resources\models\*"; DestDir: "{commonappdata}\{#MyAppPublisher}\{#MyAppName}\models"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "Resources\fonts\Montserrat-Medium.ttf"; DestDir: "{commonfonts}"; FontInstall: "Montserrat-Medium"; Components: main; Flags: onlyifdoesntexist uninsneveruninstall
Source: "Resources\fonts\Montserrat-Regular.ttf"; DestDir: "{commonfonts}"; FontInstall: "Montserrat-Regular"; Components: main; Flags: onlyifdoesntexist uninsneveruninstall
; NOTE: Don't use "Flags: ignoreversion" on any shared system files.

[Icons]
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"

[Code]
var
  Page64: TInputDirWizardPage;

function getPreProgramPath(Param: String): String;
begin
  Result := 'C:\Program Files\{#MyAppPublisher}';
end;

function getPreVST64Path(Param: String): String;
begin
  Result := 'C:\Program Files\Common Files\VST3\{#MyAppPublisher}\{#MyAppName}.vst3';
end;

function getPreAAXPath(Param: String): String;
begin
  Result := 'C:\Program Files\Common Files\Avid\Audio\Plug-Ins\{#MyAppName}.aaxplugin';
end;

function getVST64Dir(Param: String): String;
begin
  if IsWin64 then
    Result := Page64.Values[0];
end;

procedure InitializeWizard;
begin
  if IsWin64 then
  begin
    Page64 := CreateInputDirPage(wpSelectDir,
      'Select VST3 Plugins Directory',
      'Ignore and click next if you do not have a directory for VST3 plugins',
      'Please choose the path of your VST3 plugins: (Ignore and click next if you do not have a directory for VST3 plugins)',
      False, '');

    Page64.Add('');
    Page64.Values[0] := ExpandConstant(getPreVST64Path(''));
  end;
end;
