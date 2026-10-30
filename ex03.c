/*
 * Exercício 3 - Transferências Bancárias Concorrentes
 *
 * Requisitos:
 * - Simula M contas e T threads realizando transferências aleatórias entre contas.
 * - Protege os saldos com travas adequadas (um mutex por conta, adquiridos em ordem canônica
 *   para evitar deadlock).
 * - Prova, via asserções (assert), que a soma global do dinheiro permanece constante.
 * - Compara execuções corretas (com trava) e incorretas (sem trava) para evidenciar
 *   condições de corrida.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <sched.h>

#define M_ACCOUNTS 20
#define T_THREADS 8
#define TRANSFERS_PER_THREAD 2500
#define INITIAL_BALANCE 1000L

typedef struct {
    long balance;
    pthread_mutex_t lock;
} account_t;

typedef struct {
    int thread_id;
    unsigned int seed;
    bool use_locks;
    account_t *accounts;
} transfer_arg_t;

static long total_balance(account_t *accounts) {
    long sum = 0;
    for (int i = 0; i < M_ACCOUNTS; i++) {
        sum += accounts[i].balance;
    }
    return sum;
}

static void *transfer_worker(void *arg) {
    transfer_arg_t *t = (transfer_arg_t *)arg;
    account_t *accounts = t->accounts;

    for (int i = 0; i < TRANSFERS_PER_THREAD; i++) {
        int from = rand_r(&t->seed) % M_ACCOUNTS;
        int to = rand_r(&t->seed) % M_ACCOUNTS;
        while (to == from) {
            to = rand_r(&t->seed) % M_ACCOUNTS;
        }
        long amount = (rand_r(&t->seed) % 50) + 1;

        if (t->use_locks) {
            /* Ordem global de travamento por índice de conta para evitar deadlock */
            int first = (from < to) ? from : to;
            int second = (from < to) ? to : from;

            pthread_mutex_lock(&accounts[first].lock);
            pthread_mutex_lock(&accounts[second].lock);

            accounts[from].balance -= amount;
            accounts[to].balance += amount;

            pthread_mutex_unlock(&accounts[second].lock);
            pthread_mutex_unlock(&accounts[first].lock);
        } else {
            /* Execução SEM trava: expõe a janela de condição de corrida (read-modify-write) */
            long b_from = accounts[from].balance;
            long b_to = accounts[to].balance;
            if ((i & 7) == 0) {
                sched_yield(); /* Amplia a probabilidade de intercalação entre threads */
            }
            accounts[from].balance = b_from - amount;
            accounts[to].balance = b_to + amount;
        }
    }
    return NULL;
}

static long run_simulation(bool use_locks, double *elapsed_ms_out) {
    account_t accounts[M_ACCOUNTS];
    for (int i = 0; i < M_ACCOUNTS; i++) {
        accounts[i].balance = INITIAL_BALANCE;
        pthread_mutex_init(&accounts[i].lock, NULL);
    }

    pthread_t threads[T_THREADS];
    transfer_arg_t args[T_THREADS];

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < T_THREADS; i++) {
        args[i].thread_id = i;
        args[i].seed = 98765U + (unsigned int)(i * 7919);
        args[i].use_locks = use_locks;
        args[i].accounts = accounts;
        pthread_create(&threads[i], NULL, transfer_worker, &args[i]);
    }

    for (int i = 0; i < T_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    *elapsed_ms_out = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    long final_sum = total_balance(accounts);

    for (int i = 0; i < M_ACCOUNTS; i++) {
        pthread_mutex_destroy(&accounts[i].lock);
    }
    return final_sum;
}

int main(void) {
    long expected_total = (long)M_ACCOUNTS * INITIAL_BALANCE;
    double time_unsafe = 0.0, time_safe = 0.0;

    printf("=== EXERCÍCIO 3: TRANSFERÊNCIAS BANCÁRIAS (%d CONTAS, %d THREADS) ===\n",
           M_ACCOUNTS, T_THREADS);
    printf("Saldo inicial por conta: R$ %ld | Soma global esperada: R$ %ld\n",
           INITIAL_BALANCE, expected_total);
    printf("Total de transferências por simulação: %d\n\n", T_THREADS * TRANSFERS_PER_THREAD);

    /* 1. Execução incorreta (SEM travas) */
    long unsafe_sum = run_simulation(false, &time_unsafe);
    printf("[1] Execução SEM travas (condição de corrida):\n");
    printf("    Soma global esperada : R$ %ld\n", expected_total);
    printf("    Soma global obtida   : R$ %ld (Diferença / erro: R$ %+ld)\n",
           unsafe_sum, unsafe_sum - expected_total);
    printf("    Tempo de execução    : %.2f ms\n\n", time_unsafe);

    /* 2. Execução correta (COM um mutex por conta + ordem de bloqueio) */
    long safe_sum = run_simulation(true, &time_safe);
    printf("[2] Execução COM travas (1 mutex por conta em ordem canônica):\n");
    printf("    Soma global esperada : R$ %ld\n", expected_total);
    printf("    Soma global obtida   : R$ %ld (Diferença / erro: R$ %+ld)\n",
           safe_sum, safe_sum - expected_total);
    printf("    Tempo de execução    : %.2f ms\n", time_safe);

    /* Prova formal via asserção de que a soma global permanece constante */
    assert(safe_sum == expected_total && "ERRO: A soma global do dinheiro foi alterada!");
    printf("    Verificação assert() : SUCESSO (safe_sum == expected_total comprovado!)\n");

    return 0;
}

