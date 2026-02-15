//Manuel José Lopes Francisco – 2022217194
//Fabio Sugawara da Silva – 2020147048
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>     // Para fork(), execl(), wait()
#include <sys/wait.h>   // Para wait()
#include <semaphore.h>  // Para sem_open(), etc.
#include <fcntl.h>      // Para O_CREAT, etc.
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include "Writer.h"
#include "Miner.h"
#include <mqueue.h>
#define PIPE_MINER_VALIDATOR "/tmp/pipe_miner_validator"
#define QUEUE_NAME  "/fila_validator_estatistica"
#include <sys/ipc.h>
#include <sys/msg.h>
#include "Validator.h"
#include "Statistics.h"
#include "deichain.h"
#define CONFIG_FILE "config.cfg"
#define KEY_TX_POOL 5367
#define KEY_LEDGER 8764
volatile sig_atomic_t end = 0;

// Função para ler o arquivo de configuração
bool read_config_file(int *var1, int *var2, int *var3, int *var4) {
    FILE *file = fopen(CONFIG_FILE, "r"); // Abre o arquivo para leitura
    if (file == NULL) {
        perror("Erro ao abrir o ficheiro de configuração!");
        return false; // Retorna false em caso de erro
    }
    //como fscanf(...) devolve 1 se a leitura for bem-sucedida podemos usar
    //uma variavél para confirmar se o ficheiro de configuraçaõ tem o numero de argumentos correto (5)
    // Lê os valores do arquivo e atribui às variáveis
    int NUM_CONFIGS = 0; // Contador de valores lidos com sucesso
    NUM_CONFIGS += fscanf(file, "%d", var1); // Lê o número de miners
    NUM_CONFIGS += fscanf(file, "%d", var2); // Lê o tamanho do pool
    NUM_CONFIGS += fscanf(file, "%d", var3); // Lê transações por bloco
    NUM_CONFIGS += fscanf(file, "%d", var4); // Lê número máximo de blocos
    fclose(file); // Fecha o arquivo

    // Verifica se todos os 5 valores foram lidos com sucesso
    if (NUM_CONFIGS != 4) {
        printf("Erro ao ler o ficheiro de configuração!\n"
       "Verifique que:\n"
       " - O ficheiro contém exatamente 4 valores.\n"
       " - Todos os valores são inteiros.\n"
       " - Os valores estão separados por paragrafos.\n");
        return false; // Retorna false se o número de valores estiver incorreto
    }

    return true; // Retorna true se tudo estiver correto
}

// Handler para capturar SIGINT (Ctrl+C)
void sigint_handler(int signum) {
    printf("Simulação terminada por SIGINT (Ctrl+C).\n");
    end=1;
}


