# Portable Inspection Edge Node

## Como compilar e executar (Linux)
Pré-requisitos:
- g++ com suporte a C++17
- cmake
- make

Passos:
```bash
git clone https://github.com/kamatashi/Portable-Inspection-Edge-Node.git
cd Portable-Inspection-Edge-Node
mkdir -p build && cd build
cmake ..
make
./SimulateSystem


## Prova de conceito: levantamento com sinalização de padrões
Um aparelho acoplado a um veículo (ex.: drone) fotografa uma área de vários ângulos. Um classificador de visão leve (LLM) procura **padrões configuráveis** em cada imagem; as imagens em que algum padrão é encontrado recebem uma *flag* e só elas seguem, com o relatório, para a máquina do usuário via web.

O código é agnóstico de domínio: os padrões vêm de um catálogo (`id|label|description`), então o mesmo núcleo serve para uma residência ou uma zona rural.

- `src/Survey/Pattern.h`: catálogo de padrões
- `src/Survey/Survey.h/.cpp`: pipeline (classifica, aplica limiar, agrupa flags, gera JSON) e as interfaces `IVisionClassifier` e `IUplink`
- `src/Mocks/ScriptedClassifier.h`: classificador determinístico e envio em memória (substituíveis por LLM e HTTP reais)
- `src/survey_simulation.cpp`: simulação (`./SurveySimulation`)
