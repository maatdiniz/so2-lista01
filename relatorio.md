# Laboratório 01 — Exercícios sobre Sistemas Operacionais 2

**Aluno:** Matheus Diniz Amorim  
**Disciplina:** Sistemas Operacionais 2 (2026-2)  
**Linguagem utilizada:** C (Padrão POSIX Threads — `pthread`)  

---

## Como Compilar e Executar

O repositório conta com um `Makefile` automatizado para compilar e executar todos os 10 exercícios com flags estritas de verificação (`-Wall -Wextra -O2 -pthread`).

```bash
# Compilar todos os exercícios (gera os binários ex01 a ex10)
make all

# Executar todos os 10 experimentos em sequência
make run

# Limpar binários e arquivos temporários gerados nos testes
make clean
```

Também é possível executar qualquer exercício individualmente:

| Exercício | Arquivo Fonte | Comando de Execução |
| :--- | :--- | :--- |
| **Exercício 1** | `ex01.c` | `./ex01 3` *(ou `./ex01` para digitar a aposta interativamente)* |
| **Exercício 2** | `ex02.c` | `./ex02` |
| **Exercício 3** | `ex03.c` | `./ex03` |
| **Exercício 4** | `ex04.c` | `./ex04` |
| **Exercício 5** | `ex05.c` | `printf "104729\n1299709\n15485863\n32452843\n" \| ./ex05 4` |
| **Exercício 6** | `ex06.c` | `./ex06` |
| **Exercício 7** | `ex07.c` | `./ex07` |
| **Exercício 8** | `ex08.c` | `./ex08` |
| **Exercício 9** | `ex09.c` | `./ex09` |
| **Exercício 10** | `ex10.c` | `./ex10` |

---

## Exercício 1 — Corrida de Cavalos com Largada Sincronizada

### Explicação da Implementação (`ex01.c`)
- **Modelo de Threads:** Cada um dos `NUM_HORSES = 5` cavalos é representado por uma thread (`horse_thread`) que avança em passos pseudoaleatórios (`rand_r` thread-safe com semente individual por thread) entre `1` e `12` metros até alcançar ou ultrapassar `FINISH_LINE = 100`.
- **Largada Sincronizada:** Antes do loop da corrida, cada thread incrementa `ready_count` sob proteção de `race_mutex`. Enquanto `ready_count < NUM_HORSES`, as threads aguardam sem espera ativa em `pthread_cond_wait(&start_cond, &race_mutex)`. A última thread a chegar define `race_started = true` e dispara `pthread_cond_broadcast(&start_cond)`, garantindo que todos os cavalos iniciem simultaneamente.
- **Exclusão Mútua no Placar e Desempate Determinístico:** A atualização do vetor `positions[id]` e a impressão do placar ocorrem sob posse de `race_mutex`. Ao final de cada rodada sincronizada (`step_cond`), verifica-se se um ou mais cavalos cruzaram a linha de chegada (`positions[i] >= FINISH_LINE`). Caso mais de um cavalo cruze a linha na mesma rodada, aplica-se uma regra determinística livre de condição de corrida:
  1. Vence o cavalo que alcançou a **maior distância** na rodada final;
  2. Persistindo empate exato em distância, vence o cavalo de **menor identificador (`ID`)**.

### Exemplo de Saída Experimental
```text
=== EXERCÍCIO 1: CORRIDA DE CAVALOS COM THREADS ===
Cavalos participantes: 1 a 5 | Linha de chegada: 100 metros
Aposta recebida via argumento: Cavalo #3

>>> LARGADA AUTORIZADA! Todos os 5 cavalos partiram simultaneamente! <<<
...
--- Placar [Rodada 12] ---
Cavalo #1: [====================     ]  81 / 100 m
Cavalo #2: [=========================] 105 / 100 m
Cavalo #3: [==================       ]  75 / 100 m
Cavalo #4: [===============          ]  61 / 100 m
Cavalo #5: [==================       ]  73 / 100 m

===================================================
VENCEDOR OFICIAL: Cavalo #2 (distância final: 105 m)
SUA APOSTA      : Cavalo #3 -> NÃO FOI DESSA VEZ!
===================================================
```

