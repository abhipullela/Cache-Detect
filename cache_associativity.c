/*
 * Code for Set Associativity Detection
 *
 * Method: conflict miss induction.
 * Fix working set = L3 size. Vary stride = L3/N for N = 1,2,4...
 * When N exceeds real associativity, all accesses map to the same
 * cache set and evict each other (conflict misses) → latency spike.
 * The N just before the spike = set associativity.
 * 
 * *****NOTE : WORKS ON WSL OR UBUNTU ONLY -- Mainly due to lscpu and CLOCK_MONOTONIC commands*****
 *
 * Compile:  gcc -O0 cache_associativity.c -o cache_associativity
 * Run:      ./cache_associativity
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CACHE_LINE_SIZE 1024
#define MAX_ARRAY_SIZE (128 * 1024 * 1024)
#define REPEAT 500
#define MAX_WAYS 64

/*
 * L3_SIZE_BYTES — set this to your detected L3 size from cache_size.c
 * Common values: 6MB, 8MB, 12MB, 16MB, 32MB
 * Default: 16MB (As per previous experiment)
 */
#define L3_SIZE_BYTES (16 * 1024 * 1024)

// Timer function
static long long get_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Latency Measurement
double measure_latency(volatile char *buf, size_t array_size, size_t stride)
{
    size_t num_accesses = array_size / stride;

    // Warm-up : force OS to map physical pages before timing 
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

/*
 * SET ASSOCIATIVITY DETECTION
 *
 * How it works:
 *   - A cache is organised into S sets, each with N ways (slots).
 *   - Every memory address maps to exactly one set.
 *   - If you access more addresses than there are ways in a set,
 *     the oldest entry gets evicted — a conflict miss.
 *
 * The experiment:
 *   - Fix working set = L3 size so we stay inside L3.
 *   - Stride at L3/N — forces all accesses into the same N sets.
 *   - When N > actual ways, every access evicts the previous one.
 *   - Latency spikes → we found the associativity.
 */

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
    // System information
    printf("==============================================\n");
    printf("     System Hardware Information\n");
    printf("==============================================\n");
    system("lscpu | grep -E 'Model name|Socket|Core|Thread|L1|L2|L3'");
    printf("\n");

    printf("==============================================\n");
    printf("     Set Associativity Detection\n");
    printf("==============================================\n");
    printf("  Cache line size : %d bytes\n", CACHE_LINE_SIZE);
    printf("  L3 size used    : %d MB\n",    L3_SIZE_BYTES / (1024 * 1024));
    printf("  Repeat count    : %d\n\n",     REPEAT);

    // Allocate buffer 
    volatile char *buf = (volatile char *)malloc(MAX_ARRAY_SIZE); // Allocating memory 128MB
    if (!buf) { fprintf(stderr, "malloc failed.\n"); return 1; } // Error Handling
    memset((void *)buf, 1, MAX_ARRAY_SIZE); // Initialising the memory

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
