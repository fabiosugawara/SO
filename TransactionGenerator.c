//Manuel José Lopes Francisco – 2022217194
//Fabio Sugawara da Silva – 2020147048
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include "deichain.h"
#define KEY_TX_POOL 5367
volatile sig_atomic_t end_transactiongenerator = 0;

void signal_handler(int signum) {
    if (signum == SIGINT) {
        end_transactiongenerator = 1;
        printf("Aterminar transaction generator\n");
    }
}

void generate_transaction_id(char *buffer, size_t size) {
    static int counter = 0;
    snprintf(buffer, size, "TX%d-%d", getpid(), counter);
    counter++;
}

Transaction *generate_transaction(int reward) {
    Transaction *tx = malloc(sizeof(Transaction));
    if (!tx) {
        perror("Erro ao alocar memória para a transação");
        return NULL;
    }
    
    generate_transaction_id(tx->tx_id, sizeof(tx->tx_id));
    snprintf(tx->sender_id, sizeof(tx->sender_id), "S%d", getpid());
    snprintf(tx->receiver_id, sizeof(tx->receiver_id), "R%d", rand() % 100);
    tx->value = (double)(rand() % 1000) / 10.0;
    tx->reward = reward;
    tx->timestamp = time(NULL);
    return tx;
}

void print_transaction(const Transaction *tx) {
    printf("Transação:\n");
    printf(" - ID: %s\n", tx->tx_id);
    printf(" - De: %s Para: %s\n", tx->sender_id, tx->receiver_id);
    printf(" - Valor: %.2f\n", tx->value);
    printf(" - Recompensa: %d\n", tx->reward);
    //printf(" - Timestamp: %s\n", tx->timestamp);
    char time_str[20];
    struct tm *tm_info = localtime(&tx->timestamp);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("Timestamp: %s\n", time_str);
}

void escreve_tx_pool(Transaction *tx) {
    printf("Tentando escrever transação no pool...\n");

    // Abrir semáforos
    sem_t *sem_mutex_tx_pool = sem_open("/sem_mutex_tx_pool", 0);    // binário
    sem_t *sem_items_tx_pool = sem_open("/sem_items_tx_pool", 0);    // contador
    //sem_t *sem_empty_tx_pool = sem_open("/sem_empty_tx_pool", 0);  

    if (sem_mutex_tx_pool == SEM_FAILED || sem_items_tx_pool == SEM_FAILED /*|| sem_empty_tx_pool == SEM_FAILED*/) {
        perror("Erro ao abrir semáforos");
        exit(EXIT_FAILURE);
    }
    printf("Semáforos abertos!\n");

    // Obter memória partilhada
    int shmid = shmget(KEY_TX_POOL, 0, 0666);
    if (shmid == -1) {
        perror("Erro ao obter memória partilhada");
        exit(EXIT_FAILURE);
    }
    printf("Memória partilhada obtida!\n");

    // Mapear a memória compartilhada
    TransactionPool *tx_pool = (TransactionPool *)shmat(shmid, NULL, 0);
    if (tx_pool == (void *)-1) {
        perror("Erro ao mapear memória");
        exit(EXIT_FAILURE);
    }
    printf("Memória mapeada!\n");

    // Bloqueia acesso à pool
    printf("Antes do semáforo mutex\n");
    sem_wait(sem_mutex_tx_pool);
    printf("Depois do semáforo mutex\n");

    // Itera sobre o pool e busca um slot vazio
    for (int i = 0; i < tx_pool->pool_size; i++) {
        printf("Procurando slot vazio: %d\n", i);
        printf("Status do slot %d: empty=%d\n", i,tx_pool->transactions_pending_set[i].empty);

        if (tx_pool->transactions_pending_set[i].empty == 0) {  // Encontrou slot vazio
            printf("Slot vazio encontrado no índice %d\n", i);
            tx_pool->transactions_pending_set[i].tx = *tx;
            tx_pool->transactions_pending_set[i].empty = 1;  // Marca como ocupado
            tx_pool->transactions_pending_set[i].age = 0;  // Começa a idade em 0
            printf("Transação: %s escrita na Pool\n", tx->tx_id);
            break; // Sai do loop, pois já escreveu
        }
    }
    for (int j = 0; j < tx_pool->pool_size; j++) {
    printf("Status do slot %d: empty=%d\n", j, tx_pool->transactions_pending_set[j].empty);
}

    // Libera o semáforo de mutex
    sem_post(sem_mutex_tx_pool);
    // Sinaliza que um item foi adicionado
    sem_post(sem_items_tx_pool);

    printf("Escrita completa!\n");
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <reward(1-3)> <sleep_time(200-300)>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int reward = atoi(argv[1]);
    int sleep_time = atoi(argv[2]);

    if (reward < 1 || reward > 3 || sleep_time < 200 || sleep_time > 300) {
        fprintf(stderr, "Parâmetros inválidos!\n");
        return EXIT_FAILURE;
    }

    srand(time(NULL)); // Inicializar gerador de números aleatórios

    pid_t pid = fork();
    if (pid == 0) { // Processo filho
        printf("Transaction Generator (PID %d) iniciado...\n", getpid());
        
        while(!end_transactiongenerator) {
            Transaction *tx = generate_transaction(reward);
            if (!tx) {
                fprintf(stderr, "Erro na criação da transação!\n");
                exit(EXIT_FAILURE);
            }
            
            print_transaction(tx);
            escreve_tx_pool(tx);
            free(tx);
            
            usleep(sleep_time * 1000); // Espera em microssegundos
        }
        
        printf("Transaction Generator terminado.\n");
        exit(EXIT_SUCCESS);
    } 
    else if (pid > 0) { // Processo pai
        waitpid(pid, NULL, 0); // Esperar pelo filho
        return EXIT_SUCCESS;
    }     else {
        perror("Erro ao criar processo filho");
        return EXIT_FAILURE;
    }
}