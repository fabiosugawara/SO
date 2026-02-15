//Manuel José Lopes Francisco – 2022217194
//Fabio Sugawara da Silva – 2020147048
#include <fcntl.h>   
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>
#include "deichain.h"
#include "pow.h"
volatile sig_atomic_t miner_end=0;

typedef struct {//tivemos de passar uma estrutura para cada uma das threads porque para alem do id precsiamos do semafero do SEM_WRITE_FILE para sincronizar escrita
    int id;
    ControllerInfo info;
} MinerThreadArgs;

void sigint_handler_miner(int signum) {
    miner_end=1;
    printf("\nMiner recebeu o sinal para terminar...\n");
fflush(stdout);
}


void sendValidator(TransactionBlock *blk,ControllerInfo info){
    PRINT_SYNC(info.sem_write_file,"A enviar bloco %s para o validator",blk->txb_id);
        int fd = open(info.pipe_miner_validator, O_WRONLY);
        if (fd == -1) {
            perror("open WRONLY");
            exit(EXIT_FAILURE);
        }

    size_t blk_size = sizeof(TransactionBlock) + info.transactions_per_block * sizeof(Transaction);
    write(fd, blk, blk_size);
    close(fd);
    free(blk); // Liberar memória alocada
}
char *getPrevHash(ControllerInfo info){
    TransactionBlock *ledger = shmat(info.blockchain_ledger, NULL, 0);
    if (ledger == (void *) -1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    sem_wait(info.sem_ledger);
    int ultimo_blk = -1;
    for (int i = 0; i < info.blockchain_blocks; i++) {
        if (ledger[i].nonce != 0) {  // ou outro critério que uses
            ultimo_blk = i;
        }
    }
    static char previous_hash[HASH_SIZE];
    if (ultimo_blk == -1) { //nenhum bloco tem nonce!=0 -> ledger está vazio
        strcpy(previous_hash, INITIAL_HASH);
    } else {
        compute_sha256(&ledger[ultimo_blk], previous_hash,info.transactions_per_block);
    }
    sem_post(info.sem_ledger);
    return previous_hash;
}

TransactionBlock* makeBlock(int id, Transaction *transactions, ControllerInfo info) {
    PRINT_SYNC(info.sem_write_file,"Thread %d a criar Bloco",id);
    static int counter = 0;

    // Alocar memória suficiente para o bloco + todas as transações
    size_t blk_size = sizeof(TransactionBlock) + info.transactions_per_block * sizeof(Transaction);
    TransactionBlock *blk = malloc(blk_size);
    if (!blk) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    // Preencher os campos do bloco
    snprintf(blk->txb_id, sizeof(blk->txb_id), "BLK%d-%d", id, counter);
    counter++;
    blk->timestamp = time(NULL);
    blk->nonce = 0; // Inicia o nonce, será preenchido no PoW

    // Obter o hash do bloco anterior
    char *previous_hash = getPrevHash(info);
    strcpy(blk->previous_block_hash, previous_hash);
    PRINT_SYNC(info.sem_write_file,"Encontrada previus hash: %s",blk->previous_block_hash);
    // Copiar as transações para o bloco
    memcpy(blk->transactions, transactions, info.transactions_per_block * sizeof(Transaction));
    // Realizar o Proof-of-Work
    PoWResult pow_result;
    do {
        blk->timestamp = time(NULL); // Atualizar o timestamp para novas tentativas
        pow_result = proof_of_work(blk, info.transactions_per_block);
        if (pow_result.error) {
            fprintf(stderr, "Failed to find a valid nonce. Retrying...\n");
        }
    } while (pow_result.error == 1);

    return blk;
}

void ler_tx_pool(int id,ControllerInfo info) {
    if (miner_end) {
        PRINT_SYNC(info.sem_write_file, "Thread %d recebeu sinal e irá terminar antes de minerar!", id);
        return;
    }
    TransactionPool *tx_pool = (TransactionPool *)shmat(info.transaction_pool, NULL, 0);
    if (tx_pool == (void *)-1) {
        perror("Erro ao mapear memória");
        exit(EXIT_FAILURE);
    }
    sem_wait(info.sem_items_tx_pool);//espera que o txgen escreva na pool;

    sem_wait(info.sem_mutex_tx_pool);//semafero para entrar na pool;
    int cont=0;
    Transaction transactions[info.transactions_per_block];
    //lê tx da pool
     for (int i = 0; i < tx_pool->pool_size; i++) {
	if(cont>=info.transactions_per_block){break;}
        if (tx_pool->transactions_pending_set[i].empty == 1 ) {  // Encontrou slot ocupado
            PRINT_SYNC(info.sem_write_file,"Thread %d leu %s da transaction pool!",id,tx_pool->transactions_pending_set[i].tx.tx_id);
	transactions[cont]=tx_pool->transactions_pending_set[i].tx;
	cont+=1; // Sai do loop, pois já escreveu
        }
    }
    sem_post(info.sem_mutex_tx_pool);
    TransactionBlock *blk = makeBlock(id, transactions, info);
    sendValidator(blk, info);
}
// Função que cada thread miner executará
void* miner_thread(void* arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    MinerThreadArgs *args = (MinerThreadArgs*)arg;
    int id = args->id;
    ControllerInfo info = args->info;
    PRINT_SYNC(info.sem_write_file, "Thread %d está ativa e pronta para minerar!", id);
    while(miner_end!=1){
    ler_tx_pool(id,info);
        pthread_testcancel();
        if (miner_end) {
            PRINT_SYNC(info.sem_write_file, "Thread %d detetou fim e vai sair após ciclo.", id);
            break;
        }
}
    PRINT_SYNC(info.sem_write_file, "Thread %d terminando mineração...", id);
    return NULL;
}

// Função principal para criar os mineradores
void miner(ControllerInfo info) {
 struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler_miner;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Erro ao instalar handler de SIGTERM");
        exit(EXIT_FAILURE);
    }

    PRINT_SYNC(info.sem_write_file, "Miner PID: %d", getpid());
    PRINT_SYNC(info.sem_write_file, "Processo mineiro lançado corretamente!");

    pthread_t threads[info.num_miners];
    MinerThreadArgs thread_args[info.num_miners];

    for (int i = 0; i < info.num_miners; i++) {
        thread_args[i].id = i;
        thread_args[i].info =info;

        PRINT_SYNC(info.sem_write_file, "A criar miner thread de id - %d", i);
        
        if (pthread_create(&threads[i], NULL, miner_thread, &thread_args[i]) != 0) {
            PRINT_SYNC(info.sem_write_file, "Erro ao criar a miner thread de id - %d", i);
        }
    }
    while (!miner_end) {
    pause();
}

    for (int i = 0; i < info.num_miners; i++) {
        pthread_cancel(threads[i]);
    }
    for (int i = 0; i < info.num_miners; i++) {
    sem_post(info.sem_items_tx_pool);
}
    sem_post(info.sem_mutex_tx_pool);
    sem_post(info.sem_write_file);
    for (int i = 0; i < info.num_miners; i++) {
        int result =pthread_join(threads[i], NULL);
        if(result==0){
        PRINT_SYNC(info.sem_write_file,"Thread %d - a sair!",i);
        }else{
        PRINT_SYNC(info.sem_write_file,"Thread %d teve um erro ao sair de código - %d",i,result);}
    }
    PRINT_SYNC(info.sem_write_file, "Miner terminou a execução.");
    exit(EXIT_SUCCESS);
}