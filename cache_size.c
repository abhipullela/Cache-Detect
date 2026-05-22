/*
 * Code for Cache Size Detection
 *
 *   Experiment  — Stride walk (latency vs array size)
 *
 * ******NOTE : WORKS ON WSL OR UBUNTU ONLY -- Mainly due to lscpu and CLOCK_MONOTONIC commands*******
 * ─────────────────────────────────────────────────────────────
 * Compile:  gcc -O0 cache_size.c -o cache_size -lm
 * Run:      ./cache_size
 * ─────────────────────────────────────────────────────────────
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CACHE_LINE_SIZE 2048   // Real CPUs usually have 64 bytes - but to detect L3 Cache usage we use 1024 bytes
#define MAX_ARRAY_SIZE (128 * 1024 * 1024) // 128 MB max buffer - ensures we exceed L1, L2 and L3 caches    
#define REPEAT 500 // Repeat the experiment 500 times

// Timer Function
static long long get_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/*
 * EXPERIMENT 1 — STRIDE WALK
 * 
 * Walk through buffer at CACHE_LINE_SIZE byte intervals.
 * Increase array size from 4 KB to 128 MB (doubling each step).
 * When working set exceeds a cache level, latency spikes —
 * revealing L1, L2, and L3 boundaries.
 */

double measure_latency(volatile char *buf, size_t array_size, size_t stride)
{
    size_t num_accesses = array_size / stride;

    // Warm-up: force OS to map physical pages before timing 
    for (size_t i = 0; i < array_size; i += stride)
        buf[i] = (char)i;

    // Timed loop 
    long long start = get_ns();
    for (int r = 0; r < REPEAT; r++)
        for (size_t i = 0; i < array_size; i += stride)
            (void)buf[i];
    long long end = get_ns();

    double total_accesses = (double)num_accesses * REPEAT;
    return (double)(end - start) / total_accesses;
}


int main(void)
{
    // System information - prints lscpu command outputs in the terminal
    printf("==============================================\n");
    printf("     System Hardware Information\n");
    printf("==============================================\n");
    system("lscpu | grep -E 'Model name|Socket|Core|Thread|L1|L2|L3'");
    printf("\n");

    printf("==============================================\n");
    printf("         Cache Size Detection\n");
    printf("==============================================\n");
    printf("  Cache line size : %d bytes\n", CACHE_LINE_SIZE);
    printf("  Max array size  : %d MB\n",    MAX_ARRAY_SIZE / (1024 * 1024));
    printf("  Repeat count    : %d\n\n",     REPEAT);

    // Allocate buffer 
    volatile char *buf = (volatile char *)malloc(MAX_ARRAY_SIZE); // Allocating memory 128MB
    if (!buf) { fprintf(stderr, "malloc failed.\n"); return 1; } // Error Handling
    memset((void *)buf, 1, MAX_ARRAY_SIZE); // Initialising the memory

    // Experiment 1: stride walk  
    printf("--- Experiment 1: Stride Walk ---\n\n");
    printf("  %-18s  %-22s  %s\n",
           "    Array size", " Working set (KB/MB)", "Latency (ns/access)");
    printf("  %-18s  %-22s  %s\n",
           "------------------", "--------------------", "-------------------");

    double prev_latency  = 0.0;
    double threshold_mul = 1.4;
    size_t detected_l3   = 0;
    int    spike_count   = 0;

    for (size_t sz = 4 * 1024; sz <= MAX_ARRAY_SIZE; sz *= 2)
    {
        double lat = measure_latency(buf, sz, CACHE_LINE_SIZE);

        char size_label[32], ws_label[32];
        if (sz < 1024 * 1024)
            snprintf(size_label, sizeof(size_label), "%6zu KB", sz / 1024);
        else
            snprintf(size_label, sizeof(size_label), "%6zu MB", sz / (1024 * 1024));
        snprintf(ws_label, sizeof(ws_label), "%s", size_label);

        printf("  %-18s  %-22s  %.2f", size_label, ws_label, lat);

        if (prev_latency > 0.0 && lat > prev_latency * threshold_mul) {
            spike_count++;
            printf("  *** SPIKE (cache miss boundary %d) ***", spike_count);
            if (spike_count == 3 && detected_l3 == 0)
                detected_l3 = sz / 2;
        }

        printf("\n");
        prev_latency = lat;
    }

    printf("\n----------------------------------------------\n");
    if (detected_l3 > 0)
        printf("  [RESULT] Stride Walk L3 size : ~%zu MB\n",
               detected_l3 / (1024 * 1024));
    else {
        detected_l3 = 8 * 1024 * 1024;
        printf("  No clear L3 spike found.\n");
        printf("  Tip: try changing CACHE_LINE_SIZE to 64 or 1024 or 2048.\n");
    }
    printf("----------------------------------------------\n");

    printf("Run 'lscpu | grep -i cache' to compare with hardware.\n\n");

    free((void *)buf);
    
    return 0;
}