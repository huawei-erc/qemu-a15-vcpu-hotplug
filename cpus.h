#ifndef QEMU_CPUS_H
#define QEMU_CPUS_H

/* cpus.c */
void qemu_init_cpu_loop(void);
void resume_all_vcpus(void);
void pause_all_vcpus(void);
void cpu_stop_current(void);

void cpu_synchronize_all_states(void);
void cpu_synchronize_all_post_reset(void);
void cpu_synchronize_all_post_init(void);

void qtest_clock_warp(int64_t dest);

/* vl.c */
extern int smp_cores;
extern int smp_threads;

void cpus_set_all_numa_nodes(void);
int cpus_smp_cpus_set(int cpu_n, const char *cpu_model);
void cpus_hotplug_complete(void);

void set_cpu_log(const char *optarg);
void set_cpu_log_filename(const char *optarg);
void list_cpus(FILE *f, fprintf_function cpu_fprintf, const char *optarg);

#endif