---

## Exercício 2 — Buffer Circular Produtor-Consumidor e Impacto de $N$

### Explicação da Implementação (`ex02.c`)
- **Estrutura e Sincronização:** Implementou-se a estrutura `circular_buffer_t` parametrizada pela capacidade $N$, protegida por `pthread_mutex_t mutex` e duas variáveis de condição:
  - `not_full`: produtores aguardam via `while (cb->count == cb->capacity) pthread_cond_wait(&cb->not_full, &cb->mutex);` (espera ativa zero).
  - `not_empty`: consumidores aguardam via `while (cb->count == 0 && cb->consumed_total < TOTAL_ITEMS) pthread_cond_wait(&cb->not_empty, &cb->mutex);`.
- **Métricas Coletadas:** Usando `clock_gettime(CLOCK_MONOTONIC, ...)`, o programa mede o tempo de bloqueio de produtores (`total_prod_wait_ms`), o tempo de bloqueio de consumidores (`total_cons_wait_ms`) e o *throughput* global ($\text{itens consumidos} / \text{tempo total}$) para $N \in \{1, 2, 5, 10, 25, 50, 100\}$.

### Resultados Experimentais e Análise
Configuração: 4 Produtores, 4 Consumidores, 600 itens totais por teste.

| Tamanho $N$ | Tempo Total (s) | Throughput (itens/s) | Espera Produtor (ms) | Espera Consumidor (ms) | Espera Média (ms) |
| :---: | :---: | :---: | :---: | :---: | :---: |
| **1** | 0,0611 s | 9.822,2 | 0,0663 ms | 0,0333 ms | 0,0498 ms |
| **2** | 0,0580 s | 10.346,4 | 0,0570 ms | 0,0135 ms | 0,0352 ms |
| **5** | 0,0541 s | 11.093,4 | 0,0581 ms | 0,0017 ms | 0,0299 ms |
| **10** | 0,0551 s | 10.893,4 | 0,0418 ms | 0,0042 ms | 0,0230 ms |
| **25** | 0,0554 s | 10.837,6 | 0,0523 ms | 0,0017 ms | 0,0270 ms |
| **50** | 0,0562 s | 10.672,4 | 0,0193 ms | 0,0020 ms | 0,0106 ms |
| **100** | 0,0556 s | 10.800,9 | 0,0001 ms | 0,0031 ms | 0,0016 ms |

**Análise:** Quando o buffer é unitário ($N=1$), qualquer variação aleatória momentânea entre o ritmo de produção e o de consumo força o bloqueio imediato de produtores ou consumidores, resultando no menor *throughput* ($9.822,2\text{ itens/s}$) e no maior tempo médio de espera ($0,0498\text{ ms}$). À medida que $N$ cresce ($N \ge 5$), o buffer absorve flutuações estocásticas, reduzindo drasticamente o tempo de espera em fila (chegando a $0,0016\text{ ms}$ para $N=100$) e estabilizando a vazão próxima ao limite da taxa de geração/consumo.

---

## Exercício 3 — Transferências Bancárias e Invariante de Conservação

### Explicação da Implementação (`ex03.c`)
- **Cenário:** $M = 20$ contas bancárias iniciam com saldo de `R$ 1000` cada (soma global esperada de `R$ 20000`). $T = 8$ threads executam $2.500$ transferências aleatórias cada ($20.000$ transferências no total).
- **Travas por Conta com Ordem Canônica:** Cada conta possui seu próprio `pthread_mutex_t lock`, permitindo paralelismo real entre transferências que envolvem pares disjuntos de contas. Para evitar *deadlock* quando uma thread transfere de `A -> B` e outra de `B -> A`, os mutexes são sempre adquiridos em ordem crescente de índice:
  ```c
  int first = (from < to) ? from : to;
  int second = (from < to) ? to : from;
  pthread_mutex_lock(&accounts[first].lock);
  pthread_mutex_lock(&accounts[second].lock);
  ```
