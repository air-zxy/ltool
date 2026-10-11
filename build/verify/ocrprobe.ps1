$ErrorActionPreference = 'Continue'
# 探一下 Windows 自带的 OCR（Windows.Media.Ocr）能不能用、有没有中文
try {
  Add-Type -AssemblyName 'System.Runtime.WindowsRuntime, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089' -ErrorAction Stop
} catch { Write-Output ('加载 WinRT 互操作失败: ' + $_.Exception.Message) }
$src = @'
using System;
using System.Threading.Tasks;
using Windows.Globalization;
using Windows.Media.Ocr;
public static class OcrProbe {
  public static string List() {
    try {
      var sb = new System.Text.StringBuilder();
      foreach (var lang in OcrEngine.AvailableRecognizerLanguages) {
        sb.Append(lang.LanguageTag + '=' + (OcrEngine.TryCreateFromLanguage(lang) != null ? "可用" : "建不出引擎") + "; ");
      }
      var def = OcrEngine.TryCreateFromUserProfileLanguages();
      sb.Append('| 默认引擎=' + (def == null ? "没有（缺语言包）" : "有"));
      return sb.Length == 0 ? '一个 OCR 语言都没有' : sb.ToString();
    } catch (Exception e) { return 'EXC ' + e.Message; }
  }
}
'@
try {
  Add-Type -TypeDefinition $src -ReferencedAssemblies @('Windows.Foundation.UniversalApiContract', 'System.Runtime.WindowsRuntime') -ErrorAction Stop
  Write-Output ('OCR: ' + [OcrProbe]::List())
} catch {
  Write-Output ('Add-Type 失败: ' + $_.Exception.Message)
}
Write-Output '--- 已安装的 OCR 相关可选功能（注册表里能看到的语言包）---'
foreach ($k in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\OEMInformation',
                 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion')) { }
$raw = & cmd.exe /c 'dism /online /get-capabilities /format:table 2>&1'
$raw | Where-Object { $_ -match 'OCR|Optical' } | ForEach-Object { Write-Output ('  ' + $_.Trim()) }
