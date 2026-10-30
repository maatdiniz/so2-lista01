/*
 * Exercício 7 - Jantar dos Filósofos (Ordem Global e Semáforo + Mitigação de Starvation)
 *
 * Requisitos:
 * - Garfos representados por pthread_mutex_t.
 * - Duas soluções implementadas e avaliadas:
 *   (a) Ordem global de aquisição de garfos para evitar deadlock.
 *   (b) Semáforo limitando a 4 filósofos simultâneos na disputa por garfos.
 * - Coleta métricas por filósofo (número de refeições, tempo médio e maior espera).
 * - Ajusta a lógica com controle de justiça (fairness throttle) para mitigar starvation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#define NUM_PHILOSOPHERS 5
#define TARGET_MEALS_PER_PHIL 40
#define MAX_MEAL_LEAD 2 /* Limite de vantagem para evitar starvation de vizinhos */

/* Semáforo contador portável (compatível com macOS e Linux sem avisos de depreciação) */
typedef struct {
    int value;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} counting_sem_t;

static void csem_init(counting_sem_t *s, int initial_value) {
    s->value = initial_value;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
}

static void csem_wait(counting_sem_t *s) {
    pthread_mutex_lock(&s->mutex);
    while (s->value == 0) {
        pthread_cond_wait(&s->cond, &s->mutex);
    }
    s->value--;
    pthread_mutex_unlock(&s->mutex);
}

static void csem_post(counting_sem_t *s) {
    pthread_mutex_lock(&s->mutex);
    s->value++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}

static void csem_destroy(counting_sem_t *s) {
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
}

typedef enum {
    SOL_GLOBAL_ORDER = 0,
    SOL_SEMAPHORE_4  = 1
} solution_mode_t;

typedef struct {
    pthread_mutex_t forks[NUM_PHILOSOPHERS];
    counting_sem_t room_sem;

    /* Monitor de justiça (anti-starvation) */
    pthread_mutex_t fairness_mutex;
    pthread_cond_t fairness_cond;
    int meals[NUM_PHILOSOPHERS];
    double total_wait_ms[NUM_PHILOSOPHERS];
    double max_wait_ms[NUM_PHILOSOPHERS];

    solution_mode_t mode;
} table_state_t;

typedef struct {
    int id;
    unsigned int seed;
    table_state_t *table;
} phil_arg_t;

