/*
 * Exercício 8 - Buffer Circular com Rajadas (Bursts), Ociosidade e Backpressure
 *
 * Requisitos:
 * - Estende o exercício 2 para simular rajadas de produção (bursts) e períodos de ociosidade.
 * - Implementa backpressure fazendo produtores aguardarem quando a taxa de consumo cair
 *   (usando marcas d'água High Watermark = 80% e Low Watermark = 35%).
 * - Registra a ocupação do buffer ao longo do tempo para analisar estabilidade.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define BUFFER_CAPACITY 30
#define HIGH_WATERMARK 24  /* 80% da capacidade: ativa backpressure */
#define LOW_WATERMARK  10  /* ~33% da capacidade: libera backpressure */

#define NUM_PRODUCERS 3
#define NUM_CONSUMERS 2

typedef enum {
    PHASE_BURST        = 0, /* Produtores em rajada rápida, consumo normal */
    PHASE_SLOW_CONSUME = 1, /* Taxa de consumo cai drasticamente -> Backpressure atua */
    PHASE_IDLE_PROD    = 2, /* Produtores ociosos -> Buffer drena */
    PHASE_STABLE       = 3, /* Regime equilibrado */
    PHASE_FINISHED     = 4
} sim_phase_t;

typedef struct {
    int buffer[BUFFER_CAPACITY];
    int head;
    int tail;
    int count;

    bool backpressure_active;
    int backpressure_triggers;
    int produced_total;
    int consumed_total;

    sim_phase_t phase;

    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    pthread_cond_t bp_cleared;
} bp_buffer_t;

static bp_buffer_t g_buf;

static const char *phase_name(sim_phase_t p) {
    switch (p) {
        case PHASE_BURST:        return "1. RAJADA (BURST)";
        case PHASE_SLOW_CONSUME: return "2. CONSUMO LENTO ";
        case PHASE_IDLE_PROD:    return "3. PROD. OCIOSO  ";
        case PHASE_STABLE:       return "4. ESTÁVEL       ";
        default:                 return "ENCERRADO        ";
    }
}

static void *producer_worker(void *arg) {
    int id = *(int *)arg;
    unsigned int seed = 5000U + (unsigned int)(id * 97);

    while (true) {
        pthread_mutex_lock(&g_buf.mutex);
        sim_phase_t current_phase = g_buf.phase;
        if (current_phase == PHASE_FINISHED) {
            pthread_mutex_unlock(&g_buf.mutex);
            break;
        }

        /* Backpressure: se ativo, produtores aguardam a ocupação cair até LOW_WATERMARK */
        while (g_buf.backpressure_active && g_buf.phase != PHASE_FINISHED) {
            pthread_cond_wait(&g_buf.bp_cleared, &g_buf.mutex);
        }

        while (g_buf.count == BUFFER_CAPACITY && g_buf.phase != PHASE_FINISHED) {
            pthread_cond_wait(&g_buf.not_full, &g_buf.mutex);
        }

        if (g_buf.phase == PHASE_FINISHED) {
            pthread_mutex_unlock(&g_buf.mutex);
            break;
        }

        /* Insere item no buffer */
        g_buf.buffer[g_buf.tail] = g_buf.produced_total + 1;
        g_buf.tail = (g_buf.tail + 1) % BUFFER_CAPACITY;
        g_buf.count++;
        g_buf.produced_total++;

        /* Verifica se atingiu o High Watermark para acionar o Backpressure */
        if (!g_buf.backpressure_active && g_buf.count >= HIGH_WATERMARK) {
            g_buf.backpressure_active = true;
            g_buf.backpressure_triggers++;
        }

        current_phase = g_buf.phase;
        pthread_cond_signal(&g_buf.not_empty);
        pthread_mutex_unlock(&g_buf.mutex);

        /* Intervalo de produção varia conforme a fase (rajada vs. ociosidade vs. normal) */
        if (current_phase == PHASE_BURST || current_phase == PHASE_SLOW_CONSUME) {
            usleep((useconds_t)(1500 + (rand_r(&seed) % 1500))); /* Rajada: 1.5 - 3.0 ms */
        } else if (current_phase == PHASE_IDLE_PROD) {
            usleep((useconds_t)(30000 + (rand_r(&seed) % 15000))); /* Ociosidade: 30 - 45 ms */
        } else {
            usleep((useconds_t)(6000 + (rand_r(&seed) % 3000))); /* Estável: 6 - 9 ms */
        }
    }
    return NULL;
}

