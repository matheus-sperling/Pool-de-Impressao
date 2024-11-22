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

// Estrutura de pedidos de impressão com prioridade
struct Pedido {
    int id; // Identificador único
    std::string nome_documento;
    int num_paginas;
    int prioridade; // 1 a 5 (5 mais alta)
    int id_processo;
    std::chrono::system_clock::time_point hora_solicitacao;

    // Operador de comparação para a priority_queue
    bool operator<(const Pedido& other) const {
        // Prioridade maior tem precedência
        return prioridade < other.prioridade;
    }
};

// Estrutura para registro de impressão
struct RegistroImpressao {
    std::string nome_documento;
    int num_paginas;
    int id_processo;
    int id_impressora;
    std::chrono::system_clock::time_point hora_solicitacao;
    std::chrono::system_clock::time_point hora_inicio;
    std::chrono::milliseconds tempo_total;
};

// Variáveis globais
std::priority_queue<Pedido> buffer;
std::mutex buffer_mutex;
std::condition_variable buffer_cond_var;
std::atomic<bool> encerrar(false);
std::vector<RegistroImpressao> registros;
std::mutex registro_mutex;
std::unordered_map<int, int> paginas_por_impressora;
std::atomic<int> pedido_id_counter(0);

// Função para converter tempo para string
std::string time_point_to_string(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&tm, &t);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S");
    return oss.str();
}

// Função para coletar entradas do usuário
void coletar_dados(int& num_processos, int& num_impressoras, int& capacidade_buffer,
                   int& tempo_por_pagina_ms, int& max_pedidos, int& num_paginas) {
    std::cout << "Bem-vindo ao Simulador de Pool de Impressao!\n";
    std::cout << "Por favor, insira os seguintes parametros:\n";

    while (true) {
        std::cout << "1. Numero de processos (minimo 1): ";
        std::cin >> num_processos;
        if (num_processos > 0) break;
        std::cout << "Por favor, insira um valor valido.\n";
    }

    while (true) {
        std::cout << "2. Numero de impressoras (minimo 1): ";
        std::cin >> num_impressoras;
        if (num_impressoras > 0) break;
        std::cout << "Por favor, insira um valor valido.\n";
    }

    while (true) {
        std::cout << "3. Capacidade maxima do buffer (minimo 1): ";
        std::cin >> capacidade_buffer;
        if (capacidade_buffer > 0) break;
        std::cout << "Por favor, insira um valor valido.\n";
    }

    while (true) {
        std::cout << "4. Tempo de impressao por pagina (em ms, minimo 10): ";
        std::cin >> tempo_por_pagina_ms;
        if (tempo_por_pagina_ms >= 10) break;
        std::cout << "Por favor, insira um valor valido.\n";
    }

    while (true) {
        std::cout << "5. Numero maximo de pedidos por processo (minimo 1): ";
        std::cin >> max_pedidos;
        if (max_pedidos > 0) break;
        std::cout << "Por favor, insira um valor valido.\n";
    }

    while (true) {
        std::cout << "6. Numero de paginas por pedido (minimo 1): ";
        std::cin >> num_paginas;
        if (num_paginas > 0) break;
        std::cout << "Por favor, insira um valor valido.\n";
    }

    std::cout << "\nParametros configurados com sucesso!\n";
}

// Função para gerar relatório final
void gerar_relatorio() {
    std::lock_guard<std::mutex> lock(registro_mutex);

    std::cout << "\n=== Relatorio Final ===\n";

    if (registros.empty()) {
        std::cout << "Nenhum documento foi impresso.\n";
        return;
    }

    std::cout << "\nQuantidade total de paginas impressas por impressora:\n";
    for (const auto& [impressora, paginas] : paginas_por_impressora) {
        std::cout << " - Impressora " << impressora << ": " << paginas << " paginas\n";
    }

    std::cout << "\nLista de documentos impressos:\n";
    std::cout << std::left << std::setw(20) << "Documento"
              << std::setw(10) << "Paginas"
              << std::setw(10) << "Processo"
              << std::setw(15) << "Solicitacao"
              << std::setw(15) << "Inicio"
              << std::setw(15) << "Duracao(ms)"
              << std::setw(12) << "Impressora\n";

    for (const auto& registro : registros) {
        std::cout << std::left << std::setw(20) << registro.nome_documento
                  << std::setw(10) << registro.num_paginas
                  << std::setw(10) << registro.id_processo
                  << std::setw(15) << time_point_to_string(registro.hora_solicitacao)
                  << std::setw(15) << time_point_to_string(registro.hora_inicio)
                  << std::setw(15) << registro.tempo_total.count()
                  << std::setw(12) << registro.id_impressora << "\n";
    }
}

