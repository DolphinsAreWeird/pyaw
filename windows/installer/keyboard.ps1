# Adds or removes Pyaw in the current user's keyboard list (Settings > Time & language > Typing).
#   keyboard.ps1 add | remove
param([string]$Action = "add")

$tip = "0455:{E05EC474-487B-4788-AD78-D238B56D2833}{5080D84A-8835-4182-9601-296F428F17FC}"
$list = Get-WinUserLanguageList
$burmese = $list | Where-Object { $_.LanguageTag -like "my*" } | Select-Object -First 1

if ($Action -eq "add") {
    if (-not $burmese) {
        $list.Add("my")
        $burmese = $list | Where-Object { $_.LanguageTag -like "my*" } | Select-Object -First 1
        # Newly added: offer only Pyaw under Burmese, not the default Myanmar layout.
        $burmese.InputMethodTips.Clear()
    }
    if (-not ($burmese.InputMethodTips -contains $tip)) { $burmese.InputMethodTips.Add($tip) }
    Set-WinUserLanguageList $list -Force
} elseif ($burmese) {
    [void]$burmese.InputMethodTips.Remove($tip)
    if ($burmese.InputMethodTips.Count -eq 0) { [void]$list.Remove($burmese) }
    Set-WinUserLanguageList $list -Force
}
