#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <limits>

struct RequisicaoImpressao {
    int idProcesso;
    std::string nomeDocumento;
    int paginas;
    int prioridade;
    std::chrono::system_clock::time_point horaRequisicao;
    std::chrono::system_clock::time_point horaImpressao;
    int idImpressora;

    bool operator<(const RequisicaoImpressao& outra) const {
        return prioridade < outra.prioridade;
    }
};

class PoolImpressoras {
private:
    std::vector<RequisicaoImpressao> buffer;
    std::mutex mtx;
    std::condition_variable nao_vazio;
    size_t tamanhoMaximoBuffer;
    bool simulacaoRodando;
    std::vector<int> estatisticasImpressoras;
    std::vector<RequisicaoImpressao> trabalhosConcluidos;

public:
    std::string obterTimestamp() {
        auto agora = std::chrono::system_clock::now();
        auto tempo = std::chrono::system_clock::to_time_t(agora);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&tempo), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

public:
    PoolImpressoras(size_t tamanhoBuffer, int numImpressoras)
        : tamanhoMaximoBuffer(tamanhoBuffer), simulacaoRodando(true), estatisticasImpressoras(numImpressoras, 0) {}

    bool adicionarRequisicao(RequisicaoImpressao requisicao) {
        std::unique_lock<std::mutex> lock(mtx);
        if (buffer.size() >= tamanhoMaximoBuffer) {
            std::cout << "[" << obterTimestamp() << "] [ALERTA] Buffer cheio. Requisicao descartada: "
                      << requisicao.nomeDocumento << "\n";
            return false;
        }
        buffer.push_back(requisicao);
        std::push_heap(buffer.begin(), buffer.end());
        std::cout << "[" << obterTimestamp() << "] [REQUISICAO] Adicionada: " << requisicao.nomeDocumento
                  << " (Prioridade: " << requisicao.prioridade << ", Paginas: " << requisicao.paginas << ")\n";
        nao_vazio.notify_one();
        return true;
    }

    bool obterRequisicao(RequisicaoImpressao& requisicao) {
        std::unique_lock<std::mutex> lock(mtx);
        nao_vazio.wait(lock, [this] { return !buffer.empty() || !simulacaoRodando; });

        if (!simulacaoRodando && buffer.empty()) {
            return false;
        }

        std::pop_heap(buffer.begin(), buffer.end());
        requisicao = buffer.back();
        buffer.pop_back();
        std::cout << "[" << obterTimestamp() << "] [REQUISICAO] Obtida para impressao: "
                  << requisicao.nomeDocumento << " (Prioridade: " << requisicao.prioridade << ")\n";
        return true;
    }

    void adicionarTrabalhoConcluido(const RequisicaoImpressao& requisicao) {
        std::lock_guard<std::mutex> lock(mtx);
        trabalhosConcluidos.push_back(requisicao);
        estatisticasImpressoras[requisicao.idImpressora] += requisicao.paginas;
        std::cout << "[" << obterTimestamp() << "] [IMPRESSORA] Impressao concluida: " << requisicao.nomeDocumento
                  << " pela Impressora " << requisicao.idImpressora + 1
                  << " (Paginas: " << requisicao.paginas << ")\n";
    }

    void pararSimulacao() {
        std::lock_guard<std::mutex> lock(mtx);
        simulacaoRodando = false;
        nao_vazio.notify_all();
    }

    void imprimirRelatorio() {
        std::cout << "\n=== RELATORIO FINAL ===\n\n";

        for (size_t i = 0; i < estatisticasImpressoras.size(); ++i) {
            std::cout << "Impressora " << i + 1 << ": " << estatisticasImpressoras[i] << " paginas\n";
        }

        std::cout << "\nDocumentos processados:\n";
        for (const auto& trabalho : trabalhosConcluidos) {
            auto horaRequisicao = std::chrono::system_clock::to_time_t(trabalho.horaRequisicao);
            auto horaImpressao = std::chrono::system_clock::to_time_t(trabalho.horaImpressao);
            auto duracao = std::chrono::duration_cast<std::chrono::milliseconds>(trabalho.horaImpressao - trabalho.horaRequisicao).count();

            std::stringstream horaReqStream, horaImpStream;
            horaReqStream << std::put_time(std::localtime(&horaRequisicao), "%Y-%m-%d %H:%M:%S");
            horaImpStream << std::put_time(std::localtime(&horaImpressao), "%Y-%m-%d %H:%M:%S");

            std::cout << "--------------------------------------------------------\n";
            std::cout << "Documento:     " << trabalho.nomeDocumento << "\n";
            std::cout << "Processo:      " << trabalho.idProcesso << "\n";
            std::cout << "Paginas:       " << trabalho.paginas << "\n";
            std::cout << "Prioridade:    " << trabalho.prioridade << "\n";
            std::cout << "Hora Req.:     " << horaReqStream.str() << "\n";
            std::cout << "Hora Imp.:     " << horaImpStream.str() << "\n";
            std::cout << "Tempo (ms):    " << duracao << "\n";
        }

        std::cout << "\n[LOG] Fim da simulacao. Todos os documentos foram processados.\n";
    }
};

