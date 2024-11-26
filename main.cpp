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
#include <limits> // Para std::numeric_limits

// Mutex global para sincronizar o acesso ao std::cout
std::mutex cout_mutex;

// Mutex e variável para rastrear o último pedido recebido
std::mutex last_request_mutex;
std::chrono::system_clock::time_point last_request_time;

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
std::atomic<int> processos_ativos(0);

// Classe que gerencia o spool de impressão
class Spool
{
public:
    // Construtor que define a capacidade máxima do buffer
    Spool(int capacidade_buffer)
        : capacidade(capacidade_buffer), encerrar(false) {}

    // Função para adicionar um pedido ao buffer
    bool add_pedido(const Pedido &pedido)
    {
        std::unique_lock<std::mutex> lock(mutex_buffer);
        // Atualiza o tempo da última solicitação
        {
            std::lock_guard<std::mutex> time_lock(last_request_mutex);
            last_request_time = std::chrono::system_clock::now();
        }
        // Tenta adicionar o pedido ao buffer, esperando por até 1 segundo
        if (!cond_var_buffer.wait_for(lock, std::chrono::seconds(1), [this]()
                                      { return buffer.size() < capacidade || encerrar.load(); }))
        {
            // Timeout: não houve espaço disponível
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Buffer cheio. Pedido " << pedido.nome_documento << " foi descartado.\n\n";
            }
            return false; // Indica que o pedido foi descartado
        }
        if (encerrar.load())
            return false;
        buffer.push(pedido); // Adiciona o pedido à fila de prioridade

        // Impressão sincronizada da mensagem de recebimento do pedido
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "-----------------------------------------\n";
            std::cout << "Spool recebeu pedido " << pedido.nome_documento << " com "
                      << pedido.num_paginas << " páginas, prioridade " << pedido.prioridade << ".\n";
            std::cout << "-----------------------------------------\n\n";
        }

        cond_var_buffer.notify_one(); // Notifica que um novo pedido foi adicionado
        return true;                  // Indica que o pedido foi adicionado com sucesso
    }

    // Função para obter um pedido do buffer
    bool get_pedido(Pedido &pedido)
    {
        std::unique_lock<std::mutex> lock(mutex_buffer);
        // Espera até que haja um pedido na fila ou que o sistema esteja encerrando
        cond_var_buffer.wait(lock, [this]()
                             { return !buffer.empty() || encerrar.load(); });

        if (buffer.empty())
        {
            // Se a fila está vazia e o sistema está encerrando
            return false; // Indica que não há mais pedidos para processar
        }

        pedido = buffer.top();        // Obtém o pedido de maior prioridade
        buffer.pop();                 // Remove o pedido da fila
        cond_var_buffer.notify_one(); // Notifica que um pedido foi removido
        return true;
    }

    // Função que espera até que o sistema esteja inativo e então sinaliza o encerramento
    void wait_until_finished()
    {
        // Inicializa o tempo da última solicitação como o tempo atual
        {
            std::lock_guard<std::mutex> time_lock(last_request_mutex);
            last_request_time = std::chrono::system_clock::now();
        }

        // Inicia a thread de monitoramento de inatividade
        std::thread monitor_thread(&Spool::monitorar_inatividade, this);

        // Espera até que o sistema esteja encerrando
        monitor_thread.join();
    }

    // Função para sinalizar o encerramento do spool (fallback)
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

    // Função de monitoramento de inatividade
    void monitorar_inatividade()
    {
        const int tempo_limite = 30; // Tempo limite de inatividade em segundos
        while (!encerrar.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Verifica a cada segundo
            std::chrono::system_clock::time_point agora = std::chrono::system_clock::now();
            std::chrono::system_clock::time_point ultimo_pedido;
            {
                std::lock_guard<std::mutex> time_lock(last_request_mutex);
                ultimo_pedido = last_request_time;
            }
            auto duracao = std::chrono::duration_cast<std::chrono::seconds>(agora - ultimo_pedido).count();
            if (duracao >= tempo_limite)
            { // Verifica se passaram 30 segundos
                {
                    std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                    std::cout << "\nNenhuma nova solicitação de impressão recebida por 30 segundos. Sinalizando encerramento.\n\n";
                }
                encerrar.store(true);         // Sinaliza para as impressoras encerrarem
                cond_var_buffer.notify_all(); // Notifica todas as impressoras
                break;
            }
            else
            {
                int tempo_restante = tempo_limite - static_cast<int>(duracao);
                {
                    std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                    std::cout << "\rRelatório será gerado em " << tempo_restante << " segundos.        " << std::flush;
                }
            }
        }
    }
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
          tempo_por_pagina_ms(tempo_por_pagina_ms) {}

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

