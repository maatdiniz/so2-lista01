/*
 * Exercício 2 - Buffer Circular com Produtores e Consumidores
 *
 * Requisitos:
 * - Buffer circular de tamanho N acessado por múltiplos produtores e consumidores.
 * - Usa pthread_mutex e variáveis de condição (not_full, not_empty) para exclusão mútua
 *   e espera ativa zero.
 * - Gera itens com tempos aleatórios.
 * - Coleta estatísticas de throughput (itens/s) e tempo médio de espera.
 * - Demonstra, por experimento, como o tamanho do buffer N afeta o desempenho.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 4
#define ITEMS_PER_PRODUCER 150
#define TOTAL_ITEMS (NUM_PRODUCERS * ITEMS_PER_PRODUCER)

typedef struct {
    int id;
    struct timespec enqueued_at;
} item_t;

typedef struct {
    item_t *data;
    int capacity;
    int head;
    int tail;
    int count;

    int produced_total;
    int consumed_total;

    double total_prod_wait_ms;
    double total_cons_wait_ms;
    double total_queue_latency_ms;

    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} circular_buffer_t;

typedef struct {
    int thread_id;
    unsigned int seed;
    circular_buffer_t *buf;
} worker_arg_t;

static double diff_timespec_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 +
           (end.tv_nsec - start.tv_nsec) / 1e6;
}

static void buffer_init(circular_buffer_t *cb, int capacity) {
    cb->data = (item_t *)calloc((size_t)capacity, sizeof(item_t));
    cb->capacity = capacity;
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
    cb->produced_total = 0;
    cb->consumed_total = 0;
    cb->total_prod_wait_ms = 0.0;
    cb->total_cons_wait_ms = 0.0;
    cb->total_queue_latency_ms = 0.0;
    pthread_mutex_init(&cb->mutex, NULL);
    pthread_cond_init(&cb->not_full, NULL);
    pthread_cond_init(&cb->not_empty, NULL);
}

static void buffer_destroy(circular_buffer_t *cb) {
    free(cb->data);
    pthread_mutex_destroy(&cb->mutex);
    pthread_cond_destroy(&cb->not_full);
    pthread_cond_destroy(&cb->not_empty);
}

static void *producer_thread(void *arg) {
    worker_arg_t *w = (worker_arg_t *)arg;
    circular_buffer_t *cb = w->buf;

    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        /* Simula tempo aleatório de geração do item (50 a 450 us) */
        usleep((useconds_t)(50 + (rand_r(&w->seed) % 400)));

        struct timespec wait_start, wait_end;
        clock_gettime(CLOCK_MONOTONIC, &wait_start);

        pthread_mutex_lock(&cb->mutex);
        while (cb->count == cb->capacity) {
            pthread_cond_wait(&cb->not_full, &cb->mutex);
        }
        clock_gettime(CLOCK_MONOTONIC, &wait_end);

        cb->total_prod_wait_ms += diff_timespec_ms(wait_start, wait_end);

        item_t item;
        item.id = w->thread_id * 10000 + i;
        item.enqueued_at = wait_end;

        cb->data[cb->tail] = item;
        cb->tail = (cb->tail + 1) % cb->capacity;
        cb->count++;
        cb->produced_total++;

        pthread_cond_signal(&cb->not_empty);
        pthread_mutex_unlock(&cb->mutex);
    }
    return NULL;
}

static void *consumer_thread(void *arg) {
    worker_arg_t *w = (worker_arg_t *)arg;
    circular_buffer_t *cb = w->buf;

    while (true) {
        struct timespec wait_start, wait_end;
        clock_gettime(CLOCK_MONOTONIC, &wait_start);

        pthread_mutex_lock(&cb->mutex);
        while (cb->count == 0 && cb->consumed_total < TOTAL_ITEMS) {
            pthread_cond_wait(&cb->not_empty, &cb->mutex);
        }

        if (cb->count == 0 && cb->consumed_total >= TOTAL_ITEMS) {
            pthread_mutex_unlock(&cb->mutex);
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &wait_end);
        cb->total_cons_wait_ms += diff_timespec_ms(wait_start, wait_end);

        item_t item = cb->data[cb->head];
        cb->head = (cb->head + 1) % cb->capacity;
        cb->count--;
        cb->consumed_total++;

        cb->total_queue_latency_ms += diff_timespec_ms(item.enqueued_at, wait_end);

        if (cb->consumed_total >= TOTAL_ITEMS) {
            /* Acorda demais consumidores caso todos os itens tenham terminado */
            pthread_cond_broadcast(&cb->not_empty);
        }
        pthread_cond_signal(&cb->not_full);
        pthread_mutex_unlock(&cb->mutex);

        /* Simula tempo aleatório de consumo (80 a 520 us) */
        usleep((useconds_t)(80 + (rand_r(&w->seed) % 440)));
    }
    return NULL;
}

static void run_experiment(int buffer_size) {
    circular_buffer_t cb;
    buffer_init(&cb, buffer_size);

    pthread_t prods[NUM_PRODUCERS];
    pthread_t cons[NUM_CONSUMERS];
    worker_arg_t p_args[NUM_PRODUCERS];
    worker_arg_t c_args[NUM_CONSUMERS];

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        p_args[i].thread_id = i;
        p_args[i].seed = 12345U + (unsigned int)(i * 101 + buffer_size);
        p_args[i].buf = &cb;
        pthread_create(&prods[i], NULL, producer_thread, &p_args[i]);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        c_args[i].thread_id = i;
        c_args[i].seed = 67890U + (unsigned int)(i * 202 + buffer_size);
        c_args[i].buf = &cb;
        pthread_create(&cons[i], NULL, consumer_thread, &c_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(prods[i], NULL);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(cons[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed_sec = diff_timespec_ms(t_start, t_end) / 1000.0;
    double throughput = cb.consumed_total / elapsed_sec;
    double avg_prod_wait = cb.total_prod_wait_ms / cb.produced_total;
    double avg_cons_wait = cb.total_cons_wait_ms / cb.consumed_total;
    double avg_total_wait = (cb.total_prod_wait_ms + cb.total_cons_wait_ms) / (2.0 * TOTAL_ITEMS);

    printf("| %10d | %10.4f s | %14.1f | %15.4f ms | %15.4f ms | %14.4f ms |\n",
           buffer_size, elapsed_sec, throughput, avg_prod_wait, avg_cons_wait, avg_total_wait);

    buffer_destroy(&cb);
}

int main(void) {
    int sizes[] = {1, 2, 5, 10, 25, 50, 100};
    int num_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));

    printf("=== EXERCÍCIO 2: BUFFER CIRCULAR (PRODUTORES / CONSUMIDORES) ===\n");
    printf("Configuração: %d Produtores, %d Consumidores, %d itens totais por teste\n\n",
           NUM_PRODUCERS, NUM_CONSUMERS, TOTAL_ITEMS);

    printf("| Tamanho N  | Tempo Total  | Throughput/s   | Espera Produtor    | Espera Consum.     | Espera Média      |\n");
    printf("|------------|--------------|----------------|--------------------|--------------------|-------------------|\n");

    for (int i = 0; i < num_sizes; i++) {
        run_experiment(sizes[i]);
    }

    printf("\nConclusão experimental: Buffers muito pequenos (ex.: N=1 ou N=2) causam bloqueios frequentes\n");
    printf("de produtores quando há pequenas flutuações de tempo, elevando a espera do produtor e reduzindo\n");
    printf("o throughput. Ao aumentar N (ex.: N >= 10), as variações aleatórias são absorvidas pelo buffer.\n");

    return 0;
}
