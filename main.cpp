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

// Mutex e variavel para rastrear o ultimo pedido recebido
std::mutex last_request_mutex;
std::chrono::system_clock::time_point last_request_time;

// Estrutura que define um pedido de impressao
struct Pedido
{
    int id;                                                 // Identificador unico do pedido
    std::string nome_documento;                             // Nome do documento a ser impresso
    int num_paginas;                                        // Numero de paginas do documento
    int prioridade;                                         // Prioridade do pedido (1 a 5)
    int id_processo;                                        // Identificador do processo que gerou o pedido
    std::chrono::system_clock::time_point hora_solicitacao; // Horario da solicitacao

    // Sobrecarga do operador < para definir a ordem de prioridade na fila
    bool operator<(const Pedido &outro) const
    {
        // Maior prioridade (valor maior) tem precedencia
        if (prioridade == outro.prioridade)
        {
            // Se as prioridades forem iguais, o pedido mais antigo tem precedencia
            return hora_solicitacao > outro.hora_solicitacao;
        }
        return prioridade < outro.prioridade;
    }
};

// Estrutura que armazena os dados de um pedido processado
struct RegistroImpressao
{
    std::string nome_documento;                             // Nome do documento
    int num_paginas;                                        // Numero de paginas
    int id_processo;                                        // Identificador do processo solicitante
    int id_impressora;                                      // Identificador da impressora utilizada
    std::chrono::system_clock::time_point hora_solicitacao; // Horario da solicitacao
    std::chrono::system_clock::time_point hora_inicio;      // Horario de inicio da impressao
    std::chrono::milliseconds tempo_total;                  // Tempo total de impressao
    int prioridade;                                         // Prioridade do pedido
};

// Contador global de processos ativos
std::atomic<int> processos_ativos(0);

// Classe que gerencia o spool de impressao
class Spool
{
public:
    // Construtor que define a capacidade maxima do buffer
    Spool(int capacidade_buffer)
        : capacidade(capacidade_buffer), encerrar(false) {}

    // Funcao para adicionar um pedido ao buffer
    bool add_pedido(const Pedido &pedido)
    {
        std::unique_lock<std::mutex> lock(mutex_buffer);
        // Atualiza o tempo da ultima solicitacao
        {
            std::lock_guard<std::mutex> time_lock(last_request_mutex);
            last_request_time = std::chrono::system_clock::now();
        }
        // Tenta adicionar o pedido ao buffer, esperando por ate 1 segundo
        if (!cond_var_buffer.wait_for(lock, std::chrono::seconds(1), [this]()
                                      { return buffer.size() < capacidade || encerrar.load(); }))
        {
            // Timeout: nao houve espaco disponivel
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Buffer cheio. Pedido " << pedido.nome_documento << " foi descartado.\n\n";
            }
            return false; // Indica que o pedido foi descartado
        }
        if (encerrar.load())
            return false;
        buffer.push(pedido); // Adiciona o pedido a fila de prioridade

        // Impressao sincronizada da mensagem de recebimento do pedido
        {
            std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
            std::cout << "-----------------------------------------\n";
            std::cout << "Spool recebeu pedido " << pedido.nome_documento << " com "
                      << pedido.num_paginas << " paginas, prioridade " << pedido.prioridade << ".\n";
            std::cout << "-----------------------------------------\n\n";
        }

        cond_var_buffer.notify_one(); // Notifica que um novo pedido foi adicionado
        return true;                  // Indica que o pedido foi adicionado com sucesso
    }

    // Funcao para obter um pedido do buffer
    bool get_pedido(Pedido &pedido)
    {
        std::unique_lock<std::mutex> lock(mutex_buffer);
        // Espera ate que haja um pedido na fila ou que o sistema esteja encerrando
        cond_var_buffer.wait(lock, [this]()
                             { return !buffer.empty() || encerrar.load(); });

        if (buffer.empty())
        {
            // Se a fila esta vazia e o sistema esta encerrando
            return false; // Indica que nao ha mais pedidos para processar
        }

        pedido = buffer.top();        // Obtem o pedido de maior prioridade
        buffer.pop();                 // Remove o pedido da fila
        cond_var_buffer.notify_one(); // Notifica que um pedido foi removido
        return true;
    }

    // Funcao que espera ate que o sistema esteja inativo e entao sinaliza o encerramento
    void wait_until_finished()
    {
        // Inicializa o tempo da ultima solicitacao como o tempo atual
        {
            std::lock_guard<std::mutex> time_lock(last_request_mutex);
            last_request_time = std::chrono::system_clock::now();
        }

        // Inicia a thread de monitoramento de inatividade
        std::thread monitor_thread(&Spool::monitorar_inatividade, this);

        // Espera ate que o sistema esteja encerrando
        monitor_thread.join();
    }

    // Funcao para sinalizar o encerramento do spool (fallback)
    void encerrar_spool()
    {
        encerrar.store(true);
        cond_var_buffer.notify_all();
    }

