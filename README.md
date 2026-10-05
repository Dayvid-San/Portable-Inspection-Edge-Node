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
- `src/Survey/ClaudeClassifier.h/.cpp`: classificador que chama a Messages API (imagem + saída estruturada); o transporte HTTP é injetável
- `src/Survey/CurlTransport.h`: transporte via `curl` (sem dependências)
- `src/Mocks/ScriptedClassifier.h`: classificador determinístico e envio em memória (substituíveis por LLM e HTTP reais)
- `src/survey_simulation.cpp`: simulação (`./SurveySimulation`)

### Usando o classificador real
```bash
export ANTHROPIC_API_KEY=...        # sem a chave, a simulação usa o classificador roteirizado
export CLASSIFIER_MODEL=claude-haiku-4-5   # opcional (padrão: claude-opus-5-5)
export CLASSIFIER_FALLBACK=0               # opcional: desliga o fallback de recusas (necessário em modelos que não o suportam)
cd build && ./SurveySimulation
```
Se a classificação de uma imagem falha (rede, limite de uso, recusa), ela aparece em `images_failed` no relatório e nunca é tratada como "nada encontrado".
