/*
 * Command-line front end: parses options, runs the selected modes, prints the
 * table, and writes optional JSON. Keep the flow linear so the whole tool is
 * easy to follow.
 */
#include "bench.h"
#include "platform.h"
#include "runner.h"
#include "score.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CPUBENCH_BUILD_TYPE
#define CPUBENCH_BUILD_TYPE "unknown"
#endif

typedef struct {
    int single;
    int multi;
    int threads;
    const char *json_path;
    int calibrate;
} options;

typedef struct {
    bench_result results[BENCH_WORKLOAD_COUNT];
    double scores[BENCH_WORKLOAD_COUNT];
    double score;
    int ran;
} mode_result;

static const char *compiler_string(void)
{
    static char buffer[96];

#if defined(__clang__)
    snprintf(buffer, sizeof(buffer), "Clang %d.%d.%d",
             __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    snprintf(buffer, sizeof(buffer), "MSVC %d.%d",
             _MSC_VER / 100, _MSC_VER % 100);
#elif defined(__GNUC__)
    snprintf(buffer, sizeof(buffer), "GCC %d.%d.%d",
             __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    snprintf(buffer, sizeof(buffer), "Unknown compiler");
#endif

    return buffer;
}

static const char *os_name(void)
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "Other";
#endif
}

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: cpubench [options]\n"
            "\n"
            "  --single       Run single-core benchmarks\n"
            "  --multi        Run multi-core benchmarks\n"
            "  --threads N    Threads for multi-core runs (default: logical CPUs)\n"
            "  --json FILE    Write results to FILE as JSON\n"
            "  --help         Show this help\n"
            "\n"
            "With neither --single nor --multi, both are run.\n");
}

static void print_header(void)
{
    char cpu[256];
    plat_cpu_name(cpu, sizeof(cpu));

    printf("CPU: %s   Logical cores: %d   OS: %s   Compiler: %s   Build: %s\n",
           cpu, plat_logical_cores(), os_name(), compiler_string(),
           CPUBENCH_BUILD_TYPE);
    printf("---------------------------------------------------------\n");
}

static int run_calibrate(void)
{
    printf("Calibration (single-core, best of 3)\n");
    printf("---------------------------------------------------------\n");

    for (int i = 0; i < BENCH_WORKLOAD_COUNT; ++i) {
        bench_result result;
        if (run_single(&bench_workloads[i], &result) != 0)
            return -1;

        printf("%-10s %18.3f %-12s checksum 0x%016" PRIx64 "\n",
               bench_workloads[i].name, result.throughput,
               bench_workloads[i].unit, result.checksum);
    }

    printf("\nPut these throughput values into reference_throughput[] in "
           "src/score.c.\n");
    return 0;
}

static void json_escape(FILE *file, const char *text)
{
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p) {
        switch (*p) {
        case '"':
            fputs("\\\"", file);
            break;
        case '\\':
            fputs("\\\\", file);
            break;
        case '\b':
            fputs("\\b", file);
            break;
        case '\f':
            fputs("\\f", file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\r':
            fputs("\\r", file);
            break;
        case '\t':
            fputs("\\t", file);
            break;
        default:
            if (*p < 0x20)
                fprintf(file, "\\u%04x", (unsigned int)*p);
            else
                fputc(*p, file);
            break;
        }
    }
    fputc('"', file);
}

static void write_mode_json(FILE *file, const mode_result *mode, int threads)
{
    fprintf(file, "\n    \"score\": %.0f", mode->score);
    if (threads > 0)
        fprintf(file, ",\n    \"threads\": %d", threads);

    fputs(",\n    \"workloads\": {", file);
    for (int i = 0; i < BENCH_WORKLOAD_COUNT; ++i) {
        if (i > 0)
            fputs(",", file);
        fputs("\n      ", file);
        json_escape(file, bench_workloads[i].id);
        fputs(": {", file);
        fprintf(file, "\"score\": %.0f, \"throughput\": %.3f, \"unit\": ",
                mode->scores[i], mode->results[i].throughput);
        json_escape(file, bench_workloads[i].unit);
        fputs("}", file);
    }
    fputs("\n    }", file);
}

