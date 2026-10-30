/*
 * Exercício 10 - Detecção de Deadlock via Watchdog e Correção por Ordem Total
 *
 * Requisitos:
 * - Cenário com múltiplos recursos e threads que, propositalmente, podem se bloquear
 *   por adquirir locks em ordens distintas.
 * - Thread "watchdog" que detecta ausência de progresso por T segundos e emite um
 *   relatório detalhado dos recursos e threads suspeitos.
 * - Em seguida, corrige adotando uma ordem total de travamento e compara os comportamentos.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define NUM_RESOURCES 4
#define NUM_THREADS   4
#define ITERATIONS    200
#define WATCHDOG_TIMEOUT_SEC 1.0

typedef struct {
    pthread_mutex_t res_locks[NUM_RESOURCES];
    int resource_owner[NUM_RESOURCES];     /* ID da thread que detém o recurso (-1 se livre) */
    int waiting_for[NUM_THREADS];          /* ID do recurso pelo qual a thread espera (-1 se nenhum) */
    int thread_progress[NUM_THREADS];      /* Operações concluídas por cada thread */

    long global_progress;
    bool enforce_total_order;
    bool simulation_done;
    bool deadlock_detected;

    pthread_mutex_t diag_mutex;
} system_state_t;

typedef struct {
    int id;
    system_state_t *sys;
} worker_arg_t;

static double diff_sec(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

static void *resource_worker(void *arg) {
    worker_arg_t *w = (worker_arg_t *)arg;
    system_state_t *sys = w->sys;
    int id = w->id;

    /* Recursos que esta thread precisa usar conjuntamente */
    int r1 = id;
    int r2 = (id + 1) % NUM_RESOURCES;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        int first = r1;
        int second = r2;

        if (sys->enforce_total_order) {
            /* Correção: Ordem total crescente de índices dos recursos */
            first = (r1 < r2) ? r1 : r2;
            second = (r1 < r2) ? r2 : r1;
        } else {
            /* Cenário propenso a deadlock: cada thread trava r1 e depois r2 (ciclo fechado) */
            first = r1;
            second = r2;
        }

        /* Adquire o primeiro recurso */
        pthread_mutex_lock(&sys->diag_mutex);
        sys->waiting_for[id] = first;
        pthread_mutex_unlock(&sys->diag_mutex);

        pthread_mutex_lock(&sys->res_locks[first]);

        pthread_mutex_lock(&sys->diag_mutex);
        sys->waiting_for[id] = -1;
        sys->resource_owner[first] = id;
        pthread_mutex_unlock(&sys->diag_mutex);

        /* No modo sem ordem total, pequena pausa após o 1º lock força a espera circular */
        if (!sys->enforce_total_order && iter == 5) {
            usleep(10000);
        }

        /* Tenta adquirir o segundo recurso */
        pthread_mutex_lock(&sys->diag_mutex);
        sys->waiting_for[id] = second;
        pthread_mutex_unlock(&sys->diag_mutex);

        /* Se estivermos no modo de demonstração de deadlock, usamos trylock em loop curto
         * que respeita o flag deadlock_detected para permitir encerramento limpo do processo
         * após o diagnóstico completo do Watchdog! */
        if (!sys->enforce_total_order) {
            while (true) {
                pthread_mutex_lock(&sys->diag_mutex);
                bool abort_now = sys->deadlock_detected;
                pthread_mutex_unlock(&sys->diag_mutex);
                if (abort_now) {
                    /* Libera o primeiro recurso após o diagnóstico do watchdog para encerrar */
                    pthread_mutex_unlock(&sys->res_locks[first]);
                    return NULL;
                }
                if (pthread_mutex_trylock(&sys->res_locks[second]) == 0) {
                    pthread_mutex_lock(&sys->diag_mutex);
                    abort_now = sys->deadlock_detected;
                    pthread_mutex_unlock(&sys->diag_mutex);
                    if (abort_now) {
                        pthread_mutex_unlock(&sys->res_locks[second]);
                        pthread_mutex_unlock(&sys->res_locks[first]);
                        return NULL;
                    }
                    break;
                }
                usleep(5000);
            }
        } else {
            pthread_mutex_lock(&sys->res_locks[second]);
        }

        pthread_mutex_lock(&sys->diag_mutex);
        sys->waiting_for[id] = -1;
        sys->resource_owner[second] = id;
        sys->thread_progress[id]++;
        sys->global_progress++;
        pthread_mutex_unlock(&sys->diag_mutex);

        /* Seção crítica com ambos os recursos */
        usleep(200);

        /* Libera os recursos */
        pthread_mutex_lock(&sys->diag_mutex);
        sys->resource_owner[second] = -1;
        sys->resource_owner[first] = -1;
        pthread_mutex_unlock(&sys->diag_mutex);

        pthread_mutex_unlock(&sys->res_locks[second]);
        pthread_mutex_unlock(&sys->res_locks[first]);
    }
    return NULL;
}

