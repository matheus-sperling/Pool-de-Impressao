#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <iomanip>
#include <sstream>
#include <random>
#include <memory> // Para std::unique_ptr

// Mutex global para sincronizar o acesso ao std::cout
std::mutex cout_mutex;

// Estrutura que define um pedido de impressão
struct Pedido
{
    int id;                                                 // Identificador único do pedido
    std::string nome_documento;                             // Nome do documento a ser impresso
    int num_paginas;                                        // Número de páginas do documento
    int prioridade;                                         // Prioridade do pedido (1 a 5)
    int id_processo;                                        // Identificador do processo que gerou o pedido
    std::chrono::system_clock::time_point hora_solicitacao; // Horário da solicitação

    // Sobrecarga do operador < para definir a ordem de prioridade na fila
    bool operator<(const Pedido &outro) const
    {
        // Maior prioridade (valor maior) tem precedência
        if (prioridade == outro.prioridade)
        {
            // Se as prioridades forem iguais, o pedido mais antigo tem precedência
            return hora_solicitacao > outro.hora_solicitacao;
        }
        return prioridade < outro.prioridade;
    }
};

// Estrutura que armazena os dados de um pedido processado
struct RegistroImpressao
{
    std::string nome_documento;                             // Nome do documento
    int num_paginas;                                        // Número de páginas
    int id_processo;                                        // Identificador do processo solicitante
    int id_impressora;                                      // Identificador da impressora utilizada
    std::chrono::system_clock::time_point hora_solicitacao; // Horário da solicitação
    std::chrono::system_clock::time_point hora_inicio;      // Horário de início da impressão
    std::chrono::milliseconds tempo_total;                  // Tempo total de impressão
    int prioridade;                                         // Prioridade do pedido
};

// Contador global de processos ativos
std::atomic<int> processos_ativos;

// Classe que gerencia o spool de impressão
class Spool
{
public:
    // Construtor que define a capacidade máxima do buffer
    Spool(int capacidade_buffer)
        : capacidade(capacidade_buffer), encerrar(false) {}

    // Função para adicionar um pedido ao buffer
    void add_pedido(const Pedido &pedido)
    {
        std::unique_lock<std::mutex> lock(mutex_buffer);
        // Espera até que haja espaço no buffer ou que o sistema esteja encerrando
        cond_var_buffer.wait(lock, [this]()
                             { return buffer.size() < capacidade || encerrar.load(); });
        if (encerrar.load())
            return;
        buffer.push(pedido); // Adiciona o pedido à fila de prioridade

        // Impressão sincronizada da mensagem de recebimento do pedido
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "-----------------------------------------\n";
            std::cout << "Spool recebeu pedido " << pedido.nome_documento << " com "
                      << pedido.num_paginas << " paginas, prioridade " << pedido.prioridade << ".\n";
            std::cout << "-----------------------------------------\n\n";
        }