private:
    std::priority_queue<Pedido> buffer;      // Fila de prioridade para os pedidos
    std::mutex mutex_buffer;                 // Mutex para proteger o acesso ao buffer
    std::condition_variable cond_var_buffer; // Variavel de condicao para sincronizacao
    int capacidade;                          // Capacidade maxima do buffer
    std::atomic<bool> encerrar;              // Flag para indicar o encerramento do sistema

    // Funcao de monitoramento de inatividade
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
                    std::cout << "\nNenhuma nova solicitacao de impressao recebida por 30 segundos. Sinalizando encerramento.\n\n";
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
                    std::cout << "\rRelatorio sera gerado em " << tempo_restante << " segundos.        " << std::flush;
                }
            }
        }
    }
};

// Classe que representa uma Impressora
class Impressora
{
public:
    // Construtor que inicializa a impressora com seu ID, referencia ao spool, registros, contador de paginas e tempo por pagina
    Impressora(int id, Spool &spool, std::vector<RegistroImpressao> &registros,
               std::unordered_map<int, int> &paginas_por_impressora, std::mutex &registro_mutex,
               int tempo_por_pagina_ms)
        : id_impressora(id), spool_ref(spool), registros_ref(registros),
          paginas_por_impressora_ref(paginas_por_impressora), registro_mutex_ref(registro_mutex),
          tempo_por_pagina_ms(tempo_por_pagina_ms) {}

    // Deleta o construtor de copia e o operador de atribuicao para evitar copias
    Impressora(const Impressora &) = delete;
    Impressora &operator=(const Impressora &) = delete;

    // Funcao que inicia a thread da impressora
    void start()
    {
        thread_impressora = std::thread(&Impressora::run, this);
    }

    // Funcao que espera a thread da impressora terminar
    void join()
    {
        if (thread_impressora.joinable())
            thread_impressora.join();
    }

private:
    int id_impressora;                                        // Identificador da impressora
    Spool &spool_ref;                                         // Referencia ao spool de impressao
    std::vector<RegistroImpressao> &registros_ref;            // Referencia aos registros de impressao
    std::unordered_map<int, int> &paginas_por_impressora_ref; // Referencia ao contador de paginas por impressora
    std::mutex &registro_mutex_ref;                           // Referencia ao mutex para registrar impressoes
    int tempo_por_pagina_ms;                                  // Tempo de impressao por pagina
    std::thread thread_impressora;                            // Thread da impressora

    // Funcao que simula o funcionamento da impressora
    void run()
    {
        while (true)
        {
            Pedido pedido;
            bool existe_pedido = spool_ref.get_pedido(pedido);
            if (!existe_pedido)
            {
                // Nenhum pedido para processar e o spool esta encerrando
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Impressora " << id_impressora << " esta encerrando.\n\n";
                break;
            }

            // Mensagem de inicio do processamento
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "-----------------------------------------\n";
                std::cout << "Impressora " << id_impressora << " iniciou processamento de "
                          << pedido.nome_documento << " com " << pedido.num_paginas
                          << " paginas, prioridade " << pedido.prioridade << ".\n";
                std::cout << "-----------------------------------------\n\n";
            }

            auto inicio = std::chrono::system_clock::now(); // Horario de inicio da impressao
            // Simula o tempo de impressao
            std::this_thread::sleep_for(std::chrono::milliseconds(tempo_por_pagina_ms * pedido.num_paginas));
            auto fim = std::chrono::system_clock::now(); // Horario de fim da impressao

            // Calcula o tempo total de impressao
            std::chrono::milliseconds duracao = std::chrono::duration_cast<std::chrono::milliseconds>(fim - inicio);

            // Registro da impressao
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

            // Mensagem de conclusao do processamento
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

// Funcao auxiliar para ler e validar entradas inteiras
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
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Descarte a entrada invalida
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Entrada invalida! Por favor, insira um numero inteiro valido.\n\n";
            }
            continue;
        }

        // Descarte qualquer caractere extra no buffer de entrada
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        // Verifica se o valor atende ao criterio minimo
        if (valor >= minimo)
        {
            break;
        }
        else
        {
            {
                std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                std::cout << "Valor invalido! O valor deve ser no minimo " << minimo << ".\n\n";
            }
        }
    }
    return valor;
}

// Funcao para coletar os dados de entrada do usuario
void coletar_dados(int &num_processos, int &num_impressoras, int &capacidade_buffer, int &tempo_por_pagina_ms)
{
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "Bem-vindo ao Simulador de Pool de Impressao!\n\n";
    }

    // Coleta a quantidade de processos (minimo 1)
    num_processos = ler_entrada("Quantidade de processos (minimo 1): ", 1);

    // Coleta a quantidade de impressoras (minimo 1)
    num_impressoras = ler_entrada("Quantidade de impressoras (minimo 1): ", 1);

    // Coleta a capacidade maxima do buffer (minimo 1)
    capacidade_buffer = ler_entrada("Capacidade maxima do buffer (minimo 1): ", 1);

    // Coleta o tempo de impressao por pagina (minimo 10 ms)
    tempo_por_pagina_ms = ler_entrada("Tempo de impressao por pagina (ms, minimo 10): ", 10);

    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "\n";
    }
}

