#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>

// Define o caractere para ativar o sensor
#define SENSOR_ABS 'a'
#define SENSOR_AIRBAG 'b'
#define CHAMAR_GRAFICO 'p'

// Deadlines em microssegundos
#define DEADLINE_ABS 100000
#define DEADLINE_AIRBAG 100000

// Configurações globais
#define NUM_AMOSTRAS 200
#define JANELA_ATIVACOES 10
#define M_ATIVACOES 10

// Variáveis globais
volatile bool sensor_ativado[2] = {false};
pthread_mutex_t sensor_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sensor_cond = PTHREAD_COND_INITIALIZER;

// Variáveis para medir tempos
uint64_t WCET[2] = {0};
uint64_t WCRT[2] = {0};
uint64_t HWM[2] = {0};
uint64_t LWM[2] = {UINT64_MAX, UINT64_MAX};

long long int cont_ativacao[2] = {0}; 
long long int l_deadline[2] = {0};

volatile uint64_t tempo_int[2] = {0};

// Estrutura para um nó da lista ligada
typedef struct No {
    uint64_t valor;
    struct No* proximo;
} No;

// Ponteiros para listas ligadas que armazenam os tempos coletados
No* tempo_execucao_ABS = NULL;
No* tempo_resposta_ABS = NULL;
No* tempo_interferencia_ABS = NULL;
No* periodo_ocorrencia_ABS = NULL;

No* tempo_execucao_Airbag = NULL;
No* tempo_resposta_Airbag = NULL;
No* tempo_interferencia_Airbag = NULL;
No* periodo_ocorrencia_Airbag = NULL;

// Função para adicionar valores às listas ligadas
void adiciona_lista(No** head, uint64_t valor) {
    No* novo_No = (No*)malloc(sizeof(No)); // Aloca memória para o novo nó
    if (!novo_No) {
        perror("Erro ao alocar memória"); // Em caso de erro, encerra o programa
        exit(EXIT_FAILURE);
    }
    novo_No->valor = valor; // Atribui o valor ao nó
    novo_No->proximo = NULL; // Inicializa o ponteiro para o próximo nó como NULL

    if (*head == NULL) {
        *head = novo_No; // Se a lista estiver vazia, o novo nó se torna o primeiro
    } else {
        No* atual = *head;
        while (atual->proximo != NULL) {
            atual = atual->proximo; // Navega até o final da lista
        }
        atual->proximo = novo_No; // Adiciona o novo nó ao final da lista
    }
}

// Função para liberar memória das listas ligadas
void libera_lista(No* head) {
    No* atual = head;
    while (atual != NULL) {
        No* temp = atual;
        atual = atual->proximo; // Avança para o próximo nó
        free(temp); // Libera o nó atual
    }
}

// Função para exportar os dados das listas para um arquivo CSV
void exportar_para_csv(No* execucao, No* resposta, No* interferencia, No* periodo, const char* nome_arquivo) {
    FILE* arquivo = fopen(nome_arquivo, "w"); // Abre o arquivo em modo de escrita
    if (!arquivo) { // Em caso de erro, encerra o programa
        perror("Erro ao abrir o arquivo para escrita");
        exit(EXIT_FAILURE); 
    }

    // Escreve o cabeçalho no arquivo CSV
    fprintf(arquivo, "Tempo Execucao(us),Tempo Resposta(us),Tempo Interferencia(us),Periodo Ocorrencia(us)\n");

    // Itera pelas listas e escreve os dados no arquivo
    No* atual_execucao = execucao;
    No* atual_resposta = resposta;
    No* atual_interferencia = interferencia;
    No* atual_periodo = periodo;

    while (atual_execucao && atual_resposta && atual_interferencia && atual_periodo) {
        fprintf(arquivo, "%lu,%lu,%lu,%lu\n",
                atual_execucao->valor,
                atual_resposta->valor,
                atual_interferencia->valor,
                atual_periodo->valor);

        // Avança para os próximos nós das listas
        atual_execucao = atual_execucao->proximo;
        atual_resposta = atual_resposta->proximo;
        atual_interferencia = atual_interferencia->proximo;
        atual_periodo = atual_periodo->proximo;
    }

    fclose(arquivo); // Fecha o arquivo
    printf("Dados exportados para %s\n", nome_arquivo);
}