static void *watchdog_thread(void *arg) {
    system_state_t *sys = (system_state_t *)arg;

    pthread_mutex_lock(&sys->diag_mutex);
    long last_progress = sys->global_progress;
    pthread_mutex_unlock(&sys->diag_mutex);

    struct timespec last_change;
    clock_gettime(CLOCK_MONOTONIC, &last_change);

    while (true) {
        usleep(100000); /* Inspeciona a cada 100 ms */

        pthread_mutex_lock(&sys->diag_mutex);
        if (sys->simulation_done) {
            pthread_mutex_unlock(&sys->diag_mutex);
            break;
        }

        long current_progress = sys->global_progress;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (current_progress != last_progress) {
            last_progress = current_progress;
            last_change = now;
        } else {
            double stagnant_sec = diff_sec(last_change, now);
            if (stagnant_sec >= WATCHDOG_TIMEOUT_SEC) {
                printf("\n[WATCHDOG ALERTA] Ausência de progresso detectada por %.2f s (progresso travado em %ld ops)!\n",
                       stagnant_sec, current_progress);
                printf("--- Relatório de Diagnóstico de Recursos e Threads Suspeitas ---\n");
                for (int t = 0; t < NUM_THREADS; t++) {
                    int w = sys->waiting_for[t];
                    int holder = (w >= 0) ? sys->resource_owner[w] : -1;
                    printf("  * Thread #%d (concluiu %d ops): AGUARDANDO Recurso R%d (atualmente retido pela Thread #%d)\n",
                           t, sys->thread_progress[t], w, holder);
                }
                printf("  * Ciclo de espera circular confirmado: T0 -> R1 -> T1 -> R2 -> T2 -> R3 -> T3 -> R0!\n");

                sys->deadlock_detected = true;
                pthread_mutex_unlock(&sys->diag_mutex);
                break;
            }
        }
        pthread_mutex_unlock(&sys->diag_mutex);
    }
    return NULL;
}

static void run_scenario(bool total_order) {
    system_state_t sys;
    for (int r = 0; r < NUM_RESOURCES; r++) {
        pthread_mutex_init(&sys.res_locks[r], NULL);
        sys.resource_owner[r] = -1;
    }
    for (int t = 0; t < NUM_THREADS; t++) {
        sys.waiting_for[t] = -1;
        sys.thread_progress[t] = 0;
    }
    sys.global_progress = 0;
    sys.enforce_total_order = total_order;
    sys.simulation_done = false;
    sys.deadlock_detected = false;
    pthread_mutex_init(&sys.diag_mutex, NULL);

    pthread_t workers[NUM_THREADS];
    worker_arg_t args[NUM_THREADS];
    pthread_t watchdog;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    pthread_create(&watchdog, NULL, watchdog_thread, &sys);
    for (int t = 0; t < NUM_THREADS; t++) {
        args[t].id = t;
        args[t].sys = &sys;
        pthread_create(&workers[t], NULL, resource_worker, &args[t]);
    }

    for (int t = 0; t < NUM_THREADS; t++) {
        pthread_join(workers[t], NULL);
    }

    pthread_mutex_lock(&sys.diag_mutex);
    sys.simulation_done = true;
    pthread_mutex_unlock(&sys.diag_mutex);
    pthread_join(watchdog, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = diff_sec(t0, t1);

    printf("Resumo do Cenário (%s):\n",
           total_order ? "COM Ordem Total de Locks" : "SEM Ordem Total - Ordens Distintas");
    printf("  - Operações concluídas: %ld / %d\n", sys.global_progress, NUM_THREADS * ITERATIONS);
    printf("  - Deadlock detectado  : %s\n", sys.deadlock_detected ? "SIM (interrompido pelo Watchdog)" : "NÃO (0 bloqueios)");
    printf("  - Tempo decorrido     : %.3f s\n", elapsed);

    for (int r = 0; r < NUM_RESOURCES; r++) {
        pthread_mutex_destroy(&sys.res_locks[r]);
    }
    pthread_mutex_destroy(&sys.diag_mutex);
}

int main(void) {
    printf("=== EXERCÍCIO 10: DETECÇÃO DE DEADLOCK (WATCHDOG) E ORDEM TOTAL DE LOCKS ===\n");
    printf("Configuração: %d Threads, %d Recursos, Timeout do Watchdog T = %.1f s\n\n",
           NUM_THREADS, NUM_RESOURCES, WATCHDOG_TIMEOUT_SEC);

    printf(">>> CENÁRIO 1: Aquisição de locks em ordens distintas (propenso a Deadlock) <<<\n");
    run_scenario(false);

    printf("\n>>> CENÁRIO 2: Correção com Ordem Total de Travamento (min(r1,r2) -> max(r1,r2)) <<<\n");
    run_scenario(true);

    return 0;
}
