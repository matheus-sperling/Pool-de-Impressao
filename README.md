# Pool de Impressão

Este projeto é um simulador de pool de impressão, desenvolvido em C++. Ele gerencia a fila de impressão de múltiplos processos, priorizando os pedidos de impressão de acordo com a prioridade definida pelo usuário.

## Funcionalidades

- **Gerenciamento de Fila de Impressão**: Os pedidos de impressão são organizados em uma fila de prioridades.
- **Prioridades de Impressão**: Cada pedido de impressão pode ter uma prioridade de 1 a 5, onde 5 é a mais alta.
- **Relatório de Impressão**: Gera um relatório final com a quantidade total de páginas impressas por impressora e uma lista de documentos impressos.

## Como Usar

### Configuração Inicial

- Número de processos
- Número de impressoras
- Capacidade máxima do buffer
- Tempo de impressão por página (em milissegundos)
- Número máximo de pedidos por processo
- Número de páginas por pedido

### Definição de Prioridades

Defina a prioridade de cada pedido de impressão para cada processo.

### Simulação

O programa simula a adição de pedidos à fila de impressão e o processamento dos mesmos pelas impressoras.

### Relatório Final

Ao final da simulação, é gerado um relatório detalhado das impressões realizadas.

## Compilação e Execução

Para compilar e executar o programa, utilize um compilador que suporte C++11 ou superior.

g++ -std=c++11 -o pool_de_impressao main.cpp -pthread
./pool_de_impressao

## Estrutura do Projeto

- **main.cpp**: Contém a lógica principal do simulador.
- **.vscode/settings.json**: Configurações do editor para associação de arquivos.

## Contribuição

Contribuições são bem-vindas! Sinta-se à vontade para abrir issues ou enviar pull requests.

## Licença

Este projeto não possui uma licença específica.
