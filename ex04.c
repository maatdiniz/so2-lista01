/*
 * Exercício 4 - Linha de Processamento (Pipeline de 3 Estágios)
 *
 * Requisitos:
 * - Três threads em pipeline: Captura -> Processamento -> Gravação.
 * - Conectadas por duas filas limitadas protegidas por mutex + variáveis de condição.
 * - Processa N itens e finaliza com protocolo de encerramento limpo (poison pill).
 * - Demonstra ausência de deadlock e perda de itens (contagem e checksum verificados).
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>

#define QUEUE_CAPACITY 8
#define NUM_ITEMS 100
#define POISON_PILL_ID -1

typedef struct {
    int id;
    long raw_value;
    long processed_value;
} data_item_t;

typedef struct {
    data_item_t buffer[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} bounded_queue_t;

static bounded_queue_t queue_cap_to_proc;
static bounded_queue_t queue_proc_to_write;

/* Métricas de verificação de integridade (ausência de perda de itens) */
static int captured_count = 0;
static int processed_count = 0;
static int written_count = 0;
static long expected_checksum = 0;
static long actual_checksum = 0;

static void queue_init(bounded_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

static void queue_destroy(bounded_queue_t *q) {
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
}

static void queue_push(bounded_queue_t *q, data_item_t item) {
    pthread_mutex_lock(&q->mutex);
    while (q->count == QUEUE_CAPACITY) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    q->buffer[q->tail] = item;
    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

static data_item_t queue_pop(bounded_queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    data_item_t item = q->buffer[q->head];
    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return item;
}

/* Estágio 1: Thread de Captura */
static void *capture_stage(void *arg) {
    int total_items = *(int *)arg;

    for (int i = 1; i <= total_items; i++) {
        data_item_t item;
        item.id = i;
        item.raw_value = (long)i * 10L;
        item.processed_value = 0;

        captured_count++;
        expected_checksum += (item.raw_value * 2L + 7L);

        queue_push(&queue_cap_to_proc, item);
        usleep(400);
    }

    /* Envia Poison Pill para encerrar o estágio de Processamento */
    data_item_t pill = { .id = POISON_PILL_ID, .raw_value = 0, .processed_value = 0 };
    queue_push(&queue_cap_to_proc, pill);
    printf("[Captura]       Finalizou envio de %d itens + Poison Pill.\n", captured_count);
    return NULL;
}

/* Estágio 2: Thread de Processamento */
static void *processing_stage(void *arg) {
    (void)arg;
    while (true) {
        data_item_t item = queue_pop(&queue_cap_to_proc);

        if (item.id == POISON_PILL_ID) {
            /* Repassa a Poison Pill para encerrar o estágio de Gravação */
            queue_push(&queue_proc_to_write, item);
            printf("[Processamento] Recebeu Poison Pill, repassou ao estágio de Gravação e encerrou (%d itens processados).\n",
                   processed_count);
            break;
        }

        /* Transformação do item */
        item.processed_value = item.raw_value * 2L + 7L;
        processed_count++;
        usleep(600);

        queue_push(&queue_proc_to_write, item);
    }
    return NULL;
}

/* Estágio 3: Thread de Gravação */
static void *writing_stage(void *arg) {
    const char *filename = (const char *)arg;
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen");
        return NULL;
    }

    fprintf(fp, "id,raw_value,processed_value\n");

    while (true) {
        data_item_t item = queue_pop(&queue_proc_to_write);

        if (item.id == POISON_PILL_ID) {
            printf("[Gravação]      Recebeu Poison Pill, fechou '%s' e encerrou (%d itens gravados).\n",
                   filename, written_count);
            break;
        }

        fprintf(fp, "%d,%ld,%ld\n", item.id, item.raw_value, item.processed_value);
        written_count++;
        actual_checksum += item.processed_value;
        usleep(500);
    }

    fclose(fp);
    return NULL;
}

int main(int argc, char *argv[]) {
    int n_items = NUM_ITEMS;
    if (argc > 1) {
        n_items = atoi(argv[1]);
        if (n_items <= 0) n_items = NUM_ITEMS;
    }

    const char *output_file = "pipeline_output.csv";

    printf("=== EXERCÍCIO 4: PIPELINE DE 3 ESTÁGIOS COM POISON PILL ===\n");
    printf("Itens a processar: %d | Capacidade de cada fila limitada: %d\n\n",
           n_items, QUEUE_CAPACITY);

    queue_init(&queue_cap_to_proc);
    queue_init(&queue_proc_to_write);

    pthread_t t_cap, t_proc, t_write;
    pthread_create(&t_cap, NULL, capture_stage, &n_items);
    pthread_create(&t_proc, NULL, processing_stage, NULL);
    pthread_create(&t_write, NULL, writing_stage, (void *)output_file);

    pthread_join(t_cap, NULL);
    pthread_join(t_proc, NULL);
    pthread_join(t_write, NULL);

    printf("\n--- Verificação de Integridade e Encerramento Limpo ---\n");
    printf("Itens capturados  : %d\n", captured_count);
    printf("Itens processados : %d\n", processed_count);
    printf("Itens gravados    : %d\n", written_count);
    printf("Checksum esperado : %ld\n", expected_checksum);
    printf("Checksum gravado  : %ld\n", actual_checksum);

    assert(captured_count == n_items);
    assert(processed_count == n_items);
    assert(written_count == n_items);
    assert(queue_cap_to_proc.count == 0);
    assert(queue_proc_to_write.count == 0);
    assert(expected_checksum == actual_checksum);

    printf("Resultado         : SUCESSO! Nenhum item perdido e encerramento sem deadlock.\n");

    queue_destroy(&queue_cap_to_proc);
    queue_destroy(&queue_proc_to_write);
    return 0;
}