static int write_json(const char *path, int threads,
                      const mode_result *single, const mode_result *multi)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "error: cannot open '%s' for writing\n", path);
        return -1;
    }

    char cpu[256];
    plat_cpu_name(cpu, sizeof(cpu));

    fputs("{\n  \"cpu\": ", file);
    json_escape(file, cpu);
    fprintf(file, ",\n  \"logical_cores\": %d", plat_logical_cores());
    fputs(",\n  \"os\": ", file);
    json_escape(file, os_name());
    fputs(",\n  \"compiler\": ", file);
    json_escape(file, compiler_string());
    fputs(",\n  \"build\": ", file);
    json_escape(file, CPUBENCH_BUILD_TYPE);

    if (single->ran) {
        fputs(",\n  \"single\": {", file);
        write_mode_json(file, single, 0);
        fputs("\n  }", file);
    }
    if (multi->ran) {
        fputs(",\n  \"multi\": {", file);
        write_mode_json(file, multi, threads);
        fputs("\n  }", file);
    }
    if (single->ran && multi->ran) {
        double scaling = single->score > 0.0 ? multi->score / single->score
                                             : 0.0;
        fprintf(file, ",\n  \"scaling\": %.4f", scaling);
    }

    fputs("\n}\n", file);
    if (fclose(file) != 0) {
        fprintf(stderr, "error: could not finish writing '%s'\n", path);
        return -1;
    }
    return 0;
}

static int run_mode(int multi, int threads, mode_result *mode)
{
    for (int i = 0; i < BENCH_WORKLOAD_COUNT; ++i) {
        bench_result result;
        int rc = multi ? run_multi(&bench_workloads[i], threads, &result)
                       : run_single(&bench_workloads[i], &result);
        if (rc != 0)
            return -1;

        mode->results[i] = result;
        mode->scores[i] = score_from_throughput(i, result.throughput);
        if (mode->scores[i] <= 0.0) {
            fprintf(stderr,
                    "error: reference throughput for %s is not calibrated\n",
                    bench_workloads[i].name);
            return -1;
        }
    }

    mode->score = score_geomean(mode->scores, BENCH_WORKLOAD_COUNT);
    mode->ran = 1;
    return 0;
}

static void print_results(const mode_result *single, const mode_result *multi,
                          int threads)
{
    char multi_header[32];
    snprintf(multi_header, sizeof(multi_header), "Multi (%dT)", threads);

    printf("%-14s", "Test");
    if (single->ran)
        printf(" %12s", "Single");
    if (multi->ran)
        printf(" %14s", multi_header);
    printf("\n");

    for (int i = 0; i < BENCH_WORKLOAD_COUNT; ++i) {
        printf("%-14s", bench_workloads[i].name);
        if (single->ran)
            printf(" %12.0f", single->scores[i]);
        if (multi->ran)
            printf(" %14.0f", multi->scores[i]);
        printf("\n");
    }
    printf("---------------------------------------------------------\n");

    if (single->ran)
        printf("Single-Core Score: %.0f\n", single->score);
    if (multi->ran)
        printf("Multi-Core Score:  %.0f\n", multi->score);
    if (single->ran && multi->ran) {
        double scaling = single->score > 0.0 ? multi->score / single->score
                                             : 0.0;
        printf("Multi-core scaling: %.2fx\n", scaling);
    }
}

static int parse_args(int argc, char **argv, options *opt)
{
    opt->single = 0;
    opt->multi = 0;
    opt->threads = 0;
    opt->json_path = NULL;
    opt->calibrate = 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--single") == 0) {
            opt->single = 1;
        } else if (strcmp(arg, "--multi") == 0) {
            opt->multi = 1;
        } else if (strcmp(arg, "--threads") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --threads requires a value\n");
                usage(stderr);
                return -1;
            }
            char *end = NULL;
            long value = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || value < 1 || value > 1024) {
                fprintf(stderr,
                        "error: invalid thread count '%s' (expected 1..1024)\n",
                        argv[i]);
                return -1;
            }
            opt->threads = (int)value;
        } else if (strcmp(arg, "--json") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --json requires a file path\n");
                usage(stderr);
                return -1;
            }
            opt->json_path = argv[i];
        } else if (strcmp(arg, "--calibrate") == 0) {
            opt->calibrate = 1;
        } else if (strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 1;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n", arg);
            usage(stderr);
            return -1;
        }
    }

    if (!opt->single && !opt->multi) {
        opt->single = 1;
        opt->multi = 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    options opt;
    int parsed = parse_args(argc, argv, &opt);
    if (parsed == 1)
        return 0;
    if (parsed != 0)
        return 2;

    int threads = opt.threads > 0 ? opt.threads : plat_logical_cores();
    if (threads < 1)
        threads = 1;

    print_header();

    if (opt.calibrate)
        return run_calibrate() == 0 ? 0 : 1;

    mode_result single = {0};
    mode_result multi = {0};

    if (opt.single && run_mode(0, 1, &single) != 0)
        return 1;
    if (opt.multi && run_mode(1, threads, &multi) != 0)
        return 1;

    print_results(&single, &multi, threads);

    if (opt.json_path != NULL && write_json(opt.json_path, threads, &single,
                                            &multi) != 0)
        return 1;

    return 0;
}
