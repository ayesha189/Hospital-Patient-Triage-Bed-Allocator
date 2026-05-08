CC     = gcc
CFLAGS = -Wall -Wextra -pthread -I src
SRC    = src

all: admissions patient_simulator

admissions: $(SRC)/admissions.c $(SRC)/hospital.h
	$(CC) $(CFLAGS) -o admissions $(SRC)/admissions.c

patient_simulator: $(SRC)/patient_simulator.c $(SRC)/hospital.h
	$(CC) $(CFLAGS) -o patient_simulator $(SRC)/patient_simulator.c

run: all
	./scripts/start_hospital.sh best

test: all
	./scripts/start_hospital.sh best &
	sleep 2
	./scripts/triage.sh Alice 25 2
	./scripts/triage.sh Bob   40 6
	./scripts/triage.sh Sara  30 9

clean:
	rm -f admissions patient_simulator
	rm -f /tmp/triage_fifo /tmp/discharge_fifo
	rm -f /tmp/admissions.pid
	ipcrm -M 0xBEDF00D 2>/dev/null || true
	echo "Cleaned."
