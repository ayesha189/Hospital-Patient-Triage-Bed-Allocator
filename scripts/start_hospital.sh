#!/bin/bash
# ============================================================
# Script  : start_hospital.sh
# Purpose : Initialize IPC resources and launch admissions
# ============================================================

STRATEGY=${1:-best}

echo "============================================"
echo "  Starting Hospital Triage System"
echo "  Strategy: $STRATEGY"
echo "============================================"

# Clean old IPC resources if any
ipcrm -M 0xBEDF00D 2>/dev/null
rm -f /tmp/triage_fifo /tmp/discharge_fifo

echo "IPC resources initialized."
echo "Launching Admissions Manager..."

# Launch admissions in background
./admissions --strategy "$STRATEGY" &
ADMISSIONS_PID=$!
echo $ADMISSIONS_PID > /tmp/admissions.pid

sleep 1  # give it time to start

echo "Admissions Manager started (PID: $ADMISSIONS_PID)"
echo "Hospital is OPEN. Use triage.sh to admit patients."
echo "============================================"
