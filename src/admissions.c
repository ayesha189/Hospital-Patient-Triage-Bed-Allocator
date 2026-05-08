/*
 * admissions.c - Hospital Admissions Manager
 * Compile: gcc -Wall -o admissions admissions.c -lpthread
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <pthread.h>
#include <semaphore.h>
#include "hospital.h"

/* ── Globals ───────────────────────────────────────────────────────────── */
static SharedMemory *shm      = NULL;
static int           shmid    = -1;
static sem_t        *sem_icu  = NULL;
static sem_t        *sem_iso  = NULL;
static int           strategy = 0;   /* 0=best 1=first 2=worst */

static pthread_mutex_t bed_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bed_free = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t q_mtx   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_ready  = PTHREAD_COND_INITIALIZER;

static PatientRecord queue[MAX_QUEUE_SIZE];
static int           qsize    = 0;
static int           next_id  = 1;

static PatientRecord admitted[MAX_PATIENTS];
static int           admit_count = 0;

static FILE *sched_log = NULL;
static FILE *mem_log   = NULL;

/* ── Signal handlers ───────────────────────────────────────────────────── */
static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void sigterm_handler(int sig) {
    (void)sig;
    if (shm) shm->running = 0;
    pthread_cond_broadcast(&q_ready);
    pthread_cond_broadcast(&bed_free);
}

/* ── Ward initialisation ───────────────────────────────────────────────── */
static void init_ward(void) {
    int idx = 0, unit = 0;
    for (int i = 0; i < ICU_COUNT; i++, idx++) {
        shm->partitions[idx] = (BedPartition){idx, unit, 3, 1, -1, "ICU"};
        unit += 3;
    }
    for (int i = 0; i < ISOLATION_COUNT; i++, idx++) {
        shm->partitions[idx] = (BedPartition){idx, unit, 2, 1, -1, "ISOLATION"};
        unit += 2;
    }
    for (int i = 0; i < GENERAL_COUNT; i++, idx++) {
        shm->partitions[idx] = (BedPartition){idx, unit, 1, 1, -1, "GENERAL"};
        unit += 1;
    }
    shm->total_partitions = idx;
    for (int i = 0; i < WARD_SIZE; i++) shm->ward[i] = -1;
    shm->running      = 1;
    shm->total_served = 0;
}

/* ── Allocators ────────────────────────────────────────────────────────── */
static int alloc_best(int units, const char *type) {
    int best = -1, diff = 9999;
    for (int i = 0; i < shm->total_partitions; i++) {
        BedPartition *p = &shm->partitions[i];
        if (p->is_free && strcmp(p->bed_type, type) == 0 && p->size >= units)
            if (p->size - units < diff) { diff = p->size - units; best = i; }
    }
    return best;
}

static int alloc_first(int units, const char *type) {
    for (int i = 0; i < shm->total_partitions; i++) {
        BedPartition *p = &shm->partitions[i];
        if (p->is_free && strcmp(p->bed_type, type) == 0 && p->size >= units) return i;
    }
    return -1;
}

static int alloc_worst(int units, const char *type) {
    int worst = -1, wsz = -1;
    for (int i = 0; i < shm->total_partitions; i++) {
        BedPartition *p = &shm->partitions[i];
        if (p->is_free && strcmp(p->bed_type, type) == 0 &&
            p->size >= units && p->size > wsz) { wsz = p->size; worst = i; }
    }
    return worst;
}

static int allocate_bed(int units, const char *type) {
    if (strategy == 1) return alloc_first(units, type);
    if (strategy == 2) return alloc_worst(units, type);
    return alloc_best(units, type);
}

/* ── Coalescing ────────────────────────────────────────────────────────── */
static void coalesce(void) {
    for (int i = 0; i < shm->total_partitions - 1; i++) {
        BedPartition *a = &shm->partitions[i];
        BedPartition *b = &shm->partitions[i + 1];
        if (a->is_free && b->is_free &&
            strcmp(a->bed_type, b->bed_type) == 0 &&
            a->start_unit + a->size == b->start_unit) {
            printf("[Coalesce] Merging partitions %d+%d (%s)\n",
                   a->partition_id, b->partition_id, a->bed_type);
            a->size += b->size;
            for (int j = i + 1; j < shm->total_partitions - 1; j++)
                shm->partitions[j] = shm->partitions[j + 1];
            shm->total_partitions--;
            i--;
        }
    }
}