// Função que chama a exportação dos dados para os arquivos CSV
void chamar_grafico(){
    exportar_para_csv(tempo_execucao_ABS, tempo_resposta_ABS, tempo_interferencia_ABS, periodo_ocorrencia_ABS, "dados_abs.csv");
    exportar_para_csv(tempo_execucao_Airbag, tempo_resposta_Airbag, tempo_interferencia_Airbag, periodo_ocorrencia_Airbag, "dados_airbag.csv");
}

// Função para inicializar os arquivos CSV
void inicializar_csv() {
    FILE *file_abs = fopen("dados_abs.csv", "w");
    FILE *file_airbag = fopen("dados_airbag.csv", "w");

    // Verifica se os arquivos foram abertos com sucesso
    if (file_abs && file_airbag) {
        // Escrever cabeçalhos nos arquivos
        fprintf(file_abs, "TempoExecucao,TempoResposta,TempoInterferencia,PeriodoOcorrencia\n");
        fprintf(file_airbag, "TempoExecucao,TempoResposta,TempoInterferencia,PeriodoOcorrencia\n");
        fclose(file_abs);
        fclose(file_airbag);
    } else {
        perror("Erro ao criar arquivos CSV"); // Em caso de erro, exibe mensagem e encerra o programa
        exit(1);
    }
}

// Função para gravar os dados no CSV
void gravar_csv(const char *filename, uint64_t execucao, uint64_t resposta, uint64_t interferencia, uint64_t periodo) {
    FILE *file = fopen(filename, "a");
    if (file) {
        // Grava os dados no arquivo
        fprintf(file, "%lu,%lu,%lu,%lu\n", execucao, resposta, interferencia, periodo);
        fclose(file);
    } else {
        perror("Erro ao abrir arquivo CSV");
    }
}

// Função para obter o tempo em microssegundos
uint64_t obter_tempo() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

// Função para capturar teclas pressionadas (simulação de sensores)
int capturar_tecla() {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    return ch;
}

// Função para lidar com deadlines ultrapassados
void alta_deadline(char* nome_acao, uint64_t tempo_execucao) {
    if (strcmp(nome_acao, "da injeção eletrônica") == 0){
        printf("Alerta: Tempo de resposta da %s excedeu o deadline (%lu us)\n", nome_acao, tempo_execucao);
    } else if (strcmp(nome_acao, "da temperatura do motor") == 0 || 
               strcmp(nome_acao, "do ABS") == 0 || 
               strcmp(nome_acao, "do airbag") == 0) {
        uint64_t tempo_execucao_micro = (tempo_execucao / 1000);
        printf("Alerta: Tempo de resposta da %s excedeu o deadline (%lu ms)\n", nome_acao, tempo_execucao_micro);
    } else if (strcmp(nome_acao, "do cinto de segurança") == 0){
        uint64_t tempo_execucao_segundos = (tempo_execucao / 1000000);
        printf("Alerta: Tempo de resposta da %s excedeu o deadline (%lu s)\n", nome_acao, tempo_execucao_segundos);
    }
    usleep(1000000);
}

