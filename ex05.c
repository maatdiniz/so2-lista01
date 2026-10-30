/*
 * Exercício 5 - Pool Fixo de N Threads para Tarefas CPU-bound
 *
 * Requisitos:
 * - Pool fixo de N threads que processa uma fila concorrente de tarefas CPU-bound
 *   (teste de primalidade e cálculo de Fibonacci iterativo).
 * - Permite enfileirar tarefas a partir da entrada padrão (stdin), processa até EOF
 *   e finaliza o pool com sinalização apropriada.
 * - Prova que a fila é thread-safe e que nenhuma tarefa se perde.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

#define DEFAULT_POOL_SIZE 4

typedef struct task_node {
    int task_id;
    uint64_t value;
    struct task_node *next;
} task_node_t;

typedef struct {
    task_node_t *head;
    task_node_t *tail;
    int size;
    bool shutdown;

    int enqueued_count;
    int completed_count;
    int primes_found;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
} thread_pool_t;

typedef struct {
    int worker_id;
    thread_pool_t *pool;
    int tasks_handled;
} worker_ctx_t;

static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Tarefa CPU-bound 1: Teste de primalidade determinístico O(sqrt(n)) */
static bool is_prime(uint64_t n) {
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    if (n % 3 == 0) return n == 3;
    for (uint64_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) {
            return false;
        }
    }
    return true;
}

/* Tarefa CPU-bound 2: Fibonacci iterativo (mod 2^64) */
static uint64_t fibonacci_iter(uint64_t n) {
    if (n == 0) return 0;
    if (n == 1) return 1;
    uint64_t a = 0, b = 1;
    for (uint64_t i = 2; i <= n; i++) {
        uint64_t c = a + b;
        a = b;
        b = c;
    }
    return b;
}

static void pool_init(thread_pool_t *pool) {
    pool->head = NULL;
    pool->tail = NULL;
    pool->size = 0;
    pool->shutdown = false;
    pool->enqueued_count = 0;
    pool->completed_count = 0;
    pool->primes_found = 0;
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
}

static void pool_enqueue(thread_pool_t *pool, int task_id, uint64_t value) {
    task_node_t *node = (task_node_t *)malloc(sizeof(task_node_t));
    if (!node) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    node->task_id = task_id;
    node->value = value;
    node->next = NULL;

    pthread_mutex_lock(&pool->mutex);
    if (pool->tail) {
        pool->tail->next = node;
    } else {
        pool->head = node;
    }
    pool->tail = node;
    pool->size++;
    pool->enqueued_count++;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

static void pool_shutdown(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

static void pool_destroy(thread_pool_t *pool) {
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
}

static void *worker_routine(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    thread_pool_t *pool = ctx->pool;
    ctx->tasks_handled = 0;

    while (true) {
        pthread_mutex_lock(&pool->mutex);
        while (pool->size == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        if (pool->size == 0 && pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* Retira o próximo nó da fila concorrente */
        task_node_t *task = pool->head;
        pool->head = task->next;
        if (pool->head == NULL) {
            pool->tail = NULL;
        }
        pool->size--;
        pthread_mutex_unlock(&pool->mutex);

        /* Executa processamento CPU-bound fora da seção crítica da fila */
        bool prime = is_prime(task->value);
        uint64_t fib_n = (task->value % 90) + 1;
        uint64_t fib_val = fibonacci_iter(fib_n);

        pthread_mutex_lock(&print_mutex);
        printf("[Worker #%d] Tarefa #%02d | n = %12llu | Primo: %-3s | Fib(%2llu) = %llu\n",
               ctx->worker_id, task->task_id,
               (unsigned long long)task->value,
               prime ? "SIM" : "NAO",
               (unsigned long long)fib_n,
               (unsigned long long)fib_val);
        pthread_mutex_unlock(&print_mutex);

        pthread_mutex_lock(&pool->mutex);
        pool->completed_count++;
        if (prime) {
            pool->primes_found++;
        }
        pthread_mutex_unlock(&pool->mutex);

        ctx->tasks_handled++;
        free(task);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int n_threads = DEFAULT_POOL_SIZE;
    if (argc > 1) {
        n_threads = atoi(argv[1]);
        if (n_threads <= 0) n_threads = DEFAULT_POOL_SIZE;
    }

    printf("=== EXERCÍCIO 5: THREAD POOL (%d THREADS) PARA TAREFAS CPU-BOUND ===\n", n_threads);
    printf("Lendo inteiros da entrada padrão (stdin) até EOF...\n\n");

    thread_pool_t pool;
    pool_init(&pool);

    pthread_t *threads = (pthread_t *)calloc((size_t)n_threads, sizeof(pthread_t));
    worker_ctx_t *ctxs = (worker_ctx_t *)calloc((size_t)n_threads, sizeof(worker_ctx_t));

    for (int i = 0; i < n_threads; i++) {
        ctxs[i].worker_id = i + 1;
        ctxs[i].pool = &pool;
        pthread_create(&threads[i], NULL, worker_routine, &ctxs[i]);
    }

    uint64_t val;
    int task_id = 0;
    while (scanf("%llu", (unsigned long long *)&val) == 1) {
        task_id++;
        pool_enqueue(&pool, task_id, val);
    }

    /* Se nenhuma entrada foi fornecida (ex.: EOF imediato), insere lote padrão de demonstração */
    if (task_id == 0) {
        uint64_t demo_values[] = {
            104729ULL, 104723ULL, 9999991ULL, 15485863ULL,
            32452843ULL, 49979687ULL, 67867967ULL, 86028121ULL,
            100000007ULL, 100000009ULL, 2147483647ULL, 4294967291ULL
        };
        int n_demo = (int)(sizeof(demo_values) / sizeof(demo_values[0]));
        printf("(EOF imediato detectado; enfileirando %d tarefas CPU-bound de demonstração)\n\n", n_demo);
        for (int i = 0; i < n_demo; i++) {
            task_id++;
            pool_enqueue(&pool, task_id, demo_values[i]);
        }
    }

    /* Sinaliza encerramento do pool após EOF */
    pool_shutdown(&pool);

    int sum_worker_tasks = 0;
    for (int i = 0; i < n_threads; i++) {
        pthread_join(threads[i], NULL);
        sum_worker_tasks += ctxs[i].tasks_handled;
    }

    printf("\n--- Resumo e Verificação de Thread-Safety ---\n");
    for (int i = 0; i < n_threads; i++) {
        printf("Worker #%d processou %d tarefas\n", ctxs[i].worker_id, ctxs[i].tasks_handled);
    }
    printf("Total enfileirado : %d\n", pool.enqueued_count);
    printf("Total concluído   : %d (soma dos workers: %d)\n", pool.completed_count, sum_worker_tasks);
    printf("Primos encontrados: %d\n", pool.primes_found);

    assert(pool.size == 0);
    assert(pool.enqueued_count == task_id);
    assert(pool.completed_count == pool.enqueued_count);
    assert(sum_worker_tasks == pool.enqueued_count);
    printf("Verificação assert(): SUCESSO! Fila thread-safe e zero perda de tarefas.\n");

    pool_destroy(&pool);
    free(threads);
    free(ctxs);
    return 0;
}

