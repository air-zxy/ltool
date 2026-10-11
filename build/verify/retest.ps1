$s = '允许 AiChein 每日签到 版本 0.1.0'
$t1 = $s -replace '\s*(版本|version)\s*[0-9][0-9a-zA-Z._\-]*\s*$', ''
$t2 = $s -replace '\s*版本\s*[0-9][^ ]*$', ''
$t3 = $s -replace '(?s).*?(?=版本)', ''
"orig=[$s]"
"t1=[$t1]"
"t2=[$t2]"
$u = '允许 ltool 版本 0.4.0'
"u2=[$($u -replace '\s*版本\s*[0-9][0-9a-zA-Z._\-]*\s*$', '')]"
$v = 'ltool 允许 Microsoft Visual Studio Code (User)'
"v1=[$($v -replace '^ltool\s+允许\s+', '')]"
$w = 'BFF 版本 1.0.0'
"w1=[$($w -replace '\s*(版本|version)\s*[0-9][0-9a-zA-Z._\-]*\s*$', '')]"
