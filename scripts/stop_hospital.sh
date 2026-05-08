#!/bin/bash
# ============================================================
# Script  : stop_hospital.sh
# Purpose : Shutdown hospital and clean all IPC resources
# ============================================================

echo "============================================"
echo "  Shutting down Hospital..."
echo "============================================"

if [ -f /tmp/admissions.pid ]; then
    PID=$(cat /tmp/admissions.pid)
    kill -SIGTERM "$PID" 2>/dev/null
    echo "Sent SIGTERM to admissions (PID: $PID)"
    sleep 2
    rm -f /tmp/admissions.pid
else
    echo "No running admissions found."
fi

# Clean shared memory
ipcrm -M 0xBEDF00D 2>/dev/null
echo "Shared memory cleaned."

# Clean semaphores
rm -f /dev/shm/sem.sem_icu_limit
rm -f /dev/shm/sem.sem_isolation_limit
echo "Semaphores cleaned."

# Clean FIFOs
rm -f /tmp/triage_fifo /tmp/discharge_fifo
echo "FIFOs cleaned."

echo "Hospital shutdown complete."
echo "============================================"