// Função para exibir o tempo de resposta
void mostrar_tempo(char* nome_acao, uint64_t tempo_execucao, uint64_t tempo_resposta,
                   uint64_t tempo_interferencia, uint64_t periodo_ocorrencia) {
    if (tempo_execucao < 1000) {
        printf("\n\n%s ativado\nTempo de execução para a ativação do %s: %lu us\n", nome_acao, nome_acao, tempo_execucao);
    } else if (tempo_execucao < 1000000) {
        printf("%s ativado\nTempo de execução para %s: %.2f ms\n", nome_acao, nome_acao, tempo_execucao / 1000.0);
    } else {
        printf("%s colocado\nTempo de execução para a verificação do %s: %.2f s\n", nome_acao, nome_acao, tempo_execucao / 1000000.0);
    }

    if (tempo_resposta < 1000) {
        printf("Tempo de resposta para a ativação do %s: %lu us\n", nome_acao, tempo_resposta);
    } else if (tempo_resposta < 1000000) {
        printf("Tempo de resposta para %s: %.2f ms\n", nome_acao, tempo_resposta / 1000.0);
    } else {
        printf("Tempo de resposta para a verificação do %s: %.2f s\n\n", nome_acao, tempo_resposta / 1000000.0);
    }

     // Exibe o tempo de interferência
    if (tempo_interferencia < 1000) {
        printf("Tempo de interferência para a ativação do %s: %lu us\n", nome_acao, tempo_interferencia);
    } else if (tempo_interferencia < 1000000) {
        printf("Tempo de interferência para %s: %.2f ms\n", nome_acao, tempo_interferencia / 1000.0);
    } else {
        printf("Tempo de interferência para a verificação do %s: %.2f s\n", nome_acao, tempo_interferencia / 1000000.0);
    }

    if (periodo_ocorrencia < 1000) {
        printf("ccc para a ativação do %s: %lu us\n", nome_acao, periodo_ocorrencia);
    } else if (periodo_ocorrencia < 1000000) {
        printf("Periodo de ocorrência para %s: %.2f ms\n", nome_acao, periodo_ocorrencia / 1000.0);
    } else {
        printf("Periodo de ocorrência para a verificação do %s: %.2f s\n", nome_acao, periodo_ocorrencia / 1000000.0);
    }

}

// Função que controla o ABS
void* tarefa_ABS(void* arg) {
    uint64_t tempo_inicio, tempo_fim_WCET, tempo_fim_HWM, tempo_execucao, tempo_resposta,
             tempo_interferencia, tempo_anterior = 0, periodo_ocorrencia = 0;

    while (1) {
        pthread_mutex_lock(&sensor_mutex); // Bloqueia o mutex para acessar a variável compartilhada

        // Aguarda o sinal de ativação do sensor ABS ou um timeout de 1 segundo
        while (!sensor_ativado[0]) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts); // Obtém o tempo atual
            ts.tv_sec += 1; // Timeout de 1 segundo
            pthread_cond_timedwait(&sensor_cond, &sensor_mutex, &ts); // Espera o sinal ou timeout
        }

        uint64_t tempo_inicio = obter_tempo(); // Registra o tempo inicial ao ativar o sensor
        cont_ativacao[0]++; // Incrementa o contador de ativações do ABS
        tempo_interferencia = tempo_inicio - tempo_int[0]; // Calcula o tempo de interferência desde o último acionamento

        if (tempo_anterior != 0) { // Calcula o período de ocorrência entre ativações consecutivas
            periodo_ocorrencia = tempo_inicio - tempo_anterior;
        }
        tempo_anterior = tempo_inicio; // Atualiza o tempo da última ativação

        sensor_ativado[0] = false; // Reseta o estado do sensor
        pthread_mutex_unlock(&sensor_mutex);

        usleep(1); // Simula tempo de aquisição
        usleep(10); // Simula tempo de envio de dados
        usleep(5); // Simula tempo de controle do atuador
        usleep(10); // Simula tempo de envio de dados

        tempo_fim_WCET = obter_tempo(); // Captura o tempo após a execução
        tempo_execucao = tempo_fim_WCET - tempo_inicio; // Calcula o tempo total de execução

        // Atualizar WCET e verificar deadline
        if (tempo_execucao > WCET[0]){ 
            WCET[0] = tempo_execucao;
        }

        tempo_fim_HWM = obter_tempo(); // Captura o tempo após a resposta
        tempo_resposta = tempo_fim_HWM - tempo_inicio; // Calcula o tempo total de resposta

        if (tempo_resposta > HWM[0]) { // Verifica o maior tempo de resposta
            HWM[0] = tempo_resposta;
        }

        if (tempo_resposta < LWM[0]) { // Verifica o menor tempo de resposta
            LWM[0] = tempo_resposta;
        }

        WCRT[0] = HWM[0] + (HWM[0] - LWM[0]); // Calculo do WCRT do ABS


        if (HWM[0] > DEADLINE_ABS) { // Verifica se o tempo excedeu o deadline
            alta_deadline("do ABS", tempo_resposta);
            l_deadline[1]++;
        }

        mostrar_tempo("ABS", tempo_execucao, tempo_resposta, tempo_interferencia, periodo_ocorrencia);

        // Adiciona os tempos à lista ligada correspondente
        adiciona_lista(&tempo_execucao_ABS, tempo_execucao);
        adiciona_lista(&tempo_resposta_ABS, tempo_resposta);
        adiciona_lista(&tempo_interferencia_ABS, tempo_interferencia);
        adiciona_lista(&periodo_ocorrencia_ABS, periodo_ocorrencia);

        // Grava os tempos no arquivo CSV
        gravar_csv("dados_abs.csv", tempo_execucao, tempo_resposta, tempo_interferencia, periodo_ocorrencia);

        usleep(100000);        
    }
    return NULL;
}