// Classe que representa um processo que gera pedidos de impressao
class Processo
{
public:
    // Construtor que inicializa o processo com seu ID, numero maximo de pedidos e referencia ao spool
    Processo(int pid, int max_pedidos, Spool &spool)
        : id(pid), max_pedidos(max_pedidos), spool_ref(spool), pedidos_enviados(0) {}

    // Deleta o construtor de copia e o operador de atribuicao para evitar copias
    Processo(const Processo &) = delete;
    Processo &operator=(const Processo &) = delete;

    // Funcao para iniciar a thread do processo
    void start()
    {
        thread_process = std::thread(&Processo::run, this);
    }

    // Funcao para esperar a thread do processo terminar
    void join()
    {
        if (thread_process.joinable())
            thread_process.join();
    }

private:
    int id;                     // Identificador do processo
    int max_pedidos;            // Numero maximo de pedidos que o processo pode gerar
    Spool &spool_ref;           // Referencia ao spool de impressao
    int pedidos_enviados;       // Contador de pedidos enviados
    std::thread thread_process; // Thread do processo

    // Funcao que executa o processo, gerando pedidos de impressao
    void run()
    {
        try
        {
            // Inicializacao do gerador de numeros aleatorios
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> paginas_dist(1, 10);   // Distribuicao para numero de paginas
            std::uniform_int_distribution<> prioridade_dist(1, 5); // Distribuicao para prioridade

            while (pedidos_enviados < max_pedidos)
            {
                Pedido pedido;
                pedido.id = pedidos_enviados++;
                pedido.nome_documento = "arquivo_" + std::to_string(id) + "_" + std::to_string(pedido.id);
                pedido.num_paginas = paginas_dist(gen);   // Gera um numero aleatorio de paginas
                pedido.prioridade = prioridade_dist(gen); // Gera uma prioridade aleatoria
                pedido.id_processo = id;
                pedido.hora_solicitacao = std::chrono::system_clock::now(); // Registra o horario da solicitacao

                // Impressao sincronizada da mensagem de geracao do pedido
                {
                    std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
                    std::cout << "-----------------------------------------\n";
                    std::cout << "Processo " << id << " gerou pedido " << pedido.nome_documento
                              << " com " << pedido.num_paginas << " paginas, prioridade " << pedido.prioridade << ".\n";
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
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Espera antes de gerar o proximo pedido
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

// Funcao auxiliar para converter um time_point para string formatada
std::string time_point_to_string(const std::chrono::system_clock::time_point &tp)
{
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t); // Funcao segura para Windows
#else
    localtime_r(&t, &tm); // Funcao segura para Unix/Linux
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S"); // Formato HH:MM:SS
    return oss.str();
}

// Funcao para gerar o relatorio final de impressao
void gerar_relatorio(const std::vector<RegistroImpressao> &registros, const std::unordered_map<int, int> &paginas_por_impressora)
{
    {
        std::lock_guard<std::mutex> cout_lock_guard(cout_mutex);
        std::cout << "-----------------------------------------\n";
        std::cout << "=== RELATORIO FINAL ===\n\n";

        // Resumo de impressao por impressora
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

    // Coleta dos dados de entrada do usuario
    coletar_dados(num_processos, num_impressoras, capacidade_buffer, tempo_por_pagina_ms);

    processos_ativos = num_processos; // Inicializa o contador de processos ativos

    Spool spool(capacidade_buffer); // Cria o spool com a capacidade definida

    std::vector<RegistroImpressao> registros;            // Vetor para armazenar os registros de impressao
    std::unordered_map<int, int> paginas_por_impressora; // Mapa para contar paginas por impressora
    std::mutex registro_mutex;                           // Mutex para proteger o acesso aos registros

    // Inicializa o contador de paginas por impressora
    for (int i = 1; i <= num_impressoras; ++i)
    {
        paginas_por_impressora[i] = 0;
    }

    // Cria e inicia os processos que geram pedidos de impressao
    std::vector<std::unique_ptr<Processo>> processos;
    processos.reserve(num_processos); // Reserva espaco para evitar realocacoes
    for (int i = 1; i <= num_processos; ++i)
    {
        processos.emplace_back(std::make_unique<Processo>(i, 5, spool)); // Cada processo gera 5 pedidos
        processos.back()->start();
    }

    // Cria e inicia as impressoras usando std::unique_ptr
    std::vector<std::unique_ptr<Impressora>> impressoras;
    impressoras.reserve(num_impressoras); // Reserva espaco para evitar realocacoes
    for (int i = 1; i <= num_impressoras; ++i)
    {
        impressoras.emplace_back(std::make_unique<Impressora>(i, spool, registros, paginas_por_impressora, registro_mutex, tempo_por_pagina_ms));
        impressoras.back()->start();
    }

    // Espera ate que todos os processos tenham terminado e a fila esteja vazia
    spool.wait_until_finished();

    // Aguarda o termino de todas as threads dos processos
    for (auto &processo : processos)
    {
        processo->join();
    }

    // Aguarda o termino de todas as threads das impressoras
    for (auto &impressora : impressoras)
    {
        impressora->join();
    }

    // Gera o relatorio final de impressao
    gerar_relatorio(registros, paginas_por_impressora);

    return 0;
}