        cond_var_buffer.notify_one(); // Notifica que um novo pedido foi adicionado
    }

    // Função para obter um pedido do buffer
    bool get_pedido(Pedido &pedido)
    {
        std::unique_lock<std::mutex> lock(mutex_buffer);
        // Espera até que haja um pedido na fila ou que todos os processos tenham terminado
        cond_var_buffer.wait(lock, [this]()
                             { return !buffer.empty() || (processos_ativos.load() == 0); });
        if (buffer.empty() && processos_ativos.load() == 0)
            return false; // Retorna false se não há mais pedidos e processos ativos

        pedido = buffer.top();        // Obtém o pedido de maior prioridade
        buffer.pop();                 // Remove o pedido da fila
        cond_var_buffer.notify_one(); // Notifica que um pedido foi removido
        return true;
    }

    // Função que simula o trabalho da impressora
    void worker(int id_impressora, std::vector<RegistroImpressao> &registros,
                std::unordered_map<int, int> &paginas_por_impressora, std::mutex &registro_mutex,
                int tempo_por_pagina_ms)
    {
        while (!encerrar.load())
        {
            Pedido pedido;
            if (get_pedido(pedido))
            {
                // Mensagem de início do processamento
                {
                    std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                    std::cout << "-----------------------------------------\n";
                    std::cout << "Impressora " << id_impressora << " iniciou processamento de "
                              << pedido.nome_documento << " com " << pedido.num_paginas
                              << " paginas, prioridade " << pedido.prioridade << ".\n";
                    std::cout << "-----------------------------------------\n\n";
                }

                auto inicio = std::chrono::system_clock::now(); // Horário de início da impressão
                // Simula o tempo de impressão
                std::this_thread::sleep_for(std::chrono::milliseconds(tempo_por_pagina_ms * pedido.num_paginas));
                auto fim = std::chrono::system_clock::now(); // Horário de fim da impressão

                // Calcula o tempo total de impressão
                std::chrono::milliseconds duracao = std::chrono::duration_cast<std::chrono::milliseconds>(fim - inicio);

                // Registro da impressão
                {
                    std::lock_guard<std::mutex> lock(registro_mutex);
                    RegistroImpressao registro;
                    registro.nome_documento = pedido.nome_documento;
                    registro.num_paginas = pedido.num_paginas;
                    registro.id_processo = pedido.id_processo;
                    registro.id_impressora = id_impressora;
                    registro.hora_solicitacao = pedido.hora_solicitacao;
                    registro.hora_inicio = inicio;
                    registro.tempo_total = duracao;
                    registro.prioridade = pedido.prioridade;
                    registros.push_back(registro);
                    paginas_por_impressora[id_impressora] += pedido.num_paginas;
                }

                // Mensagem de conclusão do processamento
                {
                    std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                    std::cout << "-----------------------------------------\n";
                    std::cout << "Impressora " << id_impressora << " concluiu processamento de "
                              << pedido.nome_documento << ".\n";
                    std::cout << "-----------------------------------------\n\n";
                }
            }
        }
    }

    // Função que espera até que o sistema esteja inativo por 'timeout' segundos
    void wait_until_finished(std::chrono::seconds timeout)
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Espera 1 segundo
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                if (buffer.empty() && processos_ativos.load() == 0)
                {
                    // Inicia a contagem regressiva de 'timeout' segundos
                    for (int i = timeout.count(); i > 0; --i)
                    {
                        std::cout << "-----------------------------------------\n";
                        std::cout << "Tempo restante para relatorio final: " << i << " segundos.\n";
                        std::cout << "-----------------------------------------\n\n";
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        // Verifica novamente se há novos pedidos
                        if (!buffer.empty() || processos_ativos.load() > 0)
                        {
                            break; // Sai da contagem se houver novos pedidos
                        }
                    }
                    break; // Sai do loop principal após a contagem
                }
            }
        }
        encerrar.store(true);         // Sinaliza para as impressoras encerrarem
        cond_var_buffer.notify_all(); // Notifica todas as impressoras
    }

    // Função para sinalizar o encerramento do spool
    void encerrar_spool()
    {
        encerrar.store(true);
        cond_var_buffer.notify_all();
    }

private:
    std::priority_queue<Pedido> buffer;      // Fila de prioridade para os pedidos
    std::mutex mutex_buffer;                 // Mutex para proteger o acesso ao buffer
    std::condition_variable cond_var_buffer; // Variável de condição para sincronização
    int capacidade;                          // Capacidade máxima do buffer
    std::atomic<bool> encerrar;              // Flag para indicar o encerramento do sistema
};