/* ── Fragmentation logging ─────────────────────────────────────────────── */
static void log_frag(const char *event) {
    int total_free = 0, largest = 0, run = 0;
    for (int i = 0; i < shm->total_partitions; i++) {
        if (shm->partitions[i].is_free) {
            total_free += shm->partitions[i].size;
            run += shm->partitions[i].size;
            if (run > largest) largest = run;
        } else run = 0;
    }
    float frag = total_free > 0
                 ? (1.0f - (float)largest / total_free) * 100.0f : 0.0f;

    char ts[20]; time_t now = time(NULL);
    strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
    printf("[Mem] Free:%d Largest:%d Frag:%.1f%% (%s)\n",
           total_free, largest, frag, event);
    if (mem_log) {
    fprintf(mem_log,
            "[%s] %s | Free:%d | Largest:%d | Frag:%.1f%%\n",
            ts, event, total_free, largest, frag);
    fflush(mem_log);
}
}

/* ── Free bed ──────────────────────────────────────────────────────────── */
static void free_bed(int pid) {
    pthread_mutex_lock(&bed_mtx);
    for (int i = 0; i < shm->total_partitions; i++) {
        BedPartition *bp = &shm->partitions[i];
        if (bp->patient_id == pid) {            printf("[Discharge] Freeing bed %d | Patient %d\n", i, pid);
            int s = bp->start_unit, sz = bp->size;
            for (int j = s; j < s + sz; j++) shm->ward[j] = -1;
            char btype[16]; strcpy(btype, bp->bed_type);
            bp->is_free = 1; bp->patient_id = -1;
            coalesce();
            log_frag("BED_FREED");
            shm->total_served++;
            if (strcmp(btype, "ICU")       == 0) sem_post(sem_icu);
            if (strcmp(btype, "ISOLATION") == 0) sem_post(sem_iso);
            break;
        }
    }
    pthread_cond_broadcast(&bed_free);
    pthread_mutex_unlock(&bed_mtx);
}

/* ── Priority queue ────────────────────────────────────────────────────── */
static void enqueue(PatientRecord *p) {
    pthread_mutex_lock(&q_mtx);
    int i = qsize - 1;
    while (i >= 0 && queue[i].priority > p->priority) {
        queue[i + 1] = queue[i]; i--;
    }
    queue[i + 1] = *p; qsize++;
    printf("[Queue] %s (Priority %d) added | Queue size: %d\n",
           p->name, p->priority, qsize);
    pthread_cond_signal(&q_ready);
    pthread_mutex_unlock(&q_mtx);
}

/* ── Admit patient (fork + exec) ───────────────────────────────────────── */
static int care_units_for(int priority) {
    if (priority <= 2) return 3;
    if (priority == 3) return 2;
    return 1;
}

static const char *bed_type_for(int priority) {
    if (priority <= 2) return "ICU";
    if (priority == 3) return "ISOLATION";
    return "GENERAL";
}

static void admit(PatientRecord *p, int bidx) {
    BedPartition *bp = &shm->partitions[bidx];
    bp->is_free = 0; bp->patient_id = p->patient_id;
    for (int j = bp->start_unit; j < bp->start_unit + bp->size; j++)
        shm->ward[j] = p->patient_id;
    log_frag("ADMITTED");

    /* Paging simulation */
    int pages = (bp->size + PAGE_SIZE - 1) / PAGE_SIZE;
    int ifrag  = pages * PAGE_SIZE - bp->size;
    printf("[Page] Patient %d | Size:%d units | Pages:%d | InternalFrag:%d\n",
           p->patient_id, bp->size, pages, ifrag);

    if (sched_log) {
        fprintf(sched_log, "ADMIT | P%d %-15s | Priority:%d | Bed:%d(%s) | Time:%ld\n",
                p->patient_id, p->name, p->priority, bidx,
                bp->bed_type, (long)time(NULL));
        fflush(sched_log);
    }

    /* Anonymous pipe to pass patient name to child */
    int pfd[2]; pipe(pfd);
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[1]);
        char id_s[8], pri_s[8], fd_s[8];
        snprintf(id_s,  sizeof(id_s),  "%d", p->patient_id);
        snprintf(pri_s, sizeof(pri_s), "%d", p->priority);
        snprintf(fd_s,  sizeof(fd_s),  "%d", pfd[0]);
        execv("./patient_simulator",
              (char *[]){"./patient_simulator", id_s, pri_s,
                          bp->bed_type, fd_s, NULL});
        perror("execv failed");
        exit(1);
    }
    close(pfd[0]);
    write(pfd[1], p->name, strlen(p->name) + 1);
    close(pfd[1]);

    if (admit_count < MAX_PATIENTS)
        admitted[admit_count++] = *p;
}

