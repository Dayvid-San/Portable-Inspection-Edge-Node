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

O código é agnóstico de domínio: os padrões vêm de um catálogo (JSON `[{"id","label","description"}]` ou linhas `id|label|description`), então o mesmo núcleo serve para uma residência ou uma zona rural.

- `src/Survey/Pattern.h`: catálogo de padrões
- `src/Survey/Survey.h/.cpp`: pipeline (classifica, aplica limiar, agrupa flags e zonas, gera JSON) e as interfaces `IVisionClassifier` e `IUplink`
- `src/Survey/BudgetedClassifier.h`: teto de chamadas e parada após falhas seguidas (evita gastar sem perceber)
- `src/Survey/HttpUplink.h`: envio por HTTP POST com Bearer token e novas tentativas
- `src/Survey/ReportQueue.h`: fila em disco para operar offline (`ResilientUplink`)
- `tools/receiver.py`: receptor de teste (a "máquina do usuário")
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
Se a classificação de uma imagem falha (rede, limite de uso, recusa), ela aparece em `images_failed` no relatório e nunca é tratada como "nada encontrado". O `BudgetedClassifier` limita a 200 chamadas por levantamento e para após 3 falhas seguidas; as imagens barradas também entram em `images_failed`.

### Zonas
Se as fotos têm posição (`Pose::has_position`), as flags do mesmo padrão a até `zoneRadiusM` metros (padrão 25) viram uma zona no relatório (`zones`), com centroide e confiança máxima. Fotos sem posição não entram em zonas.

### Enviando para a máquina do usuário
```bash
# na máquina do usuário (escuta só em 127.0.0.1; ponha atrás de HTTPS para uso real)
UPLINK_TOKEN=segredo python3 tools/receiver.py 8080 received

# no dispositivo / simulação
UPLINK_URL=http://127.0.0.1:8080/api/ingest-scan UPLINK_TOKEN=segredo ./SurveySimulation
```
O envio leva só as imagens sinalizadas, em base64. Se o envio falha (rede, 5xx, 429), o relatório vai para uma fila em disco (`UPLINK_QUEUE_DIR`, padrão `./uplink_queue`) e é reenviado, em ordem, no próximo envio. Erro de credencial (401/403) mantém o relatório na fila para quando o token for corrigido; um relatório que o servidor recusa de vez (400, 413...) vai para `rejected/` dentro da fila, sem se perder e sem bloquear os seguintes. Sem `UPLINK_URL`, a simulação só imprime o relatório.

O receptor de teste não é um servidor de produção: ele valida o token e o tamanho, mas não faz TLS nem limite de requisições.

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
- `--threshold T` mostra também o resultado para um limiar fixo: escolha o limiar num conjunto de **validação** e aplique no de **teste**, para não medir com o mesmo dado que escolheu o limiar.
- Conjuntos abertos candidatos: BD3 (IISc), BFDD (Mendeley), SDNET2018, entre outros. Confirme a licença na página de cada um antes de usar.

### Resultado da linha de base (sem LLM)
Conjunto: [`mohammadnajeeb/concrete_crack_images`](https://huggingface.co/datasets/mohammadnajeeb/concrete_crack_images) (CC BY 4.0; fissuras em concreto, imagens pequenas de perto), partições `validation` e `test` com 4000 positivas e 4000 negativas cada. Limiar de densidade de bordas escolhido na validação (0,033) e aplicado no teste:

| AUC (teste) | precisão | recall | F1 |
|---|---|---|---|
| 0,976 | 0,923 | 0,968 | 0,945 |

Só a densidade de bordas já separa bem este conjunto, porque as fissuras são nítidas e de perto. Ele serve para validar a ferramenta, mas **não** discrimina um LLM de um filtro simples: para isso precisamos de um conjunto mais difícil (defeitos variados, fotos mais distantes, superfícies sujas), como o BD3.

## Testes
```bash
cd build && cmake .. && make && ctest
```
Usa o GoogleTest do sistema (`apt install libgtest-dev`) e só baixa se não houver.