// Classe que representa uma Impressora
class Impressora
{
public:
    // Construtor que inicializa a impressora com seu ID, referência ao spool, registros, contador de páginas e tempo por página
    Impressora(int id, Spool &spool, std::vector<RegistroImpressao> &registros,
               std::unordered_map<int, int> &paginas_por_impressora, std::mutex &registro_mutex,
               int tempo_por_pagina_ms)
        : id_impressora(id), spool_ref(spool), registros_ref(registros),
          paginas_por_impressora_ref(paginas_por_impressora), registro_mutex_ref(registro_mutex),
          tempo_por_pagina_ms(tempo_por_pagina_ms), encerrar(false) {}

    // Deleta o construtor de cópia e o operador de atribuição para evitar cópias
    Impressora(const Impressora &) = delete;
    Impressora &operator=(const Impressora &) = delete;

    // Função que inicia a thread da impressora
    void start()
    {
        thread_impressora = std::thread(&Impressora::run, this);
    }

    // Função que espera a thread da impressora terminar
    void join()
    {
        if (thread_impressora.joinable())
            thread_impressora.join();
    }

    // Função para sinalizar o encerramento da impressora
    void stop()
    {
        encerrar.store(true);
    }

private:
    int id_impressora;                                        // Identificador da impressora
    Spool &spool_ref;                                         // Referência ao spool de impressão
    std::vector<RegistroImpressao> &registros_ref;            // Referência aos registros de impressão
    std::unordered_map<int, int> &paginas_por_impressora_ref; // Referência ao contador de páginas por impressora
    std::mutex &registro_mutex_ref;                           // Referência ao mutex para registrar impressões
    int tempo_por_pagina_ms;                                  // Tempo de impressão por página
    std::atomic<bool> encerrar;                               // Flag para encerrar a impressora
    std::thread thread_impressora;                            // Thread da impressora

    // Função que simula o funcionamento da impressora
    void run()
    {
        while (!encerrar.load())
        {
            Pedido pedido;
            if (spool_ref.get_pedido(pedido))
            {
                // Mensagem de início do processamento
                {
                    std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                    std::cout << "-----------------------------------------\n";
                    std::cout << "Impressora " << id_impressora << " iniciou processamento de "
                              << pedido.nome_documento << " com " << pedido.num_paginas
                              << " paginas, prioridade " << pedido.prioridade << ".\n";
                    std::cout << "-----------------------------------------\n\n";
                }

                auto inicio = std::chrono::system_clock::now(); // Horário de início da impressão
                // Simula o tempo de impressão
                std::this_thread::sleep_for(std::chrono::milliseconds(tempo_por_pagina_ms * pedido.num_paginas));
                auto fim = std::chrono::system_clock::now(); // Horário de fim da impressão

                // Calcula o tempo total de impressão
                std::chrono::milliseconds duracao = std::chrono::duration_cast<std::chrono::milliseconds>(fim - inicio);

                // Registro da impressão
                {
                    std::lock_guard<std::mutex> lock(registro_mutex_ref);
                    RegistroImpressao registro;
                    registro.nome_documento = pedido.nome_documento;
                    registro.num_paginas = pedido.num_paginas;
                    registro.id_processo = pedido.id_processo;
                    registro.id_impressora = id_impressora;
                    registro.hora_solicitacao = pedido.hora_solicitacao;
                    registro.hora_inicio = inicio;
                    registro.tempo_total = duracao;
                    registro.prioridade = pedido.prioridade;
                    registros_ref.push_back(registro);
                    paginas_por_impressora_ref[id_impressora] += pedido.num_paginas;
                }

                // Mensagem de conclusão do processamento
                {
                    std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                    std::cout << "-----------------------------------------\n";
                    std::cout << "Impressora " << id_impressora << " concluiu processamento de "
                              << pedido.nome_documento << ".\n";
                    std::cout << "-----------------------------------------\n\n";
                }
            }
        }
    }
};

