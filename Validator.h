//Manuel José Lopes Francisco – 2022217194
//Fabio Sugawara da Silva – 2020147048
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include "Writer.h"
#include "deichain.h"
#include "pow.h"
#include <sys/time.h>
#include <stdbool.h>
#include <mqueue.h>

volatile sig_atomic_t validator_end = 0;
void sigint_handler_validator(int sig) {
    validator_end = 1;
}

void removeTransactions(Transaction *transactions,ControllerInfo info){
     TransactionPool *tx_pool = (TransactionPool *)shmat(info.transaction_pool, NULL, 0);
    if (tx_pool == (void *)-1) {
        perror("Erro ao mapear memória");
        exit(EXIT_FAILURE);
    }
 sem_wait(info.sem_mutex_tx_pool);
for(int i=0;i<info.transactions_per_block;i++){
 for (int j = 0; j < tx_pool->pool_size; j++) {//verificamos por id de transação (único)
     tx_pool->transactions_pending_set[j].age+=1;
     //tx_pool->transactions_pending_set[j].tx.reward+=;
            if (strcmp(tx_pool->transactions_pending_set[j].tx.tx_id, transactions[i].tx_id) == 0) {
                tx_pool->transactions_pending_set[j].empty = 0;  // Marca como livre
                break;  // faz break so do 2 for -> vai para a proxima transacao do array dado
            }
        }


}
//sem_post(sem_mutex_tx_pool);
sem_post(info.sem_mutex_tx_pool);
}

void writeLedge(TransactionBlock blk,ControllerInfo info){//substituir o static pos por poricura nonce==0 parecido com o miner
static int pos=0;
    TransactionBlock *ledger = (TransactionBlock *)shmat(info.blockchain_ledger, NULL, 0);
    if (ledger == (void *)-1) {
        perror("Erro ao mapear memória partilhada");
        exit(EXIT_FAILURE);
    }
    sem_wait(info.sem_ledger);
    strcpy(ledger[pos].txb_id, blk.txb_id);
    strcpy(ledger[pos].previous_block_hash, blk.previous_block_hash);
    ledger[pos].timestamp= blk.timestamp;
    pos+=1;

sem_post(info.sem_ledger);
    shmdt(ledger);
}

int getMinerId(TransactionBlock blk) {
    int miner_id = 0;
    sscanf(blk.txb_id, "BLK%d-%*d", &miner_id);
    return miner_id;
}
void validateBlock(TransactionBlock blk,ControllerInfo info,MinerBlockInfo blockInfo){
    if (verify_nonce(&blk,info.transactions_per_block)==1) {
        PRINT_SYNC(info.sem_write_file,"Bloco %s Válido! A adicionar ao ledger",blk.txb_id);
        blockInfo.valid=true;
        for(int i=0;i<info.transactions_per_block;i++){blockInfo.reward+=blk.transactions[i].reward;}
        writeLedge(blk,info);
        removeTransactions(blk.transactions,info);
    }else {
        PRINT_SYNC(info.sem_write_file,"Bloco %s Inválido!",blk.txb_id);
        blockInfo.valid=false;
    }
}
void send_stats_message(ControllerInfo info, MinerBlockInfo blockInfo) {
    Estatisticas msg;
    msg.mtype = 1;
    msg.blockInfo = blockInfo; // copia os dados da struct

    if (msgsnd(info.msgid, &msg, sizeof(Estatisticas) - sizeof(long), 0) == -1) {
        perror("Erro ao enviar mensagem para statistics");
    }
}
void readPipe(ControllerInfo info){
 MinerBlockInfo blockInfo;
int fd = open(info.pipe_miner_validator, O_RDONLY);
        if (fd == -1) {
            perror("open RDONLY");
            exit(EXIT_FAILURE);
        }
    TransactionBlock *blk = malloc(sizeof(TransactionBlock) + info.transactions_per_block * sizeof(Transaction));
    if (!blk) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    read(fd, blk, sizeof(TransactionBlock) + info.transactions_per_block * sizeof(Transaction));
    PRINT_SYNC(info.sem_write_file,"Transação %s lida do pipe", blk->txb_id);
    close(fd);

    blockInfo.miner_id = getMinerId(*blk);
    struct timeval start, end;
    gettimeofday(&start, NULL);
    validateBlock(*blk, info,blockInfo);  // passa por valor
    gettimeofday(&end, NULL);
    long micros = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    blockInfo.time = micros;
    send_stats_message(info,blockInfo);
    free(blk);
}


void validator(ControllerInfo info) {
    PRINT_SYNC(info.sem_write_file, "Processo validator lançado corretamente!");
    signal(SIGINT, sigint_handler_validator);


while (!validator_end) {
    readPipe(info);
}
    PRINT_SYNC(info.sem_write_file, "Processo validator a encerrar!");
}