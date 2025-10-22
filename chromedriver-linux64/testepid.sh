#/bin/bash


./chromedriver &   # Run in background
pid=$!          # Store the PID in 'pid' variable
echo "PID: $pid"

wait $pid       # Wait for the process to finish
echo "Process $pid is finished"