void* tarefa_Airbag(void* arg) {
    uint64_t tempo_inicio, tempo_fim_WCET, tempo_fim_HWM, tempo_execucao, tempo_resposta,
             tempo_interferencia, tempo_anterior = 0, periodo_ocorrencia = 0;
    
    while (1) {
        pthread_mutex_lock(&sensor_mutex);

        // Espera o sinal ou timeout
        while (!sensor_ativado[1]) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += 1; // Timeout de 1 segundo
            pthread_cond_timedwait(&sensor_cond, &sensor_mutex, &ts);
        }
        uint64_t tempo_inicio = obter_tempo();
        cont_ativacao[1]++;
        tempo_interferencia = tempo_inicio - tempo_int[1];

        if (tempo_anterior != 0) {
            periodo_ocorrencia = tempo_inicio - tempo_anterior;
        }
        tempo_anterior = tempo_inicio; // Atualiza o tempo da última ativação

        sensor_ativado[1] = false; // Reseta o estado do sensor
        pthread_mutex_unlock(&sensor_mutex);

        usleep(1); // Simula tempo de aquisição
        usleep(10); // Simula tempo de envio de dados
        usleep(5); // Simula tempo de controle do atuador
        usleep(10); // Simula tempo de envio de dados

        tempo_fim_WCET = obter_tempo(); // Captura o tempo após a execução
        tempo_execucao = tempo_fim_WCET - tempo_inicio; // Calcula o tempo total de execução

        // Atualizar WCET e verificar deadline
        if (tempo_execucao > WCET[1]){ 
            WCET[1] = tempo_execucao;
        }

        tempo_fim_HWM = obter_tempo(); // Captura o tempo após a resposta
        tempo_resposta = tempo_fim_HWM - tempo_inicio; // Calcula o tempo total de resposta

        if (tempo_resposta > HWM[1]) { // Verifica o maior tempo de resposta
            HWM[1] = tempo_resposta;
        }

        if (tempo_resposta < LWM[1]) { // Verifica o menor tempo de resposta
            LWM[1] = tempo_resposta;
        }

        WCRT[1] = HWM[1] + (HWM[1] - LWM[1]) + HWM[0] ; // Calculo do WCRT do Airbag

        if (HWM[1] > DEADLINE_ABS) { // Verifica se o tempo excedeu o deadline
            alta_deadline("do Airbag", tempo_resposta);
            l_deadline[1]++;
        }

        mostrar_tempo("Airbag", tempo_execucao, tempo_resposta, tempo_interferencia, periodo_ocorrencia);

        adiciona_lista(&tempo_execucao_Airbag, tempo_execucao);
        adiciona_lista(&tempo_resposta_Airbag, tempo_resposta);
        adiciona_lista(&tempo_interferencia_Airbag, tempo_interferencia);
        adiciona_lista(&periodo_ocorrencia_Airbag, periodo_ocorrencia);

        gravar_csv("dados_airbag.csv", tempo_execucao, tempo_resposta, tempo_interferencia, periodo_ocorrencia);

        usleep(100000);        
    }
    return NULL;
}