/* ── Thread: Receptionist ─────────────────────────────────────────────── */
void *receptionist_fn(void *arg) {
    (void)arg;
    printf("[Receptionist] Thread started\n");
    int fd = open(TRIAGE_FIFO, O_RDWR);
    if (fd < 0) { perror("open triage_fifo"); return NULL; }

    fd_set rfds; struct timeval tv; char line[128];
    while (shm->running) {
        FD_ZERO(&rfds); FD_SET(fd, &rfds);
        tv.tv_sec = 1; tv.tv_usec = 0;
        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) continue;

        ssize_t n = read(fd, line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = '\0';

        PatientRecord p = {0};
        /* Format written by triage.sh: "name age severity priority" */
        if (sscanf(line, "%63s %d %d %d",
                   p.name, &p.age, &p.severity, &p.priority) < 4) continue;
        if (p.priority < 1 || p.priority > 5) continue;

        p.patient_id   = next_id++;
        p.arrival_time = time(NULL);
        p.care_units   = care_units_for(p.priority);
        enqueue(&p);
    }
    close(fd);
    return NULL;
}

/* ── Thread: Scheduler ────────────────────────────────────────────────── */
void *scheduler_fn(void *arg) {
    (void)arg;
    printf("[Scheduler] Thread started\n");
    while (1) {
        pthread_mutex_lock(&q_mtx);
        while (qsize == 0 && shm->running)
            pthread_cond_wait(&q_ready, &q_mtx);
        if (!shm->running && qsize == 0) { pthread_mutex_unlock(&q_mtx); break; }

        PatientRecord p = queue[0];
        for (int i = 0; i < qsize - 1; i++) queue[i] = queue[i + 1];
        qsize--;
        pthread_mutex_unlock(&q_mtx);

        const char *btype = bed_type_for(p.priority);
        if (strcmp(btype, "ICU")       == 0) sem_wait(sem_icu);
        if (strcmp(btype, "ISOLATION") == 0) sem_wait(sem_iso);

        pthread_mutex_lock(&bed_mtx);
        int bidx;
        while ((bidx = allocate_bed(p.care_units, btype)) < 0) {
            printf("[Scheduler] No bed for patient %d, waiting...\n", p.patient_id);
            pthread_cond_wait(&bed_free, &bed_mtx);
        }
        admit(&p, bidx);
        pthread_mutex_unlock(&bed_mtx);
    }
    return NULL;
}

/* ── Thread: Discharge reader ─────────────────────────────────────────── */
void *discharge_reader_fn(void *arg) {
    (void)arg;
    int fd = open(DISCHARGE_FIFO, O_RDWR);
    if (fd < 0) { perror("open discharge_fifo"); return NULL; }

    fd_set rfds; struct timeval tv; char buf[32];
    while (shm->running) {
        FD_ZERO(&rfds); FD_SET(fd, &rfds);
        tv.tv_sec = 1; tv.tv_usec = 0;
        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) continue;
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        int pid = atoi(buf);
        if (pid > 0) free_bed(pid);
    }
    close(fd);
    return NULL;
}

/* ── Thread: Nurse (one per bed type) ────────────────────────────────── */
void *nurse_fn(void *arg) {
    char *btype = (char *)arg;
    printf("[Nurse-%s] Thread started\n", btype);
    pthread_mutex_lock(&bed_mtx);
    while (shm->running) {
        pthread_cond_wait(&bed_free, &bed_mtx);
        int free_cnt = 0;
        for (int i = 0; i < shm->total_partitions; i++)
            if (shm->partitions[i].is_free &&
                strcmp(shm->partitions[i].bed_type, btype) == 0)
                free_cnt++;
        if (free_cnt > 0)
            printf("[Nurse-%s] %d free bed(s) in ward\n", btype, free_cnt);
    }
    pthread_mutex_unlock(&bed_mtx);
    return NULL;
}

/* ── Scheduling simulations ───────────────────────────────────────────── */
static void sim_fcfs(void) {
    if (!sched_log || admit_count == 0) return;
    fprintf(sched_log, "\n=== FCFS SIMULATION ===\n");
    fprintf(sched_log, "%-5s %-15s %-6s %-6s %-10s\n",
            "ID", "Name", "Burst", "Wait", "Turnaround");
    int t = 0; float tw = 0, tta = 0;
    for (int i = 0; i < admit_count; i++) {
        int burst = care_units_for(admitted[i].priority) * 3;
        int wait  = t;
        fprintf(sched_log, "%-5d %-15s %-6d %-6d %-10d\n",
                admitted[i].patient_id, admitted[i].name,
                burst, wait, wait + burst);
        tw += wait; tta += wait + burst; t += burst;
    }
    fprintf(sched_log, "Avg Wait: %.2f | Avg Turnaround: %.2f\n\n",
            tw / admit_count, tta / admit_count);
}

