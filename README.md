# Manual de Uso - Implementação Alternativa

Este README descreve como usar a implementação alternativa do sistema de gerenciamento de spool de impressão. Este código apresenta diferenças significativas em relação ao projeto original, priorizando uma abordagem direta e concentrada, onde toda a lógica está centralizada em um único arquivo.

## Índice

1. [Introdução](#introdução)
2. [Estrutura do Código](#estrutura-do-código)
3. [Funcionalidades Principais](#funcionalidades-principais)
   - [Gerenciamento de Fila de Impressão](#1-gerenciamento-de-fila-de-impressão)
   - [Impressoras](#2-impressoras)
   - [Processos](#3-processos)
   - [Geração de Relatórios](#4-geraçao-de-relatórios)
4. [Diferenças em Relação ao Projeto Original](#diferenças-em-relação-ao-projeto-original)
5. [Uso do Código](#uso-do-código)
   - [Compilação](#compilação)
   - [Execução](#execução)
   - [Relatório Final](#relatório-final)
6. [Considerações](#considerações)

## Introdução

A implementação alternativa é construída para ser auto-suficiente, contendo todas as classes, métodos e a lógica do programa em um único arquivo. Ela oferece uma visão completa do funcionamento do sistema, sem a necessidade de navegação entre múltiplos arquivos.

## Estrutura do Código

Diferentemente do projeto original, onde as responsabilidades estão divididas em vários módulos, aqui tudo está centralizado no arquivo principal. Todas as classes, como `Spool`, `Impressora` e `Processo`, estão implementadas e definidas em um único lugar. Isso elimina a dependência de arquivos separados para headers e implementações.

## Funcionalidades Principais

### 1. Gerenciamento de Fila de Impressão
- A fila de prioridade é diretamente manipulada dentro da classe `Spool`.
- O monitoramento de inatividade é realizado com verificações constantes utilizando `std::this_thread::sleep_for`.

### 2. Impressoras
- Cada instância da classe `Impressora` é criada e gerenciada no arquivo principal.
- A lógica de processamento de documentos está incluída diretamente na classe, sem abstrações adicionais.

### 3. Processos
- Os processos são configurados e iniciados no próprio `main()` usando instâncias diretas da classe `Processo`.
- Cada processo gera pedidos de impressão de forma simples, sem dependência de funções externas ou módulos separados.

### 4. Geração de Relatórios
- A funcionalidade de geração de relatórios está embutida no código principal.
- Detalhes, como o total de páginas impressas por impressora e o histórico de documentos processados, são coletados e exibidos diretamente no final da execução.

## Diferenças em Relação ao Projeto Original

### Estrutura do Código
- Aqui, tudo está concentrado em um único arquivo, enquanto no projeto original as responsabilidades estão separadas em vários arquivos, como `spool.hpp`, `processo.cpp`, etc.

### Uso de Variáveis Globais
- Variáveis como `cout_mutex` e `last_request_time` são globais, permitindo sincronização e controle de maneira direta. No projeto original, essas responsabilidades são distribuídas e encapsuladas em classes específicas.

### Monitoramento de Inatividade
- O monitoramento de inatividade é feito através de loops com verificações constantes (polling). No projeto original, essa funcionalidade parece mais encapsulada e otimizada.

### Threads e Sincronização
- As threads de processos e impressoras são gerenciadas diretamente no `main()`. O projeto original delega esse controle para funções encapsuladas nos módulos específicos.

### Modularidade
- Todas as classes e funcionalidades estão implementadas juntas, sem divisão em módulos. No projeto original, cada classe é implementada em um arquivo separado, com interfaces definidas em headers.

### Simplicidade do `main()`
- O `main()` nesta implementação inclui toda a lógica de inicialização e controle. Já no projeto original, o `main()` é mais minimalista, com responsabilidades delegadas a módulos.

## Uso do Código

### Compilação
Compile o código com um compilador C++ moderno (ex.: GCC ou Clang):
```
g++ -std=c++17 -pthread -o spool_program main.cpp
```

### Execução
Execute o programa e siga as instruções para configurar o número de processos, impressoras e outras opções:
```
./spool_program
```

### Relatório Final
Ao final da execução, o programa exibe um relatório detalhado sobre os documentos processados e o desempenho das impressoras.

## Considerações

Essa implementação oferece uma abordagem mais direta e compacta, onde toda a lógica está acessível no mesmo local. Para quem busca uma visão mais estruturada e modularizada, o projeto original apresenta uma abordagem diferente, com separação de responsabilidades entre arquivos.

Esperamos que este README seja útil para explorar e utilizar esta implementação alternativa. Se houver dúvidas, o código é bastante acessível e autoexplicativo.