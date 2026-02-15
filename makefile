CC = gcc
FLAGS = -Wall -g -pthread
LIBS = -lssl -lcrypto

# Executáveis
PROGS = blockchain transactiongenerator

# Objetos para blockchain (Controller e Pow agora)
BLOCKCHAIN_OBJS = Controller.o Pow.o

# Objetos para transactiongenerator
TRANSACTIONGEN_OBJS = TransactionGenerator.o

# Regra principal
all: $(PROGS)

# Regra para o executável blockchain
blockchain: $(BLOCKCHAIN_OBJS)
	$(CC) $(FLAGS) $^ -o $@ $(LIBS)

# Regra para o executável transactiongenerator
transactiongenerator: $(TRANSACTIONGEN_OBJS)
	$(CC) $(FLAGS) $^ -o $@

# Dependências específicas
Controller.o: Controller.c Miner.h Validator.h Statistics.h Writer.h Pow.h
Pow.o: Pow.c Pow.h
TransactionGenerator.o: TransactionGenerator.c

# Regra para limpar arquivos compilados
clean:
	rm -f *.o *~ $(PROGS)

# Regra genérica de compilação de .c para .o
%.o: %.c
	$(CC) $(FLAGS) -c $< -o $@