// Função para coletar os dados de entrada do usuário
void coletar_dados(int &num_processos, int &num_impressoras, int &capacidade_buffer, int &tempo_por_pagina_ms)
{
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "Bem-vindo ao Simulador de Pool de Impressao!\n\n";
    }
    // Coleta a quantidade de processos
    while (true)
    {
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Quantidade de processos (minimo 1): ";
        }
        std::cin >> num_processos;
        if (num_processos > 0)
            break;
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Valor invalido.\n\n";
        }
    }

    // Coleta a quantidade de impressoras
    while (true)
    {
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Quantidade de impressoras (minimo 1): ";
        }
        std::cin >> num_impressoras;
        if (num_impressoras > 0)
            break;
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Valor invalido.\n\n";
        }
    }

    // Coleta a capacidade máxima do buffer
    while (true)
    {
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Capacidade maxima do buffer (minimo 1): ";
        }
        std::cin >> capacidade_buffer;
        if (capacidade_buffer > 0)
            break;
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Valor invalido.\n\n";
        }
    }

    // Coleta o tempo de impressão por página
    while (true)
    {
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Tempo de impressao por pagina (ms, minimo 10): ";
        }
        std::cin >> tempo_por_pagina_ms;
        if (tempo_por_pagina_ms >= 10)
            break;
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Valor invalido.\n\n";
        }
    }
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "\n";
    }
}

// Classe que representa um processo que gera pedidos de impressão
class Processo
{
public:
    // Construtor que inicializa o processo com seu ID, número máximo de pedidos e referência ao spool
    Processo(int pid, int max_pedidos, Spool &spool)
        : id(pid), max_pedidos(max_pedidos), spool_ref(spool), pedidos_enviados(0) {}

    // Deleta o construtor de cópia e o operador de atribuição para evitar cópias
    Processo(const Processo &) = delete;
    Processo &operator=(const Processo &) = delete;

    // Função para iniciar a thread do processo
    void start()
    {
        thread_process = std::thread(&Processo::run, this);
    }

    // Função para esperar a thread do processo terminar
    void join()
    {
        if (thread_process.joinable())
            thread_process.join();
    }

private:
    int id;                     // Identificador do processo
    int max_pedidos;            // Número máximo de pedidos que o processo pode gerar
    Spool &spool_ref;           // Referência ao spool de impressão
    int pedidos_enviados;       // Contador de pedidos enviados
    std::thread thread_process; // Thread do processo

    // Função que executa o processo, gerando pedidos de impressão
    void run()
    {
        // Inicialização do gerador de números aleatórios
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> paginas_dist(1, 10);   // Distribuição para número de páginas
        std::uniform_int_distribution<> prioridade_dist(1, 5); // Distribuição para prioridade

        while (pedidos_enviados < max_pedidos)
        {
            Pedido pedido;
            pedido.id = pedidos_enviados++;
            pedido.nome_documento = "arquivo_" + std::to_string(id) + "_" + std::to_string(pedido.id);
            pedido.num_paginas = paginas_dist(gen);   // Gera um número aleatório de páginas
            pedido.prioridade = prioridade_dist(gen); // Gera uma prioridade aleatória
            pedido.id_processo = id;
            pedido.hora_solicitacao = std::chrono::system_clock::now(); // Registra o horário da solicitação

            // Impressão sincronizada da mensagem de geração do pedido
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "-----------------------------------------\n";
                std::cout << "Processo " << id << " gerou pedido " << pedido.nome_documento
                          << " com " << pedido.num_paginas << " paginas, prioridade " << pedido.prioridade << ".\n";
                std::cout << "-----------------------------------------\n\n";
            }

            spool_ref.add_pedido(pedido);                                // Adiciona o pedido ao spool
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Espera antes de gerar o próximo pedido
        }
        processos_ativos--; // Decrementa o contador de processos ativos ao finalizar
    }
};

// Função auxiliar para converter um time_point para string formatada
std::string time_point_to_string(const std::chrono::system_clock::time_point &tp)
{
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t); // Função segura para Windows
#else
    localtime_r(&t, &tm); // Função segura para Unix/Linux
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S"); // Formato HH:MM:SS
    return oss.str();
}

