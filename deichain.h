#ifndef __DEICHAIN_H__
#define __DEICHAIN_H__

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <semaphore.h>
#include <stdbool.h>

#define TX_ID_LEN 64
#define TXB_ID_LEN 64
#define HASH_SIZE 65  // SHA256_DIGEST_LENGTH * 2 + 1

extern size_t transactions_per_block;
typedef struct {
  sem_t *sem_write_file;
  int num_miners;
  int pool_size;
  int transactions_per_block;
  int blockchain_blocks;
  int transaction_pool;//id transaction pool para abrir
  int blockchain_ledger;//id ledger para abrir
  sem_t *sem_mutex_tx_pool;
  sem_t *sem_items_tx_pool;
  sem_t *sem_ledger;
  char pipe_miner_validator[64];
  int msgid;
}ControllerInfo;

// Transaction structure
typedef struct {
  char tx_id[TX_ID_LEN];  // Unique transaction ID (e.g., PID + #)
  char sender_id[50];
  char receiver_id[50];
  int reward;             // Reward associated with PoW
  float value;            // Quantity or value transferred
  time_t timestamp;       // Creation time of the transaction
} Transaction;

// Transaction Block structure
typedef struct {
  char txb_id[TXB_ID_LEN];              // Unique block ID (e.g., ThreadID + #)
  char previous_block_hash[HASH_SIZE];  // Hash of the previous block
  time_t timestamp;                     // Time when block was created
  unsigned int nonce;                   // PoW solution
    Transaction transactions[];            // Array of transactions
} TransactionBlock;

typedef struct {
  bool empty;  //0-> vazio | 1 -> ocupado
  int age;
  Transaction tx;
} TransactionEntry;

typedef struct {
  int pool_size;
  TransactionEntry transactions_pending_set[];
} TransactionPool;

typedef struct {
  int miner_id;
  bool valid;
  int reward;
  long time;
}MinerBlockInfo;

typedef struct{
  long mtype; // sempre necessário para message queue
  MinerBlockInfo blockInfo; // envia uma cópia do struct completo
}Estatisticas;

// Inline function to compute the size of a TransactionBlock
static inline size_t get_transaction_block_size(int transactions_per_block) {
  if (transactions_per_block == 0) {
    perror("Must set the 'transactions_per_block' variable before using!\n");
    exit(-1);
  }
  return sizeof(TransactionBlock) +
         transactions_per_block * sizeof(Transaction);
}

#endif