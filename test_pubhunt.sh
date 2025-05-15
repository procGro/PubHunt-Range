#!/bin/bash

echo -e "\e[32mStarting PubHunt Range Test\e[0m"
echo -e "\e[36mExpected Hash160: be9d31fa3d712c1de1697eb61d1e41fcbab5b0e7\e[0m"
echo -e "\e[36mExpected Public Key: 03abead16bdf149ca87efe567c86307b2dc95d73235a4d089d359126c4f021d2b2\e[0m"
echo -e "\e[36mKey Range: 96-bit range starting at 800000000000000000000000\e[0m"
echo -e "\e[33m----------------------------------\e[0m"

# Ensure test.txt exists with the hash160
echo -n "be9d31fa3d712c1de1697eb61d1e41fcbab5b0e7" > test.txt

# Expected public key to find
EXPECTED_KEY="03abead16bdf149ca87efe567c86307b2dc95d73235a4d089d359126c4f021d2b2"

# Run PubHunt with the specified parameters
# Assuming the executable is directly in the PubHunt directory
COMMAND="/root/PubHunt-Range/PubHunt/PubHunt -t test.txt -gpu -range 0000000000000000000000000000000000000000800000000000000000000000:0000000000000000000000000000000000000000ffffffffffffffffffffffff"

# Uncomment if executable is in /bin subdirectory
# COMMAND="/root/PubHunt-Range/PubHunt/bin/PubHunt -t test.txt -gpu -range 0000000000000000000000000000000000000000800000000000000000000000:0000000000000000000000000000000000000000ffffffffffffffffffffffff"

echo -e "\e[33mRunning command: $COMMAND\e[0m"
echo -e "\e[33m----------------------------------\e[0m"

# Execute the command and save output to a file
$COMMAND | tee pubhunt_output.log

echo -e "\e[33m----------------------------------\e[0m"

# Check if the expected public key was found in the output
if grep -q "$EXPECTED_KEY" pubhunt_output.log; then
    echo -e "\e[32mSUCCESS! The expected public key was found:\e[0m"
    echo -e "\e[36m$EXPECTED_KEY\e[0m"
    
    # Extract and display the line containing the public key
    echo -e "\e[33mMatching line from output:\e[0m"
    grep -i "pubkey\|$EXPECTED_KEY" pubhunt_output.log
else
    echo -e "\e[31mFAILURE! The expected public key was NOT found in the output:\e[0m"
    echo -e "\e[36mExpected: $EXPECTED_KEY\e[0m"
    echo -e "\e[33mCheck pubhunt_output.log for details\e[0m"
fi 