int main() {
    int num_processos, num_impressoras, capacidade_buffer, tempo_por_pagina_ms, max_pedidos, num_paginas;

    coletar_dados(num_processos, num_impressoras, capacidade_buffer, tempo_por_pagina_ms, max_pedidos, num_paginas);

    // Inicializar contagem de páginas por impressora
    for (int i = 1; i <= num_impressoras; ++i) {
        paginas_por_impressora[i] = 0;
    }

    std::vector<std::thread> processos, impressoras;

    // Estrutura para armazenar prioridades definidas pelo usuário
    // prioridade_por_processo[processo][pedido] = prioridade
    std::vector<std::vector<int>> prioridade_por_processo(num_processos, std::vector<int>(max_pedidos, 1));

    // Coletar prioridades manualmente
    std::cout << "\nDefina a prioridade para cada pedido de impressao (1 a 5, onde 5 e a mais alta):\n";
    for (int i = 0; i < num_processos; ++i) {
        std::cout << "Processo " << (i + 1) << ":\n";
        for (int j = 0; j < max_pedidos; ++j) {
            int prioridade;
            while (true) {
                std::cout << "  Pedido " << j + 1 << ": ";
                std::cin >> prioridade;
                if (prioridade >= 1 && prioridade <= 5) break;
                std::cout << "  Prioridade invalida. Insira um valor entre 1 e 5.\n";
            }
            prioridade_por_processo[i][j] = prioridade;
        }
    }

    // Variável para monitorar se todos os pedidos foram adicionados
    std::atomic<int> pedidos_adicionados(0);

    // Thread para impressoras
    for (int i = 1; i <= num_impressoras; ++i) {
        impressoras.emplace_back([&, i]() {
            while (true) {
                Pedido pedido;

                {
                    std::unique_lock<std::mutex> lock(buffer_mutex);
                    buffer_cond_var.wait(lock, [&]() { return !buffer.empty() || encerrar; });

                    if (buffer.empty() && encerrar) break;

                    if (!buffer.empty()) {
                        pedido = buffer.top();
                        buffer.pop();
                        std::cout << "Pedido removido: " << pedido.nome_documento
                                  << " (ID: " << pedido.id << ", Prioridade: " << pedido.prioridade << ")\n";
                    } else {
                        continue;
                    }
                }

                // Processar o pedido
                auto inicio = std::chrono::system_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(pedido.num_paginas * tempo_por_pagina_ms));
                auto fim = std::chrono::system_clock::now();

                RegistroImpressao registro{
                    pedido.nome_documento,
                    pedido.num_paginas,
                    pedido.id_processo,
                    i,
                    pedido.hora_solicitacao,
                    inicio,
                    std::chrono::duration_cast<std::chrono::milliseconds>(fim - inicio)
                };

                {
                    std::lock_guard<std::mutex> lock(registro_mutex);
                    registros.push_back(registro);
                    paginas_por_impressora[i] += pedido.num_paginas;
                }
            }
        });
    }

    // Thread para processos
    for (int i = 1; i <= num_processos; ++i) {
        processos.emplace_back([&, i]() {
            for (int j = 0; j < max_pedidos; ++j) {
                Pedido pedido{
                    pedido_id_counter.fetch_add(1),
                    "Documento_" + std::to_string(i) + "_" + std::to_string(j),
                    num_paginas,
                    prioridade_por_processo[i - 1][j],
                    i,
                    std::chrono::system_clock::now()
                };

                {
                    std::unique_lock<std::mutex> lock(buffer_mutex);
                    buffer_cond_var.wait(lock, [&]() { return buffer.size() < static_cast<size_t>(capacidade_buffer) || encerrar; });

                    if (encerrar) {
                        return;
                    }

                    if (buffer.size() < static_cast<size_t>(capacidade_buffer)) {
                        buffer.push(pedido);
                        std::cout << "Pedido adicionado: " << pedido.nome_documento
                                  << " (ID: " << pedido.id << ", Prioridade: " << pedido.prioridade << ")\n";
                        pedidos_adicionados++;
                    } else {
                        std::cout << "Buffer cheio. Pedido descartado: " << pedido.nome_documento
                                  << " (ID: " << pedido.id << ")\n";
                        // Opcional: Implementar fila de espera ou lógica adicional
                    }
                }
                buffer_cond_var.notify_all();
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Tempo entre pedidos
            }
        });
    }

    // Aguardar conclusão dos processos
    for (auto& t : processos) t.join();

    // Sinalizar para as impressoras que devem encerrar
    encerrar = true;
    buffer_cond_var.notify_all();

    // Aguardar conclusão das impressoras
    for (auto& t : impressoras) t.join();

    gerar_relatorio();

    return 0;
}
