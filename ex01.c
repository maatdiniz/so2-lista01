/*
 * Exercício 1 - Corrida de Cavalos com Threads
 *
 * Requisitos:
 * - Cada cavalo é uma thread que avança em passos aleatórios até cruzar a linha de chegada.
 * - Pede ao usuário uma aposta antes da largada (aceita argumento de linha de comando ou stdin).
 * - Inicia todas as threads em "largada sincronizada" (barreira com mutex + condvar).
 * - Atualiza o placar com exclusão mútua.
 * - Garante que empates sejam resolvidos de forma determinística (sincronização por turno/passo
 *   ou desempate por maior distância no ciclo e menor ID em caso de empate exato) e que não
 *   haja condições de corrida no registro do primeiro colocado.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#define NUM_HORSES 5
#define FINISH_LINE 100
#define MAX_STEP 12

typedef struct {
    int id;
    unsigned int seed;
} horse_arg_t;

/* Estado global protegido por mutex */
static pthread_mutex_t race_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t step_cond = PTHREAD_COND_INITIALIZER;

static int ready_count = 0;
static bool race_started = false;
static bool race_finished = false;

static int positions[NUM_HORSES];
static int current_round = 0;
static int horses_moved_in_round = 0;

static int winner_id = -1;
static int winner_distance = 0;
static int tie_count = 0;

static void print_scoreboard_locked(int round_num) {
    printf("\n--- Placar [Rodada %02d] ---\n", round_num);
    for (int i = 0; i < NUM_HORSES; i++) {
        int pos = positions[i];
        int bars = (pos * 25) / FINISH_LINE;
        if (bars > 25) bars = 25;
        printf("Cavalo #%d: [", i + 1);
        for (int b = 0; b < 25; b++) {
            putchar(b < bars ? '=' : ' ');
        }
        printf("] %3d / %d m\n", pos, FINISH_LINE);
    }
}

static void *horse_thread(void *arg) {
    horse_arg_t *h = (horse_arg_t *)arg;
    int id = h->id;

    /* 1. Largada sincronizada: aguarda todos os cavalos estarem prontos */
    pthread_mutex_lock(&race_mutex);
    ready_count++;
    if (ready_count == NUM_HORSES) {
        race_started = true;
        printf("\n>>> LARGADA AUTORIZADA! Todos os %d cavalos partiram simultaneamente! <<<\n", NUM_HORSES);
        pthread_cond_broadcast(&start_cond);
    } else {
        while (!race_started) {
            pthread_cond_wait(&start_cond, &race_mutex);
        }
    }

    int my_round = 0;
    while (!race_finished) {
        /* Calcula passo aleatório entre 1 e MAX_STEP */
        int step = (rand_r(&h->seed) % MAX_STEP) + 1;

        /* Atualiza o placar com exclusão mútua */
        positions[id] += step;
        horses_moved_in_round++;

        if (horses_moved_in_round == NUM_HORSES) {
            current_round++;
            print_scoreboard_locked(current_round);

            /* Verifica se um ou mais cavalos cruzaram a linha de chegada nesta rodada.
             * Resolução determinística de empate:
             * 1º critério: maior distância alcançada ao cruzar a linha na rodada.
             * 2º critério (empate exato de distância na mesma rodada): menor ID do cavalo.
             */
            int best_horse = -1;
            int best_dist = -1;
            int crossed = 0;

            for (int i = 0; i < NUM_HORSES; i++) {
                if (positions[i] >= FINISH_LINE) {
                    crossed++;
                    if (positions[i] > best_dist) {
                        best_dist = positions[i];
                        best_horse = i;
                    }
                    /* Como iteramos i de 0 a NUM_HORSES-1, se positions[i] == best_dist,
                     * mantemos o menor ID de forma 100% determinística. */
                }
            }

            if (crossed > 0) {
                race_finished = true;
                winner_id = best_horse;
                winner_distance = best_dist;
                tie_count = crossed;
            }

            horses_moved_in_round = 0;
            pthread_cond_broadcast(&step_cond);
        } else {
            my_round = current_round;
            while (current_round == my_round && !race_finished) {
                pthread_cond_wait(&step_cond, &race_mutex);
            }
        }

        pthread_mutex_unlock(&race_mutex);
        usleep(15000); /* Pequena pausa entre passadas */
        pthread_mutex_lock(&race_mutex);
    }

    pthread_mutex_unlock(&race_mutex);
    return NULL;
}

int main(int argc, char *argv[]) {
    int bet = 0;

    printf("=== EXERCÍCIO 1: CORRIDA DE CAVALOS COM THREADS ===\n");
    printf("Cavalos participantes: 1 a %d | Linha de chegada: %d metros\n", NUM_HORSES, FINISH_LINE);

    if (argc > 1) {
        bet = atoi(argv[1]);
        printf("Aposta recebida via argumento: Cavalo #%d\n", bet);
    } else {
        printf("Em qual cavalo você deseja apostar (1 a %d)? ", NUM_HORSES);
        fflush(stdout);
        if (scanf("%d", &bet) != 1) {
            bet = 1;
        }
    }

    if (bet < 1 || bet > NUM_HORSES) {
        printf("Aposta inválida (%d). Ajustando automaticamente para o Cavalo #1.\n", bet);
        bet = 1;
    }

    pthread_t threads[NUM_HORSES];
    horse_arg_t args[NUM_HORSES];
    unsigned int base_seed = (unsigned int)time(NULL);

    for (int i = 0; i < NUM_HORSES; i++) {
        positions[i] = 0;
        args[i].id = i;
        args[i].seed = base_seed ^ (unsigned int)((i + 1) * 2654435761U);
        if (pthread_create(&threads[i], NULL, horse_thread, &args[i]) != 0) {
            perror("pthread_create");
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < NUM_HORSES; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n===================================================\n");
    if (tie_count > 1) {
        printf("Houve %d cavalos cruzando a linha na rodada final (desempate determinístico aplicado).\n", tie_count);
    }
    printf("VENCEDOR OFICIAL: Cavalo #%d (distância final: %d m)\n", winner_id + 1, winner_distance);
    printf("SUA APOSTA      : Cavalo #%d -> %s\n",
           bet, (bet == winner_id + 1) ? "PARABÉNS! VOCÊ ACERTOU!" : "NÃO FOI DESSA VEZ!");
    printf("===================================================\n");

    return 0;
}