int main(){
reset_log_file();//Apaga os valores de proramas passados do ficheiro log

//Criar semafero para ficheiro log____________________________
   sem_unlink("/semaforo_posix_write_file"); 
   sem_t *SEM_WRITE_FILE = sem_open("/semaforo_posix_write_file", O_CREAT, 0644, 1);
    if (SEM_WRITE_FILE == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
//______________________________________________

    // Variáveis para armazenar os valores lidos do arquivo
    int num_miners, pool_size, transactions_per_block, blockchain_blocks;
    if (read_config_file(&num_miners, &pool_size, &transactions_per_block, &blockchain_blocks)==false) {
        return 1; // Encerra o programa (já que não temos configurações não é possivel continuar)
    }
    //Guardar em const para ter a certeza que não são alterados(?é preciso?)
    PRINT_SYNC(SEM_WRITE_FILE,"Valores de configuração: ");
    const int NUM_MINERS = num_miners;
    PRINT_SYNC(SEM_WRITE_FILE," - NUM MINERS: %d",NUM_MINERS);
    const int POOL_SIZE = pool_size;
    PRINT_SYNC(SEM_WRITE_FILE," - POOL SIZE: %d",POOL_SIZE);
    const int TRANSACTIONS_PER_BLOCK = transactions_per_block;
    PRINT_SYNC(SEM_WRITE_FILE," - TRANSACTIONS_PER_BLOCK: %d",TRANSACTIONS_PER_BLOCK);
    const int BLOCKCHAIN_BLOCKS = blockchain_blocks;
    PRINT_SYNC(SEM_WRITE_FILE," - BLOCKCHAIN_BLOCKS: %d",BLOCKCHAIN_BLOCKS);


int size_tx_pool = sizeof(TransactionPool) + POOL_SIZE * sizeof(TransactionEntry);
int transaction_pool = shmget(KEY_TX_POOL, size_tx_pool, IPC_CREAT | 0666);
if (transaction_pool < 0) {
    perror("Erro no shmget");
    exit(EXIT_FAILURE);
}
printf("transaction_pool: %d",transaction_pool);
TransactionPool *tx_pool_ptr = (TransactionPool *)shmat(transaction_pool, NULL, 0);
if (tx_pool_ptr == (void *)-1) {
    perror("Erro ao associar memória");
    exit(EXIT_FAILURE);
}
tx_pool_ptr->pool_size = POOL_SIZE;
// Inicializar o conteúdo da pool
for (int i = 0; i < POOL_SIZE; i++) {
    tx_pool_ptr->transactions_pending_set[i].empty = 0;
    tx_pool_ptr->transactions_pending_set[i].age = 0;
}

shmdt(tx_pool_ptr);  // desanexa -> não é preciso neste processo
    
    PRINT_SYNC(SEM_WRITE_FILE,"Criada Memória Partilhada Transaction Pool!\n - Transaction Pool id:  %d", transaction_pool);

sem_unlink("/sem_mutex_tx_pool");//semáfero para acesso á memoria partilhada transaction pool
sem_unlink("/sem_items_tx_pool");//semáfero de contagem d a memoria partilhada transaction pool

sem_t *sem_mutex_tx_pool = sem_open("/sem_mutex_tx_pool", O_CREAT | O_EXCL, 0666, 1);    // binário
sem_t *sem_items_tx_pool = sem_open("/sem_items_tx_pool", O_CREAT | O_EXCL, 0666, 0);    // contador // contador
    if (sem_mutex_tx_pool == SEM_FAILED || sem_items_tx_pool == SEM_FAILED ) {
        perror("Erro ao abrir semáforos");
        exit(EXIT_FAILURE);
    }

//________________________________________________
//Cria memória partilhada blockchain ledger e semafero para a aceder
int blockchain_ledger; 
if ((blockchain_ledger = shmget(KEY_LEDGER, BLOCKCHAIN_BLOCKS*sizeof(TransactionBlock), IPC_CREAT | 0777)) < 0) {
        perror("Error in shmget()");
        exit(EXIT_FAILURE);
    }
    PRINT_SYNC(SEM_WRITE_FILE,"Criada Memória Partilhada Blockchain Ledger!\n - Blockchain Ledger id:  %d", blockchain_ledger);
sem_unlink("/semaforo_blockchain_ledger"); 
    sem_t *SEM_LEDGER = sem_open("/semaforo_blockchain_ledger", O_CREAT, 0644, 1);
    if (SEM_LEDGER == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
    
//############################################
//criar named pipe (miner->escreve) (validator->lê)
    unlink(PIPE_MINER_VALIDATOR); // Remove o pipe antigo, se existir
    if (mkfifo(PIPE_MINER_VALIDATOR, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
    }
//#############################################    
    key_t key;
    int msgid;

    // Criar uma chave única (use o mesmo arquivo no receptor)
    key = ftok("message_queue_key.txt", 65);
    if (key == -1) {
        perror("ftok");
        exit(1);
    }

    // Criar a fila de mensagens
    msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        exit(1);
    }
    PRINT_SYNC(SEM_WRITE_FILE,"Criada Memória Message Queue!\n - Message Queue id:  %d", msgid);
//____________________________________________________
//Lançar processos filhos


    ControllerInfo info;
    info.sem_write_file = SEM_WRITE_FILE;
    info.num_miners = NUM_MINERS;
    info.pool_size = POOL_SIZE;
    info.transactions_per_block = TRANSACTIONS_PER_BLOCK;
    info.blockchain_blocks = BLOCKCHAIN_BLOCKS;
    info.transaction_pool = transaction_pool;
    info.blockchain_ledger = blockchain_ledger;
    info.sem_mutex_tx_pool = sem_mutex_tx_pool;
    info.sem_items_tx_pool = sem_items_tx_pool;
    info.sem_ledger = SEM_LEDGER;
    strcpy(info.pipe_miner_validator, PIPE_MINER_VALIDATOR);
    info.msgid = msgid;
   pid_t MINER_PID, VALIDATOR_PID, STATISTICS_PID;
   
    PRINT_SYNC(SEM_WRITE_FILE,"A criar processo filho - Miner!");
    if ((MINER_PID=fork()) == 0) {
        miner(info);//semafero para escrever no log e num miner para threads
       exit(1);
    } else if (MINER_PID < 0) {
        perror("Erro ao criar processo filho (miner)\n");
        return 1;
    }
    PRINT_SYNC(SEM_WRITE_FILE, "Miner PID: %d", MINER_PID);
    PRINT_SYNC(SEM_WRITE_FILE,"A criar processo filho - Validator!");
    if ((VALIDATOR_PID=fork()) == 0) {
        validator(info);
        exit(1);
    } else if (VALIDATOR_PID < 0) {
        perror("Erro ao criar processo filho (validator)\n");
        return 1;
    }

     PRINT_SYNC(SEM_WRITE_FILE,"A criar processo filho - Statistics!");
     if ((STATISTICS_PID=fork()) == 0) {
        statistics(info);
        exit(1);
    } else if (STATISTICS_PID < 0) {
        perror("Erro ao criar processo filho (statistics)\n");
        return 1;
    }

    signal(SIGINT, sigint_handler);
while (!end) {
    pause(); // Bloqueia até receber um sinal
}
    // Quando sair do loop, envia SIGTERM aos filhos e aguarda a finalização
    PRINT_SYNC(SEM_WRITE_FILE,"A mandar sinal para os filhos!");
    kill(MINER_PID, SIGINT);
    waitpid(MINER_PID, NULL, 0);

    kill(VALIDATOR_PID, SIGINT);
    kill(STATISTICS_PID, SIGINT);

    // Espera que todos os filhos terminem corretamente
    waitpid(VALIDATOR_PID, NULL, 0);
    waitpid(STATISTICS_PID, NULL, 0);
PRINT_SYNC(SEM_WRITE_FILE,"filhos voltaram!");

    shmctl(transaction_pool, IPC_RMID, NULL);  // Libera a memória compartilhada
    shmctl(blockchain_ledger, IPC_RMID, NULL); // Libera a memória compartilhada
    sem_close(sem_mutex_tx_pool);
    sem_unlink("/sem_mutex_tx_pool");
        sem_close(sem_items_tx_pool);
    sem_unlink("/sem_items_tx_pool");
    sem_close(SEM_LEDGER);
    sem_unlink("/semaforo_blockchain_ledger");
    sem_close(SEM_WRITE_FILE);
    sem_unlink("/semaforo_posix_write_file");
    msgctl(msgid, IPC_RMID, NULL);
exit(EXIT_SUCCESS);
}













