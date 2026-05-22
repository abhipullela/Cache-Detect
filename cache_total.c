/*
 *
 * ******NOTE : WORKS ON WSL OR UBUNTU ONLY -- Mainly due to lscpu and CLOCK_MONOTONIC commands*******
 * ─────────────────────────────────────────────────────────────
 * Compile:  gcc -O0 cache_total.c -o cache_total -lm
 * Run:      ./cache_total
 * ─────────────────────────────────────────────────────────────
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CACHE_LINE_SIZE 2048   
#define MAX_ARRAY_SIZE (128 * 1024 * 1024)    
#define REPEAT 500 
#define MAX_WAYS 64
#define L3_SIZE_BYTES (16 * 1024 * 1024)

// Timer Function
static long long get_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

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

int detect_associativity(volatile char *buf, size_t l3_size)
{
    printf("--- Set Associativity Detection ---\n\n");
    printf("  Working set fixed at : %zu KB\n", l3_size / 1024);
    printf("  Spike threshold      : 1.4x previous latency\n\n");
    printf("  %-10s  %-18s  %-20s  %s\n",
           "Ways (N)", "Stride (bytes)", "Latency (ns/access)", "Note");
    printf("  %-10s  %-18s  %-20s  %s\n",
           "--------", "--------------", "-------------------", "----");

    double prev_lat = 0.0;
    int    detected = -1;

    for (int n = 1; n <= MAX_WAYS; n *= 2) {
        size_t stride = l3_size / n;

        // Stride must be at least one cache line 
        if (stride < CACHE_LINE_SIZE)
            break;

        double lat = measure_latency(buf, l3_size, stride);

        // Spike detection
        char note[64] = "-";
        if (prev_lat > 0.0 && lat > prev_lat * 1.4 && detected == -1) {
            detected = n / 2;
            snprintf(note, sizeof(note),
                     "*** SPIKE! Associativity ~ %d-way ***", detected);
        }

        printf("  %-10d  %-18zu  %-20.2f  %s\n", n, stride, lat, note);

        prev_lat = lat;
    }

    return detected;
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
    printf("  Cache Size & Set Associativity Detector\n");
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

    // Run associativity detection 
    int assoc = detect_associativity(buf, L3_SIZE_BYTES);

    // Result 
    printf("\n==============================================\n");
    printf("         Set Associativity Result\n");
    printf("==============================================\n");
    if (assoc > 0)
        printf("  [RESULT] Detected set associativity : %d-way\n", assoc);
    else
        printf("  [RESULT] Could not clearly detect associativity.\n");
    printf("  Note: associativity result may not match hardware\n");
    printf("        exactly — this is expected and acceptable.\n");
    printf("==============================================\n\n");

    printf("Run 'lscpu | grep -i cache' to compare with hardware.\n\n");

    free((void *)buf);
    
    return 0;
}