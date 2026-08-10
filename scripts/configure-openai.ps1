$ErrorActionPreference = 'Stop'

$secureKey = Read-Host 'Paste your OpenAI API key (input is hidden)' -AsSecureString
$pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
try {
    $plainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    if ([string]::IsNullOrWhiteSpace($plainKey)) { throw 'The API key cannot be empty.' }
    [Environment]::SetEnvironmentVariable('OPENAI_API_KEY', $plainKey,
                                           [EnvironmentVariableTarget]::User)
} finally {
    if ($pointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
    $plainKey = $null
}

Write-Host 'OPENAI_API_KEY configured for this Windows user.' -ForegroundColor Green
Write-Host 'Close Ableton completely and open it again before testing GPT generation.'
