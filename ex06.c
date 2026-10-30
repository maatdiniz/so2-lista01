/*
 * Exercício 6 - Soma Total e Histograma Paralelo (MapReduce em Arquivo Grande)
 *
 * Requisitos:
 * - Lê um arquivo grande de inteiros e calcula:
 *   (a) soma total
 *   (b) histograma de frequências
 *   usando P threads em paralelo.
 * - Particiona o arquivo em blocos, faz "map" local em cada thread e "reduce"
 *   com exclusão mútua mínima.
 * - Mede o speedup para P = 1, 2, 4, 8.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <assert.h>

#define TOTAL_INTEGERS (16 * 1024 * 1024) /* 16 milhões de inteiros (~64 MB) */
#define HIST_BINS 100                     /* Valores de 0 a 99 */
#define DATA_FILENAME "dados_ex06.bin"
#define READ_CHUNK_INTS 65536

typedef struct {
    int thread_id;
    int fd;
    size_t start_index;
    size_t end_index;

    /* Acumuladores locais da fase MAP (sem contenção de lock) */
    int64_t local_sum;
    uint64_t local_hist[HIST_BINS];

    /* Ponteiros para o REDUCE global com exclusão mútua mínima */
    pthread_mutex_t *reduce_mutex;
    int64_t *global_sum;
    uint64_t *global_hist;
} map_worker_t;

static double diff_sec(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

static void generate_input_file(const char *filename, size_t count,
                                int64_t *expected_sum, uint64_t *expected_hist) {
    printf("Gerando arquivo '%s' com %zu inteiros (%zu MB)...\n",
           filename, count, (count * sizeof(int32_t)) / (1024 * 1024));

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open write");
        exit(EXIT_FAILURE);
    }

    int32_t *buf = (int32_t *)malloc(READ_CHUNK_INTS * sizeof(int32_t));
    unsigned int seed = 202602U;
    *expected_sum = 0;
    for (int b = 0; b < HIST_BINS; b++) {
        expected_hist[b] = 0;
    }

    size_t written = 0;
    while (written < count) {
        size_t batch = count - written;
        if (batch > READ_CHUNK_INTS) batch = READ_CHUNK_INTS;

        for (size_t i = 0; i < batch; i++) {
            int32_t val = (int32_t)(rand_r(&seed) % HIST_BINS);
            buf[i] = val;
            *expected_sum += val;
            expected_hist[val]++;
        }

        ssize_t bytes = write(fd, buf, batch * sizeof(int32_t));
        if (bytes != (ssize_t)(batch * sizeof(int32_t))) {
            perror("write");
            exit(EXIT_FAILURE);
        }
        written += batch;
    }

    free(buf);
    close(fd);
}

static void *map_reduce_worker(void *arg) {
    map_worker_t *w = (map_worker_t *)arg;
    w->local_sum = 0;
    for (int b = 0; b < HIST_BINS; b++) {
        w->local_hist[b] = 0;
    }

    int32_t *chunk = (int32_t *)malloc(READ_CHUNK_INTS * sizeof(int32_t));
    if (!chunk) {
        perror("malloc chunk");
        exit(EXIT_FAILURE);
    }

    /* Fase MAP: Lê o bloco atribuído via pread() e acumula localmente sem locks */
    size_t curr = w->start_index;
    while (curr < w->end_index) {
        size_t to_read = w->end_index - curr;
        if (to_read > READ_CHUNK_INTS) to_read = READ_CHUNK_INTS;

        off_t offset = (off_t)(curr * sizeof(int32_t));
        ssize_t bytes = pread(w->fd, chunk, to_read * sizeof(int32_t), offset);
        if (bytes <= 0) {
            break;
        }
        size_t ints_read = (size_t)bytes / sizeof(int32_t);

        for (size_t i = 0; i < ints_read; i++) {
            int32_t v = chunk[i];
            w->local_sum += v;
            w->local_hist[v]++;
        }
        curr += ints_read;
    }

    free(chunk);

    /* Fase REDUCE: Exclusão mútua mínima (uma única aquisição ao final do bloco) */
    pthread_mutex_lock(w->reduce_mutex);
    *(w->global_sum) += w->local_sum;
    for (int b = 0; b < HIST_BINS; b++) {
        w->global_hist[b] += w->local_hist[b];
    }
    pthread_mutex_unlock(w->reduce_mutex);

    return NULL;
}

