@echo off
echo Starting PubHunt Range Test
echo Expected Hash160: be9d31fa3d712c1de1697eb61d1e41fcbab5b0e7
echo Expected Public Key: 03abead16bdf149ca87efe567c86307b2dc95d73235a4d089d359126c4f021d2b2
echo Key Range: 96-bit range starting at 800000000000000000000000
echo ----------------------------------

REM Ensure test.txt exists with the hash160
echo be9d31fa3d712c1de1697eb61d1e41fcbab5b0e7 > test.txt

REM Run PubHunt with the specified parameters
echo Running PubHunt with test parameters...
echo ----------------------------------

.\PubHunt\x64\Release\PubHunt.exe -t test.txt -gpu -range 0000000000000000000000000000000000000000800000000000000000000000:0000000000000000000000000000000000000000ffffffffffffffffffffffff

echo ----------------------------------
echo Test completed. Check the output to verify if the public key was found:
echo 03abead16bdf149ca87efe567c86307b2dc95d73235a4d089d359126c4f021d2b2

pause 