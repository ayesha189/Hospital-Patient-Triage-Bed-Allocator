#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <time.h>

#define ICU_COUNT        4
#define ISOLATION_COUNT  4
#define GENERAL_COUNT    12
#define MAX_BEDS         40
#define WARD_SIZE        40
#define MAX_PATIENTS     50
#define MAX_QUEUE_SIZE   30
#define PAGE_SIZE        2

#define SHM_KEY          0xBEDF00D
#define TRIAGE_FIFO      "/tmp/triage_fifo"
#define DISCHARGE_FIFO   "/tmp/discharge_fifo"
#define SEM_ICU          "/sem_icu_limit"
#define SEM_ISOLATION    "/sem_isolation_limit"

typedef struct {
    int    patient_id;
    char   name[64];
    int    age;
    int    severity;
    int    priority;
    int    care_units;
    time_t arrival_time;
    int    assigned_bed;
} PatientRecord;

typedef struct {
    int  partition_id;
    int  start_unit;
    int  size;
    int  is_free;
    int  patient_id;
    char bed_type[16];
} BedPartition;

typedef struct {
    BedPartition partitions[MAX_BEDS];
    int          total_partitions;
    int          ward[WARD_SIZE];
    volatile int running;
    int          total_served;
} SharedMemory;

#endif
