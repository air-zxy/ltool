$need = @{
  'New-LocalUser'        = @('Name','Password','FullName','Description','PasswordNeverExpires','AccountNeverExpires')
  'Add-LocalGroupMember' = @('Group','Member')
  'Rename-LocalUser'     = @('Name','NewName')
  'Get-LocalGroupMember' = @('Group','Name')
  'ConvertTo-SecureString' = @('AsPlainText','Force')
  'Get-LocalUser'        = @('Name')
}
foreach ($c in $need.Keys | Sort-Object) {
  $cmd = Get-Command $c -ErrorAction SilentlyContinue
  if (-not $cmd) { "{0,-22} (命令不存在)" -f $c; continue }
  foreach ($n in $need[$c]) {
    $hit = if ($cmd.Parameters.Keys -contains $n) { 'OK' } else { 'MISSING' }
    "{0,-22} {1,-22} {2}" -f $c, $n, $hit
  }
}
$sid = (New-Object Security.Principal.SecurityIdentifier('S-1-5-32-544')).Translate([Security.Principal.NTAccount]).Value
"Administrators 组在本机的真名: $sid"
$adm = $sid.Split('\')[-1]
"取出的组名: $adm"
"该组现有成员数: " + (@(Get-LocalGroupMember -Group $adm -ErrorAction SilentlyContinue)).Count