// Função para gerar o relatório final de impressão
void gerar_relatorio(const std::vector<RegistroImpressao> &registros, const std::unordered_map<int, int> &paginas_por_impressora)
{
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "-----------------------------------------\n";
        std::cout << "=== RELATORIO FINAL ===\n\n";

        // Resumo de impressão por impressora
        std::cout << "Resumo de Impressao por Impressora:\n";
        for (const auto &[impressora, paginas] : paginas_por_impressora)
        {
            std::cout << "  Impressora " << impressora << " -> Total de paginas impressas: " << paginas << "\n";
        }

        std::cout << "\nDetalhes dos Documentos Processados:\n";
    }

    // Lista detalhada de cada documento processado
    for (const auto &registro : registros)
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "-----------------------------------------\n";
        std::cout << "Documento         : " << registro.nome_documento << "\n";
        std::cout << "Paginas           : " << registro.num_paginas << "\n";
        std::cout << "Processo          : " << registro.id_processo << "\n";
        std::cout << "Impressora        : " << registro.id_impressora << "\n";
        std::cout << "Prioridade        : " << registro.prioridade << "\n";
        std::cout << "Hora Solicitacao  : " << time_point_to_string(registro.hora_solicitacao) << "\n";
        std::cout << "Hora Impressao    : " << time_point_to_string(registro.hora_inicio) << "\n";
        std::cout << "Tempo Total       : " << registro.tempo_total.count() << "ms\n";
        std::cout << "-----------------------------------------\n\n";
    }
}

int main()
{
    int num_processos, num_impressoras, capacidade_buffer, tempo_por_pagina_ms;

    // Coleta dos dados de entrada do usuário
    coletar_dados(num_processos, num_impressoras, capacidade_buffer, tempo_por_pagina_ms);

    processos_ativos = num_processos; // Inicializa o contador de processos ativos

    Spool spool(capacidade_buffer); // Cria o spool com a capacidade definida

    std::vector<RegistroImpressao> registros;            // Vetor para armazenar os registros de impressão
    std::unordered_map<int, int> paginas_por_impressora; // Mapa para contar páginas por impressora
    std::mutex registro_mutex;                           // Mutex para proteger o acesso aos registros

    // Inicializa o contador de páginas por impressora
    for (int i = 1; i <= num_impressoras; ++i)
    {
        paginas_por_impressora[i] = 0;
    }

    // Cria e inicia os processos que geram pedidos de impressão
    std::vector<std::unique_ptr<Processo>> processos;
    processos.reserve(num_processos); // Reserva espaço para evitar realocações
    for (int i = 1; i <= num_processos; ++i)
    {
        processos.emplace_back(std::make_unique<Processo>(i, 5, spool)); // Cada processo gera 5 pedidos
        processos.back()->start();
    }

    // Cria e inicia as impressoras usando std::unique_ptr
    std::vector<std::unique_ptr<Impressora>> impressoras;
    impressoras.reserve(num_impressoras); // Reserva espaço para evitar realocações
    for (int i = 1; i <= num_impressoras; ++i)
    {
        impressoras.emplace_back(std::make_unique<Impressora>(i, spool, registros, paginas_por_impressora, registro_mutex, tempo_por_pagina_ms));
        impressoras.back()->start();
    }

    // Espera até que o sistema esteja inativo por 30 segundos
    spool.wait_until_finished(std::chrono::seconds(30));

    // Encerra o spool para que as impressoras possam terminar
    spool.encerrar_spool();

    // Aguarda o término de todas as threads dos processos
    for (auto &processo : processos)
    {
        processo->join();
    }

    // Aguarda o término de todas as threads das impressoras
    for (auto &impressora : impressoras)
    {
        impressora->join();
    }

    // Gera o relatório final de impressão
    gerar_relatorio(registros, paginas_por_impressora);

    return 0;
}