static void *consumer_worker(void *arg) {
    int id = *(int *)arg;
    unsigned int seed = 9000U + (unsigned int)(id * 131);

    while (true) {
        pthread_mutex_lock(&g_buf.mutex);
        while (g_buf.count == 0 && g_buf.phase != PHASE_FINISHED) {
            pthread_cond_wait(&g_buf.not_empty, &g_buf.mutex);
        }

        if (g_buf.count == 0 && g_buf.phase == PHASE_FINISHED) {
            pthread_mutex_unlock(&g_buf.mutex);
            break;
        }

        int item = g_buf.buffer[g_buf.head];
        (void)item;
        g_buf.head = (g_buf.head + 1) % BUFFER_CAPACITY;
        g_buf.count--;
        g_buf.consumed_total++;

        /* Se backpressure estava ativo e o buffer drenou até LOW_WATERMARK, libera produtores */
        if (g_buf.backpressure_active && g_buf.count <= LOW_WATERMARK) {
            g_buf.backpressure_active = false;
            pthread_cond_broadcast(&g_buf.bp_cleared);
        }

        sim_phase_t current_phase = g_buf.phase;
        pthread_cond_signal(&g_buf.not_full);
        pthread_mutex_unlock(&g_buf.mutex);

        /* Taxa de consumo varia conforme a fase */
        if (current_phase == PHASE_SLOW_CONSUME) {
            usleep((useconds_t)(14000 + (rand_r(&seed) % 4000))); /* Consumo lento: 14 - 18 ms */
        } else {
            usleep((useconds_t)(4000 + (rand_r(&seed) % 2000))); /* Consumo normal: 4 - 6 ms */
        }
    }
    return NULL;
}

int main(void) {
    printf("=== EXERCÍCIO 8: RAJADAS (BURSTS), OCIOSIDADE E BACKPRESSURE ===\n");
    printf("Capacidade: %d | High Watermark (ativa BP): %d | Low Watermark (libera BP): %d\n\n",
           BUFFER_CAPACITY, HIGH_WATERMARK, LOW_WATERMARK);

    g_buf.head = 0;
    g_buf.tail = 0;
    g_buf.count = 0;
    g_buf.backpressure_active = false;
    g_buf.backpressure_triggers = 0;
    g_buf.produced_total = 0;
    g_buf.consumed_total = 0;
    g_buf.phase = PHASE_BURST;
    pthread_mutex_init(&g_buf.mutex, NULL);
    pthread_cond_init(&g_buf.not_full, NULL);
    pthread_cond_init(&g_buf.not_empty, NULL);
    pthread_cond_init(&g_buf.bp_cleared, NULL);

    pthread_t prods[NUM_PRODUCERS], cons[NUM_CONSUMERS];
    int p_ids[NUM_PRODUCERS], c_ids[NUM_CONSUMERS];

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        p_ids[i] = i + 1;
        pthread_create(&prods[i], NULL, producer_worker, &p_ids[i]);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        c_ids[i] = i + 1;
        pthread_create(&cons[i], NULL, consumer_worker, &c_ids[i]);
    }

    printf("| Tempo (ms) | Fase Atual        | Ocupação | Backpressure | Gráfico de Ocupação (0..%d)       |\n",
           BUFFER_CAPACITY);
    printf("|------------|-------------------|----------|--------------|-----------------------------------|\n");

    /* Monitora 24 janelas de 40 ms (total ~960 ms divididos nas 4 fases) */
    for (int step = 0; step < 24; step++) {
        usleep(40000);

        pthread_mutex_lock(&g_buf.mutex);
        if (step == 6)  g_buf.phase = PHASE_SLOW_CONSUME;
        if (step == 13) g_buf.phase = PHASE_IDLE_PROD;
        if (step == 18) g_buf.phase = PHASE_STABLE;

        int occ = g_buf.count;
        bool bp = g_buf.backpressure_active;
        sim_phase_t ph = g_buf.phase;
        pthread_mutex_unlock(&g_buf.mutex);

        printf("| %7d ms | %s |  %2d / %2d | %-12s | [",
               (step + 1) * 40, phase_name(ph), occ, BUFFER_CAPACITY,
               bp ? "ATIVO (WAIT)" : "NORMAL");
        for (int b = 0; b < BUFFER_CAPACITY; b++) {
            putchar(b < occ ? '#' : '.');
        }
        printf("] |\n");
    }

    /* Finaliza a simulação de forma limpa */
    pthread_mutex_lock(&g_buf.mutex);
    g_buf.phase = PHASE_FINISHED;
    g_buf.backpressure_active = false;
    pthread_cond_broadcast(&g_buf.bp_cleared);
    pthread_cond_broadcast(&g_buf.not_full);
    pthread_cond_broadcast(&g_buf.not_empty);
    pthread_mutex_unlock(&g_buf.mutex);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(prods[i], NULL);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(cons[i], NULL);
    }

    printf("\n--- Estatísticas de Estabilidade e Backpressure ---\n");
    printf("Itens produzidos         : %d\n", g_buf.produced_total);
    printf("Itens consumidos         : %d\n", g_buf.consumed_total);
    printf("Acionamentos Backpressure: %d vezes (sempre que ocupação >= %d, liberando em <= %d)\n",
           g_buf.backpressure_triggers, HIGH_WATERMARK, LOW_WATERMARK);

    pthread_mutex_destroy(&g_buf.mutex);
    pthread_cond_destroy(&g_buf.not_full);
    pthread_cond_destroy(&g_buf.not_empty);
    pthread_cond_destroy(&g_buf.bp_cleared);
    return 0;
}

