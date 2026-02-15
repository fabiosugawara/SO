//Manuel José Lopes Francisco – 2022217194
//Fabio Sugawara da Silva – 2020147048
#include <stdio.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#define WRITE_FILE "log.txt"
#include <errno.h> 
// Evitar múltiplas inclusões
#ifndef FILE_WRITER_H  
#define FILE_WRITER_H

void reset_log_file() {
    FILE *file = fopen(WRITE_FILE, "w");
    if (file) fclose(file);
}

void write_file_screen(sem_t *sem, const char *data) {
    if (!sem) {
        printf("Semáforo inválido!\n");
        return;
    }
// Obter timestamp atual
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[9];  // HH:MM:SS + '\0'
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    // Criar string final com timestamp + data
    char dataNtime[1024];  // tamanho arbitrário, ajusta se necessário
    snprintf(dataNtime, sizeof(dataNtime), "[%s] %s", timestamp, data);

    // Imprimir no ecrã
    printf("%s\n", dataNtime);


    if (sem_wait(sem) == -1) {
        perror("Erro ao adquirir o semáforo");
        return;
    }
    
    FILE *f = fopen(WRITE_FILE, "a");
    if (!f) {
        perror("Erro ao abrir arquivo");
        sem_post(sem);  // Libera semáforo mesmo em caso de erro
        return;
    }
    
    fprintf(f, "%s\n", dataNtime);
    fclose(f);
    
    if (sem_post(sem) == -1) {
        perror("Erro ao liberar o semáforo");
    }
}

// Macro para escrita sincronizada no log e tela com flag para indicar valores extras
// Versão final da macro
#define PRINT_SYNC(sem, fmt, ...) do { \
    char buf[1024]; \
    if (strchr(fmt, '%') != NULL) { \
        snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
        write_file_screen(sem, buf); \
    } else { \
        write_file_screen(sem, fmt); \
    } \
} while(0)







#endif