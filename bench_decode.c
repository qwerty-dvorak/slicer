/* _POSIX_C_SOURCE exposes clock_gettime / struct timespec under -std=c99 */
#define _POSIX_C_SOURCE 199309L

/*
 * bench_decode.c - standalone PNG decode benchmark harness
 *
 * Decodes a PNG file N times and reports:
 *   - total wall-clock time
 *   - per-iteration time (mean, min, max)
 *   - throughput in MB/s (raw file size / time)
 *
 * Usage:
 *   bench_decode <image.png> [iterations]
 *
 * Build (see Makefile targets: bench, bench-perf, bench-prof):
 *   cc -O2 -o build/bench_decode bench_decode.c image.c png_decoder.c -ldl
 * -pthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "image.h"
#include "png_decoder.h"

/* ------------------------------------------------------------------ */
/* Portable monotonic clock                                             */
/* ------------------------------------------------------------------ */

static double
now_seconds (void)
{
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime (CLOCK_MONOTONIC, &ts);
#else
    /* fallback: wall clock */
    clock_gettime (CLOCK_REALTIME, &ts);
#endif
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static long
file_size_bytes (const char *path)
{
    FILE *f = fopen (path, "rb");
    long sz;
    if (!f)
        return -1;
    if (fseek (f, 0, SEEK_END) != 0)
        {
            fclose (f);
            return -1;
        }
    sz = ftell (f);
    fclose (f);
    return sz;
}

static void
print_separator (void)
{
    puts ("--------------------------------------------------------------");
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int
main (int argc, char **argv)
{
    const char *path;
    int iterations = 100;
    long fsize;
    double *samples;
    double t_total = 0.0;
    double t_min = 1e30;
    double t_max = 0.0;
    double t_mean;
    double throughput_mbs;
    int i;

    /* ---- argument parsing ---- */
    if (argc < 2 || argc > 3)
        {
            fprintf (
                stderr, "usage: %s <image.png|ppm> [iterations]\n", argv[0]
            );
            fprintf (stderr, "  iterations defaults to 100\n");
            return 1;
        }

    path = argv[1];

    if (argc == 3)
        {
            char *end = NULL;
            long v = strtol (argv[2], &end, 10);
            if (!end || *end != '\0' || v <= 0 || v > 1000000L)
                {
                    fprintf (
                        stderr,
                        "error: iterations must be a positive integer\n"
                    );
                    return 1;
                }
            iterations = (int)v;
        }

    /* ---- file info ---- */
    fsize = file_size_bytes (path);
    if (fsize < 0)
        {
            fprintf (stderr, "error: cannot open '%s'\n", path);
            return 1;
        }

    /* ---- warm-up: one decode to page-in the file and libraries ---- */
    {
        image_t warm = { 0 };
        if (!image_load (path, &warm))
            {
                fprintf (
                    stderr,
                    "error: failed to decode '%s' during warm-up\n",
                    path
                );
                return 1;
            }
        printf ("image: %s\n", path);
        printf (
            "dimensions: %d x %d  has_alpha=%d\n",
            warm.width,
            warm.height,
            warm.has_alpha
        );
        printf (
            "file size: %ld bytes (%.2f KiB)\n", fsize, (double)fsize / 1024.0
        );
        printf (
            "pixel data: %d x %d x 4 = %d bytes (%.2f KiB)\n",
            warm.width,
            warm.height,
            warm.width * warm.height * 4,
            (double)(warm.width * warm.height * 4) / 1024.0
        );
        printf ("iterations: %d\n", iterations);
        image_free (&warm);
    }
    print_separator ();

    /* ---- allocate sample array ---- */
    samples = (double *)malloc ((size_t)iterations * sizeof (double));
    if (!samples)
        {
            fprintf (stderr, "error: out of memory for sample array\n");
            return 1;
        }

    /* ---- timed loop ---- */
    printf ("running benchmark...\n");
    for (i = 0; i < iterations; i++)
        {
            image_t img = { 0 };
            double t0, t1, elapsed;

            t0 = now_seconds ();
            if (!image_load (path, &img))
                {
                    fprintf (
                        stderr, "error: decode failed on iteration %d\n", i
                    );
                    free (samples);
                    return 1;
                }
            t1 = now_seconds ();

            image_free (&img);

            elapsed = t1 - t0;
            samples[i] = elapsed;
            t_total += elapsed;
            if (elapsed < t_min)
                t_min = elapsed;
            if (elapsed > t_max)
                t_max = elapsed;
        }

    /* ---- statistics ---- */
    t_mean = t_total / (double)iterations;

    /* throughput: compressed bytes decoded per second */
    throughput_mbs = ((double)fsize / (1024.0 * 1024.0)) / t_mean;

    /* compute stddev */
    {
        double variance = 0.0;
        double stddev;
        for (i = 0; i < iterations; i++)
            {
                double diff = samples[i] - t_mean;
                variance += diff * diff;
            }
        variance /= (double)iterations;
        stddev = 0.0;
        /* manual sqrt via Newton's method to avoid requiring -lm */
        if (variance > 0.0)
            {
                double x = variance;
                int j;
                for (j = 0; j < 64; j++)
                    {
                        double nx = 0.5 * (x + variance / x);
                        if (nx >= x)
                            break;
                        x = nx;
                    }
                stddev = x;
            }

        print_separator ();
        printf ("results (%d iterations):\n", iterations);
        printf ("  total   : %.4f s\n", t_total);
        printf (
            "  mean    : %.4f ms  (%.2f us)\n", t_mean * 1e3, t_mean * 1e6
        );
        printf ("  stddev  : %.4f ms\n", stddev * 1e3);
        printf ("  min     : %.4f ms\n", t_min * 1e3);
        printf ("  max     : %.4f ms\n", t_max * 1e3);
        printf ("  throughput (file MB/s) : %.1f MB/s\n", throughput_mbs);
        printf (
            "  throughput (pixel MB/s): %.1f MB/s\n",
            ((double)fsize * (double)iterations / (1024.0 * 1024.0)) / t_total
        );
    }
    print_separator ();

    /* ---- percentile histogram (10 buckets) ---- */
    {
        /* simple bucket histogram over [t_min, t_max] */
#define N_BUCKETS 10
        int counts[N_BUCKETS] = { 0 };
        double range = t_max - t_min;
        printf ("latency histogram (ms):\n");
        if (range < 1e-12)
            {
                printf ("  all samples identical: %.4f ms\n", t_min * 1e3);
            }
        else
            {
                int b;
                for (i = 0; i < iterations; i++)
                    {
                        int bucket = (int)(((samples[i] - t_min) / range)
                                           * (N_BUCKETS - 1));
                        if (bucket < 0)
                            bucket = 0;
                        if (bucket >= N_BUCKETS)
                            bucket = N_BUCKETS - 1;
                        counts[bucket]++;
                    }
                for (b = 0; b < N_BUCKETS; b++)
                    {
                        double lo
                            = (t_min + range * (double)b / N_BUCKETS) * 1e3;
                        double hi
                            = (t_min + range * (double)(b + 1) / N_BUCKETS)
                              * 1e3;
                        int bar_len = counts[b] * 40 / iterations;
                        char bar[41];
                        int k;
                        for (k = 0; k < bar_len && k < 40; k++)
                            bar[k] = '#';
                        bar[bar_len < 40 ? bar_len : 40] = '\0';
                        printf (
                            "  [%6.3f - %6.3f ms] %4d | %s\n",
                            lo,
                            hi,
                            counts[b],
                            bar
                        );
                    }
            }
#undef N_BUCKETS
        print_separator ();
    }

    free (samples);
    return 0;
}