- **Comparação com Execução Sem Trava:** No modo sem trava, leituras e escritas concorrentes nos saldos sofrem *lost updates* (condição de corrida clássica em operações *read-modify-write*). No modo com trava, a conservação da soma global é validada via `assert(safe_sum == expected_total)`.

### Resultados Experimentais
```text
=== EXERCÍCIO 3: TRANSFERÊNCIAS BANCÁRIAS (20 CONTAS, 8 THREADS) ===
Saldo inicial por conta: R$ 1000 | Soma global esperada: R$ 20000
Total de transferências por simulação: 20000

[1] Execução SEM travas (condição de corrida):
    Soma global esperada : R$ 20000
    Soma global obtida   : R$ 20186 (Diferença / erro: R$ +186)
    Tempo de execução    : 2.18 ms

[2] Execução COM travas (1 mutex por conta em ordem canônica):
    Soma global esperada : R$ 20000
    Soma global obtida   : R$ 20000 (Diferença / erro: R$ +0)
    Tempo de execução    : 4.59 ms
    Verificação assert() : SUCESSO (safe_sum == expected_total comprovado!)
```

---

## Exercício 4 — Linha de Processamento (Pipeline de 3 Estágios) com Poison Pill

### Explicação da Implementação (`ex04.c`)
- **Arquitetura em Pipeline:** Três threads dedicadas compõem os estágios:
  1. `capture_stage`: gera $N = 100$ itens brutos (`raw_value = id * 10`) e os insere na fila limitada `queue_cap_to_proc` (capacidade 8).
  2. `processing_stage`: consome de `queue_cap_to_proc`, aplica a transformação `processed_value = raw_value * 2 + 7` e enfileira em `queue_proc_to_write` (capacidade 8).
  3. `writing_stage`: consome de `queue_proc_to_write`, grava os resultados em `pipeline_output.csv` e acumula o checksum dos itens gravados.
- **Protocolo de Encerramento Limpo (Poison Pill):** Após gerar o último item válido, a thread de Captura insere uma pílula de veneno (`id = -1`). Ao desenfileirar `id == -1`, a thread de Processamento repassa a mesma *poison pill* para `queue_proc_to_write` e encerra sua execução. Quando a thread de Gravação lê `id == -1`, fecha o arquivo e termina.
- **Prova de Ausência de Perda de Itens e Deadlock:** Asserções finais verificam que `captured_count == processed_count == written_count == 100`, que ambas as filas terminaram vazias e que `expected_checksum == actual_checksum == 101700`.

### Saída Experimental
```text
[Captura]       Finalizou envio de 100 itens + Poison Pill.
[Processamento] Recebeu Poison Pill, repassou ao estágio de Gravação e encerrou (100 itens processados).
[Gravação]      Recebeu Poison Pill, fechou 'pipeline_output.csv' e encerrou (100 itens gravados).

--- Verificação de Integridade e Encerramento Limpo ---
Itens capturados  : 100
Itens processados : 100
Itens gravados    : 100
Checksum esperado : 101700
Checksum gravado  : 101700
Resultado         : SUCESSO! Nenhum item perdido e encerramento sem deadlock.
```

---

## Exercício 5 — Pool Fixo de $N$ Threads para Tarefas CPU-Bound

