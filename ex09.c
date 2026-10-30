/*
 * Exercício 9 - Corrida de Revezamento / Equipe com Barreira Cíclica
 *
 * Requisitos:
 * - K threads representam uma equipe e todas precisam alcançar uma barreira para
 *   liberar a próxima "perna" (rodada) da prova.
 * - Usa pthread_barrier_t (com implementação portável baseada em mutex/condvar para
 *   sistemas como macOS onde pthread_barrier_t não faz parte do header padrão).
 * - Registra quantas rodadas são concluídas por minuto sob diferentes tamanhos de equipe.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/* Implementação portável de pthread_barrier_t com mutex + condvar caso não exista no SO */
#ifndef PTHREAD_BARRIER_SERIAL_THREAD
#define PTHREAD_BARRIER_SERIAL_THREAD 1

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned int threshold;
    unsigned int count;
    unsigned int generation;
} custom_barrier_t;

#define pthread_barrier_t custom_barrier_t

static int pthread_barrier_init(pthread_barrier_t *b, const void *attr, unsigned int count) {
    (void)attr;
    if (count == 0) return -1;
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->threshold = count;
    b->count = 0;
    b->generation = 0;
    return 0;
}

static int pthread_barrier_wait(pthread_barrier_t *b) {
    pthread_mutex_lock(&b->mutex);
    unsigned int gen = b->generation;
    b->count++;

    if (b->count == b->threshold) {
        b->generation++;
        b->count = 0;
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->mutex);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    while (gen == b->generation) {
        pthread_cond_wait(&b->cond, &b->mutex);
    }
    pthread_mutex_unlock(&b->mutex);
    return 0;
}

static int pthread_barrier_destroy(pthread_barrier_t *b) {
    pthread_mutex_destroy(&b->mutex);
    pthread_cond_destroy(&b->cond);
    return 0;
}
#endif

#define NUM_LEGS 400 /* Número de pernas/rodadas avaliadas em cada experimento */

typedef struct {
    int thread_id;
    unsigned int seed;
    pthread_barrier_t *barrier;
    int *rounds_completed;
} runner_arg_t;

static double diff_sec(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

static void *relay_runner(void *arg) {
    runner_arg_t *r = (runner_arg_t *)arg;

    for (int leg = 0; leg < NUM_LEGS; leg++) {
        /* Simula o tempo que o atleta leva para correr sua etapa (200 a 500 us) */
        usleep((useconds_t)(200 + (rand_r(&r->seed) % 300)));

        /* Sincroniza na barreira: ninguém inicia a próxima perna antes de todos chegarem */
        int rc = pthread_barrier_wait(r->barrier);
        if (rc == PTHREAD_BARRIER_SERIAL_THREAD) {
            (*r->rounds_completed)++;
        }
    }
    return NULL;
}

static void benchmark_team_size(int k_threads) {
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, (unsigned int)k_threads);

    pthread_t *threads = (pthread_t *)calloc((size_t)k_threads, sizeof(pthread_t));
    runner_arg_t *args = (runner_arg_t *)calloc((size_t)k_threads, sizeof(runner_arg_t));
    int rounds_completed = 0;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < k_threads; i++) {
        args[i].thread_id = i;
        args[i].seed = 42000U + (unsigned int)(i * 521 + k_threads * 17);
        args[i].barrier = &barrier;
        args[i].rounds_completed = &rounds_completed;
        pthread_create(&threads[i], NULL, relay_runner, &args[i]);
    }

    for (int i = 0; i < k_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_s = diff_sec(t0, t1);
    double rounds_per_sec = rounds_completed / elapsed_s;
    double rounds_per_min = rounds_per_sec * 60.0;

    printf("| %13d | %14d | %11.4f s | %15.1f | %18.1f |\n",
           k_threads, rounds_completed, elapsed_s, rounds_per_sec, rounds_per_min);

    pthread_barrier_destroy(&barrier);
    free(threads);
    free(args);
}

int main(void) {
    int team_sizes[] = {2, 4, 8, 16, 32};
    int n_sizes = (int)(sizeof(team_sizes) / sizeof(team_sizes[0]));

    printf("=== EXERCÍCIO 9: CORRIDA DE REVEZAMENTO COM BARREIRA (pthread_barrier_t) ===\n");
    printf("Rodadas (pernas) por experimento: %d\n\n", NUM_LEGS);

    printf("| Equipe (K)    | Rodadas Concl. | Tempo Total   | Rodadas / seg   | Rodadas / minuto   |\n");
    printf("|---------------|----------------|---------------|-----------------|--------------------|\n");

    for (int i = 0; i < n_sizes; i++) {
        benchmark_team_size(team_sizes[i]);
    }

    printf("\nAnálise: Como a barreira exige que todas as K threads alcancem o ponto de encontro,\n");
    printf("o tempo de cada perna é determinado pelo corredor mais lento da rodada (efeito straggler)\n");
    printf("somado ao custo de sincronização da barreira, reduzindo as rodadas/minuto conforme K cresce.\n");

    return 0;
}

