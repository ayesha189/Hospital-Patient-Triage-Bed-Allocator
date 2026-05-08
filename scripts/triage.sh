#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : triage.sh
# Purpose : Validate input, compute priority, send to admissions
# Usage   : ./triage.sh <name> <age> <severity 1-10>
# ============================================================

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <name> <age> <severity 1-10>"
    exit 1
fi

NAME=$1
AGE=$2
SEVERITY=$3

# Validate name
if [ -z "$NAME" ]; then
    echo "Error: Name cannot be empty"
    exit 1
fi

# Validate age is a number
if ! [[ "$AGE" =~ ^[0-9]+$ ]]; then
    echo "Error: Age must be a number"
    exit 1
fi

# Validate severity range
if ! [[ "$SEVERITY" =~ ^[0-9]+$ ]] || [ "$SEVERITY" -lt 1 ] || [ "$SEVERITY" -gt 10 ]; then
    echo "Error: Severity must be between 1 and 10"
    exit 1
fi

# Compute triage priority from severity
if   [ "$SEVERITY" -le 2 ]; then PRIORITY=1
elif [ "$SEVERITY" -le 4 ]; then PRIORITY=2
elif [ "$SEVERITY" -le 6 ]; then PRIORITY=3
elif [ "$SEVERITY" -le 8 ]; then PRIORITY=4
else                              PRIORITY=5
fi

echo "Patient: $NAME | Age: $AGE | Severity: $SEVERITY | Priority: $PRIORITY"

# Send to admissions via named FIFO (pipe)
FIFO="/tmp/triage_fifo"
if [ ! -p "$FIFO" ]; then
    echo "Error: Admissions not running (FIFO not found)"
    exit 1
fi

echo "$NAME $AGE $SEVERITY $PRIORITY" > "$FIFO"
echo "Patient $NAME sent to admissions successfully."