### Explicação da Implementação (`ex05.c`)
- **Fila Concorrente e Pool de Threads:** O pool inicializa $N$ threads trabalhadoras (`worker_routine`) que aguardam tarefas em uma fila encadeada protegida por `pool->mutex` e `pool->cond`.
- **Leitura até `EOF` e Sinalização de Encerramento:** A thread principal lê inteiros de 64 bits a partir de `stdin` até encontrar `EOF`, enfileirando cada tarefa com `pool_enqueue` e acordando um worker com `pthread_cond_signal`. Ao atingir `EOF`, chama `pool_shutdown`, que marca `pool->shutdown = true` e executa `pthread_cond_broadcast(&pool->cond)`.
- **Processamento CPU-Bound:** Cada worker retira um nó da fila, libera o mutex imediatamente e executa fora da seção crítica o teste de primalidade determinístico $O(\sqrt{n})$ (`is_prime`) e o cálculo iterativo de Fibonacci (`fibonacci_iter`).
- **Garantia contra Perda de Tarefas:** Os workers só encerram quando `pool->size == 0 && pool->shutdown` é verdadeiro, assegurando que toda tarefa enfileirada antes do `EOF` seja completamente processada antes do término da thread.

### Saída Experimental
```text
=== EXERCÍCIO 5: THREAD POOL (4 THREADS) PARA TAREFAS CPU-BOUND ===
Lendo inteiros da entrada padrão (stdin) até EOF...

[Worker #3] Tarefa #03 | n =     15485863 | Primo: SIM | Fib(14) = 377
[Worker #4] Tarefa #02 | n =      1299709 | Primo: SIM | Fib(20) = 6765
[Worker #3] Tarefa #05 | n =     49979687 | Primo: SIM | Fib(78) = 8944394323791464
[Worker #2] Tarefa #01 | n =       104729 | Primo: SIM | Fib(60) = 1548008755920
[Worker #1] Tarefa #04 | n =     32452843 | Primo: SIM | Fib(14) = 377
[Worker #4] Tarefa #06 | n =     67867967 | Primo: SIM | Fib(48) = 4807526976
[Worker #3] Tarefa #07 | n =     86028121 | Primo: SIM | Fib( 2) = 1
[Worker #2] Tarefa #08 | n =    104395303 | Primo: SIM | Fib(74) = 1304969544928657

--- Resumo e Verificação de Thread-Safety ---
Worker #1 processou 1 tarefas
Worker #2 processou 2 tarefas
Worker #3 processou 3 tarefas
Worker #4 processou 2 tarefas
Total enfileirado : 8
Total concluído   : 8 (soma dos workers: 8)
Primos encontrados: 8
Verificação assert(): SUCESSO! Fila thread-safe e zero perda de tarefas.
```

---

## Exercício 6 — Soma Total e Histograma de Frequências em Paralelo (MapReduce)

### Explicação da Implementação (`ex06.c`)
- **Particionamento em Blocos e Leitura Paralela (`pread`):** O programa gera um arquivo binário de $16.777.216$ inteiros de 32 bits (`64 MB`) com valores no intervalo $[0, 99]$. Para $P$ threads, o domínio de índices é particionado em blocos contíguos `[start_index, end_index)`. Cada thread lê sua partição diretamente do descritor de arquivo usando `pread()`, que realiza leitura posicional thread-safe sem disputar o ponteiro global de arquivo (`lseek`).
- **Fase Map Local (Zero Locks):** Cada thread acumula sua soma parcial em `w->local_sum` e seu histograma parcial em `w->local_hist[100]` inteiramente em memória privada da thread, eliminando contenção de cache (*false sharing*) e de travas durante toda a varredura.
- **Fase Reduce (Exclusão Mútua Mínima):** Ao concluir 100% do seu bloco, cada thread adquire `reduce_mutex` **uma única vez** para somar seu resultado parcial aos acumuladores globais (`global_sum` e `global_hist`).

### Tabela de Speedup ($P = 1, 2, 4, 8$)

| Threads ($P$) | Tempo Médio (s) | Speedup ($T_1 / T_P$) | Eficiência | Soma Total Verificada |
| :---: | :---: | :---: | :---: | :---: |
| **1** | 0,0149 s | 1,00x | 100,0% | 830.534.027 |
| **2** | 0,0073 s | 2,04x | 102,1% | 830.534.027 |
| **4** | 0,0043 s | 3,51x | 87,9% | 830.534.027 |
| **8** | 0,0041 s | 3,67x | 45,9% | 830.534.027 |