static void sim_priority(void) {
    if (!sched_log || admit_count == 0) return;
    PatientRecord sorted[MAX_PATIENTS];
    memcpy(sorted, admitted, admit_count * sizeof(PatientRecord));
    for (int i = 0; i < admit_count - 1; i++)
        for (int j = i + 1; j < admit_count; j++)
            if (sorted[j].priority < sorted[i].priority) {
                PatientRecord tmp = sorted[i];
                sorted[i] = sorted[j]; sorted[j] = tmp;
            }
    fprintf(sched_log, "=== PRIORITY SCHEDULING SIMULATION ===\n");
    int t = 0; float tw = 0, tta = 0;
    for (int i = 0; i < admit_count; i++) {
        int burst = care_units_for(sorted[i].priority) * 3;
        int wait  = t;
        fprintf(sched_log, "P%-4d %-15s Pri:%d Wait:%-4d TAT:%d\n",
                sorted[i].patient_id, sorted[i].name,
                sorted[i].priority, wait, wait + burst);
        tw += wait; tta += wait + burst; t += burst;
    }
    fprintf(sched_log, "Avg Wait: %.2f | Avg Turnaround: %.2f\n",
            tw / admit_count, tta / admit_count);
}

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--strategy") == 0) {
            if      (strcmp(argv[i+1], "first") == 0) strategy = 1;
            else if (strcmp(argv[i+1], "worst") == 0) strategy = 2;
        }

    mkfifo(TRIAGE_FIFO,    0666);
    mkfifo(DISCHARGE_FIFO, 0666);
    mkdir("logs", 0755);
    sched_log = fopen("logs/schedule_log.txt", "w");
    mem_log   = fopen("logs/memory_log.txt",   "w");

    shmid = shmget(SHM_KEY, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (shmid < 0) { perror("shmget"); return 1; }
    shm = (SharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); return 1; }
    init_ward();

    sem_unlink(SEM_ICU); sem_unlink(SEM_ISOLATION);
    sem_icu = sem_open(SEM_ICU,       O_CREAT, 0666, ICU_COUNT);
    sem_iso = sem_open(SEM_ISOLATION, O_CREAT, 0666, ISOLATION_COUNT);

    struct sigaction sa = {0};
    sa.sa_handler = sigchld_handler; sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    sa.sa_handler = sigterm_handler; sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    printf("╔══════════════════════════════════════╗\n");
    printf("║   Hospital Patient Triage System     ║\n");
    printf("╠══════════════════════════════════════╣\n");
    printf("║  ICU:%d  Isolation:%d  General:%d      ║\n",
           ICU_COUNT, ISOLATION_COUNT, GENERAL_COUNT);
    printf("║  Strategy: %-25s║\n",
           strategy == 0 ? "Best-Fit" :
           strategy == 1 ? "First-Fit" : "Worst-Fit");
    printf("╚══════════════════════════════════════╝\n");
    fflush(stdout);

    pthread_t rec_t, sched_t, dis_t, nurse_t[3];
    char *ntypes[] = {"ICU", "GENERAL", "ISOLATION"};
    pthread_create(&rec_t,   NULL, receptionist_fn,    NULL);
    pthread_create(&sched_t, NULL, scheduler_fn,       NULL);
    pthread_create(&dis_t,   NULL, discharge_reader_fn,NULL);
    for (int i = 0; i < 3; i++)
        pthread_create(&nurse_t[i], NULL, nurse_fn, ntypes[i]);

    pause();   /* wait for SIGTERM */

    shm->running = 0;
    pthread_cond_broadcast(&q_ready);
    pthread_cond_broadcast(&bed_free);

    sim_fcfs();
    sim_priority();
    if (sched_log) fclose(sched_log);
    if (mem_log)   fclose(mem_log);

    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    sem_close(sem_icu);  sem_unlink(SEM_ICU);
    sem_close(sem_iso);  sem_unlink(SEM_ISOLATION);
    unlink(TRIAGE_FIFO);
    unlink(DISCHARGE_FIFO);

    printf("\nShutdown complete. Total patients served: %d\n", shm->total_served);
    return 0;
}
