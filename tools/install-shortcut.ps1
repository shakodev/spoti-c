param(
    [string]$Executable = (Join-Path $PSScriptRoot '../build-desktop/spotic.exe')
)

$ErrorActionPreference = 'Stop'
$target = (Resolve-Path -LiteralPath $Executable).Path
$programs = [Environment]::GetFolderPath('Programs')
$shortcutPath = Join-Path $programs 'Spoti-C.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $target
$shortcut.WorkingDirectory = Split-Path -Parent $target
$shortcut.IconLocation = "$target,0"
$shortcut.Description = 'Spoti-C native music player'
$shortcut.Save()
$saved = $shell.CreateShortcut($shortcutPath)
if ($saved.TargetPath -ne $target -or !(Test-Path -LiteralPath $saved.TargetPath)) {
    throw 'Shortcut verification failed.'
}
Write-Output "Installed: $shortcutPath"
Write-Output "Opens: $($saved.TargetPath)"