void *monitorar_sensores(void *arg) {
    while (1) { // Laço infinito que monitora continuamente a entrada do teclado
        int tecla = capturar_tecla(); // Captura a tecla pressionada pelo usuário
        if (tecla != EOF) { // Verifica se uma tecla foi pressionada (EOF indica que nada foi capturado)
            pthread_mutex_lock(&sensor_mutex); // Bloqueia o mutex para garantir acesso seguro às variáveis compartilhadas
            switch (tecla) { // Verifica qual tecla foi pressionada
                case SENSOR_ABS:
                    tempo_int[0] = obter_tempo();
                    sensor_ativado[0] = true;
                    break;
                case SENSOR_AIRBAG:
                    tempo_int[1] = obter_tempo();
                    sensor_ativado[1] = true;
                    break;
                case CHAMAR_GRAFICO:
                    chamar_grafico();
                    break;
            }
            pthread_mutex_unlock(&sensor_mutex);
        }
    }
    return NULL;
}

// Thread: Atualizar Display
void* atualizar_display(void* arg) {
    while (1) { // Exibe informações relevantes no display a cada 1 segundo
        
        // Exibe o WCET e WCRT para ABS
        printf("\n\nABS:\n");
        printf("  WCET = %lu us\n", WCET[0]);
        printf("  WCRT = %lu us\n", WCRT[0]);
        printf("  Número de ativações: %llu\n", cont_ativacao[0]);
        printf("  Estado: %s\n", sensor_ativado[0] ? "Ativo" : "Inativo");
        printf("  Deadlines excedidas (skip): %lld\n", l_deadline[0]);
        printf("  HWM: %.2f%%\n", (((double)(cont_ativacao[0] - l_deadline[0]) / cont_ativacao[0]) * 100.0));

        // Exibe o WCET e WCRT para Airbag
        printf("\nAirbag:\n");
        printf("  WCET = %lu us\n", WCET[1]);
        printf("  WCRT = %lu us\n", WCRT[1]);
        printf("  Número de ativações: %llu\n", cont_ativacao[1]);
        printf("  Estado: %s\n", sensor_ativado[1] ? "Ativo" : "Inativo");
        printf("  Deadlines excedidas (skip): %lld\n", l_deadline[1]);
        printf("  HWM: %.2f%%\n", (((double)(cont_ativacao[1] - l_deadline[1]) / cont_ativacao[1]) * 100.0));

        sleep(1); // Atualização a cada 1 segundo
    }
}

// Função principal
int main() {

    inicializar_csv(); // Inicializa os arquivos CSV para armazenar os dados

    pthread_t threads[4]; // Declaração das threads
    pthread_attr_t attr; // Declaração dos atributos das threads

    // Inicializar atributos das threads
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);

    // Configure a prioridade para cada thread
    struct sched_param param;  // Estrutura para configurar prioridades

    // Criar threads
    param.sched_priority = 50;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&threads[0], &attr, tarefa_ABS, NULL);

    param.sched_priority = 40;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&threads[1], &attr, tarefa_Airbag, NULL);

    param.sched_priority = 5;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&threads[2], &attr, atualizar_display, NULL);

    param.sched_priority = 15;
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&threads[3], &attr, monitorar_sensores, NULL);

    pthread_attr_destroy(&attr); // Libera os atributos das threads após uso

    // Aguarda a finalização das threads
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    pthread_join(threads[2], NULL);
    pthread_join(threads[3], NULL);

    return 0;
}