bool validarEntrada(int& entrada) {
    std::cin >> entrada;
    if (std::cin.fail() || entrada < 1) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return false;
    }
    return true;
}

void threadImpressora(PoolImpressoras& pool, int idImpressora, int tempoPorPagina) {
    while (true) {
        RequisicaoImpressao requisicao;
        if (!pool.obterRequisicao(requisicao)) {
            break;
        }

        requisicao.idImpressora = idImpressora;
        requisicao.horaImpressao = std::chrono::system_clock::now();

        std::cout << "[" << pool.obterTimestamp() << "] [IMPRESSORA] Impressora " << idImpressora + 1
                  << " processando " << requisicao.nomeDocumento
                  << " (Paginas: " << requisicao.paginas << ")\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(tempoPorPagina * requisicao.paginas));
        pool.adicionarTrabalhoConcluido(requisicao);
    }

    std::cout << "[" << pool.obterTimestamp() << "] [IMPRESSORA] Impressora " << idImpressora + 1
              << " aguardando novas requisicoes.\n";
}

void threadProcesso(PoolImpressoras& pool, int idProcesso) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> paginas_dist(1, 20);
    std::uniform_int_distribution<> prioridade_dist(1, 5);
    std::uniform_int_distribution<> atraso_dist(1000, 5000);

    for (int i = 1; i <= 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(atraso_dist(gen)));

        RequisicaoImpressao requisicao;
        requisicao.idProcesso = idProcesso;
        requisicao.nomeDocumento = "Doc_P" + std::to_string(idProcesso) + "_" + std::to_string(i);
        requisicao.paginas = paginas_dist(gen);
        requisicao.prioridade = prioridade_dist(gen);
        requisicao.horaRequisicao = std::chrono::system_clock::now();

        if (!pool.adicionarRequisicao(requisicao)) {
            std::cout << "[" << pool.obterTimestamp() << "] [ALERTA] Buffer cheio. Requisicao descartada: "
                      << requisicao.nomeDocumento << "\n";
        }
    }
}

int main() {
    int numProcessos, numImpressoras, tamanhoBuffer, tempoPorPagina;

    std::cout << "Numero de processos (>= 1): ";
    while (!validarEntrada(numProcessos)) {
        std::cout << "Entrada invalida. Insira novamente: ";
    }

    std::cout << "Numero de impressoras (>= 1): ";
    while (!validarEntrada(numImpressoras)) {
        std::cout << "Entrada invalida. Insira novamente: ";
    }

    std::cout << "Tamanho do buffer (>= 1): ";
    while (!validarEntrada(tamanhoBuffer)) {
        std::cout << "Entrada invalida. Insira novamente: ";
    }

    std::cout << "Tempo por pagina (ms, >= 1): ";
    while (!validarEntrada(tempoPorPagina)) {
        std::cout << "Entrada invalida. Insira novamente: ";
    }

    PoolImpressoras pool(tamanhoBuffer, numImpressoras);
    std::vector<std::thread> impressoras, processos;

    for (int i = 0; i < numImpressoras; ++i) {
        impressoras.emplace_back(threadImpressora, std::ref(pool), i, tempoPorPagina);
    }

    for (int i = 0; i < numProcessos; ++i) {
        processos.emplace_back(threadProcesso, std::ref(pool), i + 1);
    }

    for (auto& p : processos) {
        p.join();
    }

    std::this_thread::sleep_for(std::chrono::seconds(30));

    pool.pararSimulacao();

    for (auto& p : impressoras) {
        p.join();
    }

    pool.imprimirRelatorio();

    return 0;
}