function Codes([string]$s) { ($s.ToCharArray() | ForEach-Object { [int]$_ }) -join ',' }
function U([int[]]$c) { -join ($c | ForEach-Object { [char]$_ }) }

$FEN = [char]0x5206                                        # 分
$CLEAN = U @(0x43, 0x76D8, 0x6E05, 0x7406)                 # C盘清理
'A len=' + @($CLEAN).Length + ' codes=' + (Codes $CLEAN) + ' fen=' + (Codes ([string]$FEN))
'B direct=' + (Codes ([string]([char]0x43) + [char]0x76D8 + [char]0x6E05 + [char]0x7406))
try {
    $AE = [System.Windows.Automation.AutomationElement]
    'C type=' + $AE.FullName
    'D has-method=' + (@($AE::GetMethods() | Where-Object { $_.Name -eq 'ElementFromPoint' }).Count)
} catch { 'C-ERR ' + $_.Exception.Message }
try {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
    $pt = New-Object System.Windows.Point(10, 10)
    $el = $AE::ElementFromPoint($pt)
    'E frompoint=' + $el.Current.Name
} catch { 'E-ERR ' + $_.Exception.Message }
