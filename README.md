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

## Avaliação de classificadores
`EvalClassifier` mede um classificador num conjunto rotulado, com a estrutura `<dataset>/positive/*` e `<dataset>/negative/*`. Imprime AUC e, por limiar, TP/FP/FN/TN, precisão, recall e F1. Imagens cuja classificação falha são contadas à parte e ficam fora das métricas.

```bash
cd build && cmake .. && make EvalClassifier
./EvalClassifier /caminho/dataset --classifier edge --pattern crack --max-side 1280   # linha de base sem LLM
ANTHROPIC_API_KEY=... ./EvalClassifier /caminho/dataset --classifier claude --pattern crack
```
- `edge` é a linha de base sem LLM (densidade de bordas, `EdgeClassifier`): o LLM só vale a pena se ganhar claramente dela.
- `--max-side N` reduz a imagem (média de blocos) antes do processamento local. Para o `claude`, o arquivo original é enviado como está; formatos que a API não aceita (PGM, BMP) contam como falha.
- `tools/make_synthetic_dataset.py` gera um conjunto **sintético** só para validar a ferramenta. Os números dele não dizem nada sobre fotos reais.
- Conjuntos abertos candidatos para um teste real: BD3 (IISc), BFDD (Mendeley), SDNET2018, entre outros. Confirme a licença na página de cada um antes de usar.
