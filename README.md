# Análise Temporal de Tarefas - Linux

Análise temporal criteriosa de tarefas desenvolvidas em um sistema baseado em **Pthreads no Linux**. O objetivo principal é avaliar o desempenho das tasks relacionadas ao **ABS** e **Airbag**, medindo tempos críticos como o **WCET** (Worst Case Execution Time) e o **WCRT** (Worst Case Response Time), além de verificar o conceito de **(m,k)-firme**. Link do [vídeo de demonstração do funcionamento do código e análise temporal.](https://youtu.be/6yXkmLEop2Y)

---

## **Problema**
A análise foi conduzida com base no seguinte problema:
- Avaliar a resposta temporal das tarefas de **ABS** e **Airbag**, considerando:
  - **Tempo de execução.**
  - **Tempo de interferência.**
  - **Período de ocorrência** (periódico, esporádico ou aperiódico).
  - **Deadline.**
- Verificar o cumprimento de deadlines e firmeza do sistema.
- Implementar uma task/thread adicional que simula a atualização do display, embora esta não tenha sido analisada.

---

## **Metodologia**
A análise seguiu os seguintes passos:

1. **Especificação do Modelo de Tarefa:**
   - **Tempo de Execução:** Medido para cada tarefa em diferentes cenários.
   - **Tempo de Interferência:** Determinado pela análise de prioridades e contexto de execução.
   - **Período de Ocorrência:** Classificação das tarefas como periódicas.
   - **Deadline:** Prazos de execução definidos para cada tarefa.

2. **Medições Realizadas:**
   - **WCET (Worst Case Execution Time):** Obtido para os piores cenários.
   - **WCRT (Worst Case Response Time):** Inclui o tempo de execução e interferência.

3. **Hardware Considerado:**
   - Sistema operacional Linux com suporte a Pthreads.
   - Impactos de hardware simulados: memória, CPU e latência.

4. **Abordagem de Firmeza:**
   - Aplicado o conceito de (m,k)-firme com uma janela de 10 ativações.
   - Identificado o **HWM (High Water Mark)** de 100%.
   - Avaliado o fator skip entre perdas de deadlines.

5. **Ferramentas Utilizadas:**
   - Medições realizadas usando bibliotecas de temporização e threads em Linux.
   - Scripts para automação de coleta de dados e geração de gráficos.
