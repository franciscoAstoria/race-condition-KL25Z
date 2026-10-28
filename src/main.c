/* Demonstracao didatica para Zephyr 4.2 / FRDM-KL25Z. */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <stdbool.h>

#define RODADAS 100
#define PILHA 768
#define PRIORIDADE 5

struct sensor {
    struct k_sem iniciar;
    struct k_sem gravar;
    bool usar_mutex;
};

static struct sensor sensores[2];
static struct k_thread threads[2];
K_THREAD_STACK_DEFINE(pilha_a, PILHA);
K_THREAD_STACK_DEFINE(pilha_b, PILHA);
K_SEM_DEFINE(leituras_prontas, 0, 2);
K_SEM_DEFINE(atualizacoes_prontas, 0, 2);
K_MUTEX_DEFINE(mutex_total);

/* Cada acesso e atomico; a sequencia ler-somar-gravar NAO e. */
static atomic_t total_pecas = ATOMIC_INIT(0);

static void tarefa_sensor(void *p1, void *p2, void *p3)
{
    struct sensor *sensor = p1;
    (void)p2;
    (void)p3;

    while (true) {
        k_sem_take(&sensor->iniciar, K_FOREVER);

        if (sensor->usar_mutex) {
            k_mutex_lock(&mutex_total, K_FOREVER);
            atomic_val_t copia = atomic_get(&total_pecas);

            /* Aumenta a oportunidade de troca de tarefa, para demonstracao.
             * Em producao, manter a secao critica curta. */
            k_msleep(1);
            atomic_set(&total_pecas, copia + 1);
            k_mutex_unlock(&mutex_total);
        } else {
            atomic_val_t copia = atomic_get(&total_pecas);

            /* Instrumentacao: as duas tarefas devem ler antes de gravar.
             * Estes semaforos NAO dao exclusividade ao contador. */
            k_sem_give(&leituras_prontas);
            k_sem_take(&sensor->gravar, K_FOREVER);
            atomic_set(&total_pecas, copia + 1);
        }

        k_sem_give(&atualizacoes_prontas);
    }
}

static void executar_experimento(bool usar_mutex)
{
    /* As tarefas estao bloqueadas em iniciar, sem atualizacoes pendentes. */
    atomic_set(&total_pecas, 0);
    sensores[0].usar_mutex = usar_mutex;
    sensores[1].usar_mutex = usar_mutex;

    printk("\n=== %s ===\n", usar_mutex ? "COM MUTEX" : "SEM MUTEX");

    for (int rodada = 1; rodada <= RODADAS; rodada++) {
        k_sem_give(&sensores[0].iniciar);
        k_sem_give(&sensores[1].iniciar);

        if (!usar_mutex) {
            /* Aguarda ambas as leituras; so entao libera as escritas. */
            k_sem_take(&leituras_prontas, K_FOREVER);
            k_sem_take(&leituras_prontas, K_FOREVER);
            k_sem_give(&sensores[0].gravar);
            k_sem_give(&sensores[1].gravar);
        }

        k_sem_take(&atualizacoes_prontas, K_FOREVER);
        k_sem_take(&atualizacoes_prontas, K_FOREVER);

        if (rodada <= 3) {
            printk("Rodada %d: esperado=%d, registrado=%d\n",
                   rodada, rodada * 2, (int)atomic_get(&total_pecas));
        }
    }

    int registrado = (int)atomic_get(&total_pecas);
    printk("Pecas simuladas: %d\n", 2 * RODADAS);
    printk("Pecas registradas: %d\n", registrado);
    printk("Pecas perdidas: %d\n", 2 * RODADAS - registrado);
    printk("Resultado: %s\n", registrado == 2 * RODADAS
           ? "CONTAGEM CORRETA" : "ERRO DE CONTAGEM (race condition)");
}

int main(void)
{
    for (int i = 0; i < 2; i++) {
        k_sem_init(&sensores[i].iniciar, 0, 1);
        k_sem_init(&sensores[i].gravar, 0, 1);
    }

    k_thread_create(&threads[0], pilha_a, K_THREAD_STACK_SIZEOF(pilha_a),
                    tarefa_sensor, &sensores[0], NULL, NULL,
                    PRIORIDADE, 0, K_NO_WAIT);
    k_thread_create(&threads[1], pilha_b, K_THREAD_STACK_SIZEOF(pilha_b),
                    tarefa_sensor, &sensores[1], NULL, NULL,
                    PRIORIDADE, 0, K_NO_WAIT);

    k_msleep(1500); /* Tempo para abrir o monitor serial apos o reset. */
    printk("\nContagem de pecas em duas esteiras (sensores simulados)\n");
    executar_experimento(false);
    executar_experimento(true);
    printk("\nFim. Pressione RESET para repetir.\n");
    return 0;
}
