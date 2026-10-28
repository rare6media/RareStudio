# Signs RareStudio's executables with the self-signed "Rare6Media" code-signing certificate.
#
#   scripts\sign.ps1                # create the certificate if needed, sign the staged exe + launcher
#   scripts\sign.ps1 -Trust         # also trust the certificate on THIS computer (one Windows prompt)
#
# The private key stays in your Windows certificate store (CurrentUser\My) and is never exported, so it
# cannot end up in the repository. Only the public certificate is written, to installer\Rare6Media.cer.
# Self-signed means Windows shows "Rare6Media" as the publisher only on computers that trust that
# certificate (-Trust does this here; elsewhere, import the .cer into Trusted Root and Trusted
# Publishers). SmartScreen reputation still has to be earned, or bought with a commercial certificate.
param(
    [string]$Build = (Join-Path $PSScriptRoot '..\build'),
    [switch]$Trust
)
$ErrorActionPreference = 'Stop'
$subject = 'CN=Rare6Media'
$cer = Join-Path $PSScriptRoot '..\installer\Rare6Media.cer'

$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object { $_.Subject -eq $subject -and $_.NotAfter -gt (Get-Date).AddDays(30) } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    Write-Host "Creating the $subject code-signing certificate (valid 5 years)..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -KeyAlgorithm RSA -KeyLength 3072 `
        -HashAlgorithm SHA256 -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddYears(5) -CertStoreLocation Cert:\CurrentUser\My
}
Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null

if ($Trust) {
    # CurrentUser stores: no admin needed, but Windows asks you to confirm adding a root certificate.
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
    Write-Host 'Certificate trusted for this Windows account.'
}

$stage = Join-Path (Resolve-Path $Build) 'stage'
$files = @((Join-Path $stage 'RareStudioApp.exe'), (Join-Path (Resolve-Path $Build) 'launcher\RareStudio.exe')) | Where-Object { Test-Path $_ }
foreach ($f in $files) {
    # Timestamped, so the signature stays valid after the certificate itself expires.
    $r = Set-AuthenticodeSignature -FilePath $f -Certificate $cert -HashAlgorithm SHA256 -TimestampServer 'http://timestamp.digicert.com'
    if ($r.Status -eq 'HashMismatch' -or -not $r.SignerCertificate) { throw "Signing $f failed: $($r.StatusMessage)" }
    Write-Host ("Signed {0}  ({1})" -f (Split-Path $f -Leaf), $r.Status)
}