private:
    int id_impressora;                                        // Identificador da impressora
    Spool &spool_ref;                                         // Referência ao spool de impressão
    std::vector<RegistroImpressao> &registros_ref;            // Referência aos registros de impressão
    std::unordered_map<int, int> &paginas_por_impressora_ref; // Referência ao contador de páginas por impressora
    std::mutex &registro_mutex_ref;                           // Referência ao mutex para registrar impressões
    int tempo_por_pagina_ms;                                  // Tempo de impressão por página
    std::thread thread_impressora;                            // Thread da impressora

    // Função que simula o funcionamento da impressora
    void run()
    {
        while (true)
        {
            Pedido pedido;
            bool existe_pedido = spool_ref.get_pedido(pedido);
            if (!existe_pedido)
            {
                // Nenhum pedido para processar e o spool está encerrando
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Impressora " << id_impressora << " está encerrando.\n\n";
                break;
            }

            // Mensagem de início do processamento
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "-----------------------------------------\n";
                std::cout << "Impressora " << id_impressora << " iniciou processamento de "
                          << pedido.nome_documento << " com " << pedido.num_paginas
                          << " páginas, prioridade " << pedido.prioridade << ".\n";
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
};

// Função auxiliar para ler e validar entradas inteiras
int ler_entrada(const std::string &prompt, int minimo)
{
    int valor;
    while (true)
    {
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << prompt;
        }
        std::cin >> valor;

        // Verifica se a leitura foi bem-sucedida
        if (std::cin.fail())
        {
            std::cin.clear();                                                   // Limpa o estado de erro
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Descarte a entrada inválida
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Entrada inválida! Por favor, insira um número inteiro válido.\n\n";
            }
            continue;
        }

        // Descarte qualquer caractere extra no buffer de entrada
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        // Verifica se o valor atende ao critério mínimo
        if (valor >= minimo)
        {
            break;
        }
        else
        {
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Valor inválido! O valor deve ser no mínimo " << minimo << ".\n\n";
            }
        }
    }
    return valor;
}

// Função para coletar os dados de entrada do usuário
void coletar_dados(int &num_processos, int &num_impressoras, int &capacidade_buffer, int &tempo_por_pagina_ms)
{
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "Bem-vindo ao Simulador de Pool de Impressão!\n\n";
    }

    // Coleta a quantidade de processos (mínimo 1)
    num_processos = ler_entrada("Quantidade de processos (mínimo 1): ", 1);

    // Coleta a quantidade de impressoras (mínimo 1)
    num_impressoras = ler_entrada("Quantidade de impressoras (mínimo 1): ", 1);

    // Coleta a capacidade máxima do buffer (mínimo 1)
    capacidade_buffer = ler_entrada("Capacidade máxima do buffer (mínimo 1): ", 1);

    // Coleta o tempo de impressão por página (mínimo 10 ms)
    tempo_por_pagina_ms = ler_entrada("Tempo de impressão por página (ms, mínimo 10): ", 10);

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
        try
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
                              << " com " << pedido.num_paginas << " páginas, prioridade " << pedido.prioridade << ".\n";
                    std::cout << "-----------------------------------------\n\n";
                }

                bool sucesso = spool_ref.add_pedido(pedido); // Adiciona o pedido ao spool
                if (!sucesso)
                {
                    // Opcional: registrar que o pedido foi descartado
                    {
                        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                        std::cout << "Processo " << id << " notificou que o pedido " << pedido.nome_documento << " foi descartado.\n\n";
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Espera antes de gerar o próximo pedido
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cerr << "Erro no processo " << id << ": " << e.what() << "\n";
        }
        catch (...)
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cerr << "Erro desconhecido no processo " << id << ".\n";
        }
        processos_ativos--; // Decrementa o contador de processos ativos ao finalizar
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "Processo " << id << " finalizou.\n\n";
        }
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
        std::cout << "=== RELATÓRIO FINAL ===\n\n";

        // Resumo de impressão por impressora
        std::cout << "Resumo de Impressão por Impressora:\n";
        for (const auto &[impressora, paginas] : paginas_por_impressora)
        {
            std::cout << "  Impressora " << impressora << " -> Total de páginas impressas: " << paginas << "\n";
        }

        std::cout << "\nDetalhes dos Documentos Processados:\n";
    }

    // Lista detalhada de cada documento processado
    for (const auto &registro : registros)
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "-----------------------------------------\n";
        std::cout << "Documento         : " << registro.nome_documento << "\n";
        std::cout << "Páginas           : " << registro.num_paginas << "\n";
        std::cout << "Processo          : " << registro.id_processo << "\n";
        std::cout << "Impressora        : " << registro.id_impressora << "\n";
        std::cout << "Prioridade        : " << registro.prioridade << "\n";
        std::cout << "Hora Solicitação  : " << time_point_to_string(registro.hora_solicitacao) << "\n";
        std::cout << "Hora Impressão    : " << time_point_to_string(registro.hora_inicio) << "\n";
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

    // Espera até que todos os processos tenham terminado e a fila esteja vazia
    spool.wait_until_finished();

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
