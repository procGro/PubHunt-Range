Write-Host "Starting PubHunt Range Test" -ForegroundColor Green
Write-Host "Expected Hash160: be9d31fa3d712c1de1697eb61d1e41fcbab5b0e7" -ForegroundColor Cyan
Write-Host "Expected Public Key: 03abead16bdf149ca87efe567c86307b2dc95d73235a4d089d359126c4f021d2b2" -ForegroundColor Cyan
Write-Host "Key Range: 96-bit range starting at 800000000000000000000000" -ForegroundColor Cyan
Write-Host "----------------------------------" -ForegroundColor Yellow

# Ensure test.txt exists with the hash160
$hash160 = "be9d31fa3d712c1de1697eb61d1e41fcbab5b0e7"
Set-Content -Path "test.txt" -Value $hash160 -NoNewline

# Run PubHunt with the specified parameters
$command = ".\PubHunt\x64\Release\PubHunt.exe -t test.txt -gpu -range 0000000000000000000000000000000000000000800000000000000000000000:0000000000000000000000000000000000000000ffffffffffffffffffffffff"

Write-Host "Running command: $command" -ForegroundColor Yellow
Write-Host "----------------------------------" -ForegroundColor Yellow

# Execute the command
Invoke-Expression $command

Write-Host "----------------------------------" -ForegroundColor Yellow
Write-Host "Test completed. Check the output to verify if the public key was found:" -ForegroundColor Green
Write-Host "03abead16bdf149ca87efe567c86307b2dc95d73235a4d089d359126c4f021d2b2" -ForegroundColor Cyan 