# Simulador de Pool de Impressão

Este projeto é um simulador de um pool de impressão, implementado em C++ utilizando programação multithread.

## Descrição

O programa simula um ambiente onde múltiplos processos geram pedidos de impressão e múltiplas impressoras processam esses pedidos. Ele utiliza threads, mutexes, variáveis de condição e filas de prioridade para sincronizar o acesso aos recursos compartilhados.

## Funcionalidades

- Processos geradores de pedidos de impressão com número aleatório de páginas e prioridades.
- Spool de impressão que gerencia os pedidos em uma fila de prioridade.
- Impressoras que processam os pedidos do spool.
- Geração de relatório final detalhado com as informações de cada impressão.

## Compilação

Certifique-se de ter um compilador C++ com suporte a C++11 ou superior.

Para compilar o programa, execute:

g++ -std=c++11 -o pool_de_impressao main.cpp -pthread
./pool_de_impressao

## Estrutura do Projeto

- **main.cpp**: Contém a lógica principal do simulador.
- **.vscode/settings.json**: Configurações do editor para associação de arquivos.

## Contribuição

Contribuições são bem-vindas! Sinta-se à vontade para abrir issues ou enviar pull requests.

## Licença

Este projeto não possui uma licença específica.