**Análise:** Observa-se escalabilidade quase linear de $P=1$ para $P=2$ ($2,04\times$) e $P=4$ ($3,51\times$) graças à ausência total de contenção durante a fase *Map*. Em $P=8$, o *speedup* atinge $3,67\times$, começando a saturar devido à largura de banda de memória/sistema de arquivos nas chamadas `pread()`.

---

## Exercício 7 — Jantar dos Filósofos (Duas Soluções + Mitigação de Starvation)

### Explicação da Implementação (`ex07.c`)
- **Representação:** Os 5 garfos são modelados como `pthread_mutex_t forks[5]`.
- **Solução 7(a) — Ordem Global de Aquisição:** Cada filósofo $i$ identifica seus garfos `left = i` e `right = (i + 1) % 5`, mas sempre adquire primeiro `min(left, right)` e depois `max(left, right)`. Isso quebra a condição de **espera circular** de Coffman, tornando o *deadlock* impossível.
- **Solução 7(b) — Semáforo Limitando a 4 Filósofos:** Um semáforo contador (`room_sem`) inicializado com `4` garante que no máximo 4 filósofos tentem pegar garfos simultaneamente. Pelo Princípio da Casa dos Pombos, com 4 filósofos e 5 garfos, pelo menos um filósofo sempre conseguirá adquirir ambos os garfos e comer.
- **Mitigação de Starvation:** Foi adicionado o monitor `wait_fair_turn`, que verifica o número mínimo de refeições entre todos os filósofos (`min_meals`). Caso um filósofo rápido tente comer tendo uma vantagem $\ge 2$ refeições sobre o colega mais faminto (`meals[id] - min_meals >= MAX_MEAL_LEAD`), ele aguarda em `fairness_cond` até que os demais avancem, garantindo equidade estrita.

### Métricas Coletadas por Filósofo

**Solução 7(a): Ordem Global de Aquisição de Garfos (Tempo total: 58,78 ms)**

| Filósofo | Refeições | Espera Média (ms) | Maior Espera (ms) |
| :---: | :---: | :---: | :---: |
| **#1** | 40 | 0,440 ms | 1,209 ms |
| **#2** | 40 | 0,355 ms | 1,202 ms |
| **#3** | 40 | 0,315 ms | 1,660 ms |
| **#4** | 40 | 0,320 ms | 1,100 ms |
| **#5** | 40 | 0,463 ms | 1,288 ms |

**Solução 7(b): Semáforo Limitando a 4 Filósofos Simultâneos (Tempo total: 72,93 ms)**

| Filósofo | Refeições | Espera Média (ms) | Maior Espera (ms) |
| :---: | :---: | :---: | :---: |
| **#1** | 40 | 0,778 ms | 2,009 ms |
| **#2** | 40 | 0,811 ms | 2,115 ms |
| **#3** | 40 | 0,802 ms | 2,194 ms |
| **#4** | 40 | 0,746 ms | 2,236 ms |
| **#5** | 40 | 0,786 ms | 2,169 ms |

---

## Exercício 8 — Rajadas de Produção (Bursts), Ociosidade e Backpressure

### Explicação da Implementação (`ex08.c`)
- **Mecanismo de Backpressure com Histerese (High/Low Watermarks):** Em vez de bloquear apenas quando o buffer atinge 100% da capacidade (`BUFFER_CAPACITY = 30`), implementou-se controle de contrapressão por histerese:
  - Quando a ocupação atinge `HIGH_WATERMARK = 24` (80%), a flag `backpressure_active` é ativada e todos os produtores são colocados em espera na variável de condição `bp_cleared`.
  - Os produtores permanecem pausados enquanto os consumidores drenam o buffer até atingir `LOW_WATERMARK = 10` (~33%), momento em que `pthread_cond_broadcast(&g_buf.bp_cleared)` retoma a produção. Isso evita oscilações rápidas (*thrashing*) na borda superior do buffer.
