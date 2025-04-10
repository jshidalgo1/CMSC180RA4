#!/bin/bash

# File to transfer
FILE="hidalgo_lab04"

# Remote username
USER="cmsc180"

# Password for the remote user
PASSWORD="useruser"

# Loop through the range 01 to 18, skipping 09 and 12
for i in $(seq -w 01 18); do
    if [ "$i" == "09" ] || [ "$i" == "12" ]; then
        echo "Skipping drone${i}..."
        continue
    fi

    HOST="${USER}@drone${i}"
    echo "Transferring ${FILE} to ${HOST}..."
    sshpass -p "${PASSWORD}" scp -o StrictHostKeyChecking=no "${FILE}" "${HOST}:"
    if [ $? -eq 0 ]; then
        echo "Transfer to ${HOST} successful."
    else
        echo "Transfer to ${HOST} failed."
    fi
done