static double run_parallel_test(int fd, int num_threads,
                                int64_t expected_sum, const uint64_t *expected_hist,
                                int64_t *out_sum, uint64_t *out_hist) {
    pthread_t *threads = (pthread_t *)calloc((size_t)num_threads, sizeof(pthread_t));
    map_worker_t *workers = (map_worker_t *)calloc((size_t)num_threads, sizeof(map_worker_t));
    pthread_mutex_t reduce_mutex = PTHREAD_MUTEX_INITIALIZER;

    *out_sum = 0;
    for (int b = 0; b < HIST_BINS; b++) {
        out_hist[b] = 0;
    }

    size_t base_block = TOTAL_INTEGERS / (size_t)num_threads;
    size_t remainder = TOTAL_INTEGERS % (size_t)num_threads;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    size_t offset = 0;
    for (int i = 0; i < num_threads; i++) {
        size_t block_len = base_block + ((size_t)i < remainder ? 1 : 0);
        workers[i].thread_id = i;
        workers[i].fd = fd;
        workers[i].start_index = offset;
        workers[i].end_index = offset + block_len;
        workers[i].reduce_mutex = &reduce_mutex;
        workers[i].global_sum = out_sum;
        workers[i].global_hist = out_hist;
        offset += block_len;

        pthread_create(&threads[i], NULL, map_reduce_worker, &workers[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = diff_sec(t0, t1);

    /* Valida correção da soma e do histograma */
    assert(*out_sum == expected_sum);
    for (int b = 0; b < HIST_BINS; b++) {
        assert(out_hist[b] == expected_hist[b]);
    }

    pthread_mutex_destroy(&reduce_mutex);
    free(threads);
    free(workers);
    return elapsed;
}

int main(void) {
    printf("=== EXERCÍCIO 6: SOMA E HISTOGRAMA PARALELO (MAP-REDUCE) ===\n\n");

    int64_t expected_sum = 0;
    uint64_t expected_hist[HIST_BINS];
    generate_input_file(DATA_FILENAME, TOTAL_INTEGERS, &expected_sum, expected_hist);

    int fd = open(DATA_FILENAME, O_RDONLY);
    if (fd < 0) {
        perror("open read");
        return EXIT_FAILURE;
    }

    int thread_counts[] = {1, 2, 4, 8};
    int n_configs = (int)(sizeof(thread_counts) / sizeof(thread_counts[0]));

    int64_t calc_sum = 0;
    uint64_t calc_hist[HIST_BINS];

    /* Aquecimento do cache de página do SO para medir de forma justa o escalonamento das threads */
    run_parallel_test(fd, 1, expected_sum, expected_hist, &calc_sum, calc_hist);

    printf("\n| Threads (P) | Tempo (s)    | Speedup (T1/TP) | Eficiência   | Soma Total Verificada |\n");
    printf("|-------------|--------------|-----------------|--------------|-----------------------|\n");

    double t1_time = 0.0;
    for (int i = 0; i < n_configs; i++) {
        int p = thread_counts[i];
        /* Média de 3 repetições para estabilidade estatística */
        double sum_time = 0.0;
        for (int rep = 0; rep < 3; rep++) {
            sum_time += run_parallel_test(fd, p, expected_sum, expected_hist, &calc_sum, calc_hist);
        }
        double avg_time = sum_time / 3.0;
        if (p == 1) {
            t1_time = avg_time;
        }
        double speedup = t1_time / avg_time;
        double efficiency = (speedup / p) * 100.0;

        printf("| %11d | %10.4f s | %14.2fx | %10.1f %% | %21lld |\n",
               p, avg_time, speedup, efficiency, (long long)calc_sum);
    }

    printf("\n--- Amostra do Histograma de Frequências (primeiras 10 classes de %d) ---\n", HIST_BINS);
    uint64_t total_counted = 0;
    for (int b = 0; b < HIST_BINS; b++) {
        total_counted += calc_hist[b];
        if (b < 10) {
            printf("Valor %2d: %8llu ocorrências (%.2f%%)\n",
                   b, (unsigned long long)calc_hist[b],
                   (100.0 * (double)calc_hist[b]) / (double)TOTAL_INTEGERS);
        }
    }
    printf("Total acumulado no histograma: %llu inteiros (100%% validado via assert).\n",
           (unsigned long long)total_counted);

    close(fd);
    unlink(DATA_FILENAME); /* Remove arquivo temporário ao final */
    return 0;
}