- **Fases Simuladas:** O experimento percorre 4 regimes ao longo do tempo: (1) Rajada de produção (*Burst*), (2) Queda abrupta na taxa de consumo, (3) Período de ociosidade dos produtores e (4) Regime estável.

### Série Temporal da Ocupação do Buffer
```text
| Tempo (ms) | Fase Atual        | Ocupação | Backpressure | Gráfico de Ocupação (0..30)       |
|------------|-------------------|----------|--------------|-----------------------------------|
|      40 ms | 1. RAJADA (BURST) |  19 / 30 | ATIVO (WAIT) | [###################...........] |
|      80 ms | 1. RAJADA (BURST) |  23 / 30 | NORMAL       | [#######################.......] |
|     120 ms | 1. RAJADA (BURST) |  16 / 30 | NORMAL       | [################..............] |
|     240 ms | 1. RAJADA (BURST) |  20 / 30 | ATIVO (WAIT) | [####################..........] |
|     280 ms | 2. CONSUMO LENTO  |  24 / 30 | ATIVO (WAIT) | [########################......] |
|     320 ms | 2. CONSUMO LENTO  |  20 / 30 | ATIVO (WAIT) | [####################..........] |
|     360 ms | 2. CONSUMO LENTO  |  15 / 30 | ATIVO (WAIT) | [###############...............] |
|     400 ms | 2. CONSUMO LENTO  |  11 / 30 | ATIVO (WAIT) | [###########...................] |
|     440 ms | 2. CONSUMO LENTO  |  22 / 30 | ATIVO (WAIT) | [######################........] |
|     520 ms | 2. CONSUMO LENTO  |  12 / 30 | ATIVO (WAIT) | [############..................] |
|     560 ms | 3. PROD. OCIOSO   |  21 / 30 | ATIVO (WAIT) | [#####################.........] |
|     600 ms | 3. PROD. OCIOSO   |  10 / 30 | NORMAL       | [##########....................] |
|     640 ms | 3. PROD. OCIOSO   |   0 / 30 | NORMAL       | [..............................] |
|     800 ms | 4. ESTÁVEL        |   1 / 30 | NORMAL       | [#.............................] |
|     960 ms | 4. ESTÁVEL        |   3 / 30 | NORMAL       | [###...........................] |

Itens produzidos         : 241
Itens consumidos         : 241
Acionamentos Backpressure: 8 vezes (sempre que ocupação >= 24, liberando em <= 10)
```

---

## Exercício 9 — Corrida de Revezamento em Equipe com Barreira (`pthread_barrier_t`)

### Explicação da Implementação (`ex09.c`)
- **Barreira Cíclica Reutilizável:** Cada equipe possui $K$ threads corredoras que precisam concluir sua etapa atual antes que qualquer membro da equipe inicie a próxima perna da prova (`NUM_LEGS = 400`). Para garantir portabilidade total entre Linux e macOS, implementou-se a API `pthread_barrier_t` utilizando `pthread_mutex_t`, `pthread_cond_t` e um contador de fase (`generation`), evitando que threads recém-despertadas reentrem na barreira e misturem estados de rodadas consecutivas.
- A última thread a alcançar a barreira retorna `PTHREAD_BARRIER_SERIAL_THREAD`, incrementa o contador de rodadas concluídas e libera toda a equipe com `pthread_cond_broadcast`.

### Resultados Experimentais por Tamanho de Equipe ($K$)

| Equipe ($K$ threads) | Rodadas Concluídas | Tempo Total (s) | Rodadas / segundo | Rodadas / minuto |
| :---: | :---: | :---: | :---: | :---: |
| **2** | 400 | 0,2017 s | 1.982,8 | 118.965,6 |
| **4** | 400 | 0,2198 s | 1.820,3 | 109.215,0 |
| **8** | 400 | 0,2348 s | 1.703,5 | 102.209,9 |
| **16** | 400 | 0,2491 s | 1.605,7 | 96.343,4 |
| **32** | 400 | 0,2628 s | 1.522,1 | 91.325,2 |