static double diff_ms(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

/* Verifica se o filósofo 'id' está muito à frente do filósofo menos atendido */
static void wait_fair_turn(table_state_t *t, int id) {
    pthread_mutex_lock(&t->fairness_mutex);
    while (true) {
        int min_meals = t->meals[0];
        for (int i = 1; i < NUM_PHILOSOPHERS; i++) {
            if (t->meals[i] < min_meals) {
                min_meals = t->meals[i];
            }
        }
        if (t->meals[id] - min_meals < MAX_MEAL_LEAD) {
            break;
        }
        pthread_cond_wait(&t->fairness_cond, &t->fairness_mutex);
    }
    pthread_mutex_unlock(&t->fairness_mutex);
}

static void record_meal_and_notify(table_state_t *t, int id, double wait_ms) {
    pthread_mutex_lock(&t->fairness_mutex);
    t->meals[id]++;
    t->total_wait_ms[id] += wait_ms;
    if (wait_ms > t->max_wait_ms[id]) {
        t->max_wait_ms[id] = wait_ms;
    }
    pthread_cond_broadcast(&t->fairness_cond);
    pthread_mutex_unlock(&t->fairness_mutex);
}

static void *philosopher_thread(void *arg) {
    phil_arg_t *p = (phil_arg_t *)arg;
    table_state_t *t = p->table;
    int id = p->id;
    int left = id;
    int right = (id + 1) % NUM_PHILOSOPHERS;

    for (int m = 0; m < TARGET_MEALS_PER_PHIL; m++) {
        /* 1. Pensando */
        usleep((useconds_t)(200 + (rand_r(&p->seed) % 400)));

        /* 2. Com fome: mede o tempo de espera até conseguir os dois garfos */
        struct timespec w_start, w_end;
        clock_gettime(CLOCK_MONOTONIC, &w_start);

        /* Mitigação de starvation: cede vez se estiver muito à frente dos demais */
        wait_fair_turn(t, id);

        if (t->mode == SOL_GLOBAL_ORDER) {
            /* Solução (a): Ordem global de aquisição (menor índice primeiro) */
            int first = (left < right) ? left : right;
            int second = (left < right) ? right : left;

            pthread_mutex_lock(&t->forks[first]);
            pthread_mutex_lock(&t->forks[second]);

            clock_gettime(CLOCK_MONOTONIC, &w_end);
            record_meal_and_notify(t, id, diff_ms(w_start, w_end));

            /* 3. Comendo */
            usleep((useconds_t)(250 + (rand_r(&p->seed) % 350)));

            pthread_mutex_unlock(&t->forks[second]);
            pthread_mutex_unlock(&t->forks[first]);
        } else {
            /* Solução (b): Semáforo limitando a 4 filósofos simultâneos na mesa */
            csem_wait(&t->room_sem);
            pthread_mutex_lock(&t->forks[left]);
            pthread_mutex_lock(&t->forks[right]);

            clock_gettime(CLOCK_MONOTONIC, &w_end);
            record_meal_and_notify(t, id, diff_ms(w_start, w_end));

            /* 3. Comendo */
            usleep((useconds_t)(250 + (rand_r(&p->seed) % 350)));

            pthread_mutex_unlock(&t->forks[right]);
            pthread_mutex_unlock(&t->forks[left]);
            csem_post(&t->room_sem);
        }
    }
    return NULL;
}

static void run_philosophers_experiment(solution_mode_t mode, const char *title) {
    table_state_t table;
    table.mode = mode;
    csem_init(&table.room_sem, NUM_PHILOSOPHERS - 1); /* Limite = 4 filósofos */
    pthread_mutex_init(&table.fairness_mutex, NULL);
    pthread_cond_init(&table.fairness_cond, NULL);

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_mutex_init(&table.forks[i], NULL);
        table.meals[i] = 0;
        table.total_wait_ms[i] = 0.0;
        table.max_wait_ms[i] = 0.0;
    }

    pthread_t threads[NUM_PHILOSOPHERS];
    phil_arg_t args[NUM_PHILOSOPHERS];

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        args[i].id = i;
        args[i].seed = 7777U + (unsigned int)(i * 313 + (int)mode * 1000);
        args[i].table = &table;
        pthread_create(&threads[i], NULL, philosopher_thread, &args[i]);
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    printf("\n--- %s (Tempo total: %.2f ms) ---\n", title, diff_ms(t0, t1));
    printf("| Filósofo | Refeições | Espera Média (ms) | Maior Espera (ms) |\n");
    printf("|----------|-----------|-------------------|-------------------|\n");
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        double avg_wait = table.total_wait_ms[i] / table.meals[i];
        printf("|   #%d     |    %3d    |     %9.3f     |     %9.3f     |\n",
               i + 1, table.meals[i], avg_wait, table.max_wait_ms[i]);
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_mutex_destroy(&table.forks[i]);
    }
    csem_destroy(&table.room_sem);
    pthread_mutex_destroy(&table.fairness_mutex);
    pthread_cond_destroy(&table.fairness_cond);
}

int main(void) {
    printf("=== EXERCÍCIO 7: JANTAR DOS FILÓSOFOS (%d FILÓSOFOS) ===\n", NUM_PHILOSOPHERS);
    printf("Meta por filósofo: %d refeições | Controle anti-starvation ativo (diferença máx = %d)\n",
           TARGET_MEALS_PER_PHIL, MAX_MEAL_LEAD);

    run_philosophers_experiment(SOL_GLOBAL_ORDER,
                                "Solução 7(a): Ordem Global de Aquisição de Garfos");
    run_philosophers_experiment(SOL_SEMAPHORE_4,
                                "Solução 7(b): Semáforo Limitando a 4 Filósofos Simultâneos");

    return 0;
}

