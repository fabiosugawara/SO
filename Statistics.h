#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include "deichain.h"
#include "Writer.h"
#include <mqueue.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
typedef struct {
    int total_blocks;
    int valid_blocks;
    int invalid_blocks;
    int total_reward;
    long total_time;
} MinerStats;

static volatile sig_atomic_t sigusr1_received = 0;
static volatile sig_atomic_t sigint_received = 0;

void sigusr1_handler(int signum) {
    sigusr1_received = 1;
}

void sigint_handler_statistics(int signum) {
    sigint_received = 1;
}


    void print_stats(MinerStats stats[], ControllerInfo info){
        char buffer[info.num_miners*4096]; // Aumente se necessário
        int offset = 0;

        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
            "########################  Estatisticas  ########################\n");

        long total_time = 0;
        int total_blocks = 0;

        for (int i = 0; i < info.num_miners; i++) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                "Miner %d - Blocos Válidos: %d, Blocos Inválidos: %d\n",
                i, stats[i].valid_blocks, stats[i].invalid_blocks);
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                "Miner %d - Créditos: %d\n\n", i, stats[i].total_reward);

            total_time += stats[i].total_time;
            total_blocks += stats[i].valid_blocks + stats[i].invalid_blocks;
        }

        double avg_time = total_blocks > 0 ? (double) total_time / total_blocks : 0;

        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
            "Tempo Médio de Verificação das Transações: %f microsegundos\n", avg_time);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
            "Total de blocos validados: %d\n", total_blocks);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
            "########################  Estatisticas  ########################\n\n");

        PRINT_SYNC(info.sem_write_file, "%s", buffer);
    

}

void statistics(ControllerInfo info) {
    // Array automático no stack
    MinerStats stats[info.num_miners]; // VLA
    for (int i = 0; i < info.num_miners; i++) {
        stats[i].total_blocks = 0;
        stats[i].valid_blocks = 0;
        stats[i].total_reward = 0;
        stats[i].total_time = 0;
    }

    // Sinais
    struct sigaction sa_usr1 = {.sa_handler = sigusr1_handler};
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    sigaction(SIGUSR1, &sa_usr1, NULL);

    struct sigaction sa_int = {.sa_handler = sigint_handler_statistics};
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    Estatisticas msg;
    while (!sigint_received) {
        ssize_t bytes = msgrcv(info.msgid, &msg, sizeof(MinerBlockInfo), 0, IPC_NOWAIT);
        if (bytes >= 0) {
            int id = msg.blockInfo.miner_id;
            if (id >= 0 && id < info.num_miners) {
                stats[id].total_blocks++;
                stats[id].total_time += msg.blockInfo.time;
                if (msg.blockInfo.valid) {
                    stats[id].valid_blocks++;
                    stats[id].total_reward += msg.blockInfo.reward;
                }else if(msg.blockInfo.valid == false){
                  stats[id].invalid_blocks++;
                }
            }
        } else {
            pause();// dorme um pouco para evitar busy-wait
        }

        if (sigusr1_received) {
            print_stats(stats, info);
            sigusr1_received = 0;
        }
    }

    // Encerramento
    print_stats(stats, info);
    fflush(stdout);
    if (msgctl(info.msgid, IPC_RMID, NULL) == -1) {
        perror("msgctl(IPC_RMID)");
    } else {
        PRINT_SYNC(info.sem_write_file,"Fila de mensagens removida.\n");
    }
    PRINT_SYNC(info.sem_write_file,"Statistics a encerrar!");
    exit(EXIT_SUCCESS);
}