**Análise:** Como a barreira sincroniza pelo elemento mais lento de cada rodada ($\max_{i \in \{1..K\}} T_i$), o aumento do tamanho da equipe $K$ eleva tanto o valor esperado do máximo de $K$ variáveis aleatórias (*efeito straggler*) quanto o custo de coordenação na barreira, reduzindo gradualmente a taxa de rodadas por minuto de $118.965,6$ ($K=2$) para $91.325,2$ ($K=32$).

---

## Exercício 10 — Simulação de Deadlock, Thread Watchdog e Correção por Ordem Total

### Explicação da Implementação (`ex10.c`)
- **Cenário 1 (Deadlock Proposital):** Quatro threads ($T_0, T_1, T_2, T_3$) competem por quatro recursos ($R_0, R_1, R_2, R_3$). Sem ordem global, cada thread $T_i$ adquire primeiro $R_i$ e em seguida solicita $R_{(i+1) \bmod 4}$. Quando todas adquirem seu primeiro recurso simultaneamente, forma-se o ciclo de espera circular $T_0 \to R_1 \to T_1 \to R_2 \to T_2 \to R_3 \to T_3 \to R_0$.
- **Thread Watchdog:** Uma thread monitora periodicamente o contador global de operações (`global_progress`) e a tabela de posse/espera (`resource_owner[]` e `waiting_for[]`). Ao detectar ausência de progresso por $T = 1,0\text{ s}$, o Watchdog emite um relatório completo apontando exatamente quais threads estão travadas e quem detém cada recurso aguardado, sinalizando o encerramento seguro do teste.
- **Cenário 2 (Correção com Ordem Total de Travamento):** Adota-se uma ordem total estrita sobre os índices dos recursos (`first = min(r1, r2)`, `second = max(r1, r2)`). Dessa forma, a thread $T_3$ passa a solicitar $R_0$ antes de $R_3$, eliminando o ciclo no grafo de alocação de recursos.

### Comparação Experimental dos Comportamentos
```text
=== EXERCÍCIO 10: DETECÇÃO DE DEADLOCK (WATCHDOG) E ORDEM TOTAL DE LOCKS ===
Configuração: 4 Threads, 4 Recursos, Timeout do Watchdog T = 1.0 s

>>> CENÁRIO 1: Aquisição de locks em ordens distintas (propenso a Deadlock) <<<

[WATCHDOG ALERTA] Ausência de progresso detectada por 1.03 s (progresso travado em 10 ops)!
--- Relatório de Diagnóstico de Recursos e Threads Suspeitas ---
  * Thread #0 (concluiu 0 ops): AGUARDANDO Recurso R1 (atualmente retido pela Thread #1)
  * Thread #1 (concluiu 5 ops): AGUARDANDO Recurso R2 (atualmente retido pela Thread #2)
  * Thread #2 (concluiu 0 ops): AGUARDANDO Recurso R3 (atualmente retido pela Thread #3)
  * Thread #3 (concluiu 5 ops): AGUARDANDO Recurso R0 (atualmente retido pela Thread #0)
  * Ciclo de espera circular confirmado: T0 -> R1 -> T1 -> R2 -> T2 -> R3 -> T3 -> R0!
Resumo do Cenário (SEM Ordem Total - Ordens Distintas):
  - Operações concluídas: 10 / 800
  - Deadlock detectado  : SIM (interrompido pelo Watchdog)
  - Tempo decorrido     : 1.141 s

>>> CENÁRIO 2: Correção com Ordem Total de Travamento (min(r1,r2) -> max(r1,r2)) <<<
Resumo do Cenário (COM Ordem Total de Locks):
  - Operações concluídas: 800 / 800
  - Deadlock detectado  : NÃO (0 bloqueios)
  - Tempo decorrido     : 0.210 s
```

