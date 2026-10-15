# Directory Open — instruções de desenvolvimento

O nome definitivo é **Directory Open**, forma curta **DirOpen** e executável
**DirOpen.exe**. O nome antigo fica restrito a referências históricas e identificadores
técnicos de compatibilidade. Se CONTEXTO.md local estiver presente, leia-o primeiro
para retomar o estado atual; não o inclua em commits ou publicações.

Aplicativo próprio para Windows em C++20/Qt Widgets. Consultar README.md,
docs/COMPILAR.md, docs/ARQUITETURA.md e CONTRIBUTING.md conforme o escopo.
Preservar recursos existentes, dados reais e compatibilidade de sessão/clipboard.

- Trabalhar com fixtures e --no-session ou --session-file em verificações de GUI.
- Modelos permanecem somente leitura; alterações passam pelo motor nativo e confirmação.
- Lixeira não tem alternativa de exclusão definitiva. Não remover itens do aplicativo
  de origem depois de um arraste já movido pelo motor.
- Identidade, ausência, resultado parcial e erro são diferentes. Não reinterpretar
  dados brutos nos consumidores; corrigir o produtor e o proprietário dos fatos.
- Não abandonar threads/recursos no encerramento. Preservar identidade para limpeza.
- Decodificação fora da UI; filas, cache, resultados e serialização limitados.
- Não ler clipboard dentro de dataChanged. Conservar tokens e sequência nativa.
- Mudanças de configuração devem ser aditivas, idempotentes e com backup.
- Executar checks relevantes; descrever cobertura e omissões reais. Mocks não provam
  comportamento de Windows.Data.Pdf, IFileOperation ou clipboard nativo.
- Medir tempo/CPU/pico de memória em mudanças de desempenho; rotular frio/aquecido,
  estimativas e custos não medidos. Não generalizar a partir de bytes ou um único caso.
- Atualizar documentação/changelog na etapa adequada. Usar Conventional Commits.
- Não versionar builds, executáveis, bibliotecas, credenciais, sessões pessoais ou
  artefatos de investigação locais. Capturas de documentação usam dados de demonstração.
- Não copiar código/imagens/licença/configuração do Directory Opus.
- build.cmd não instala ferramentas nem muda configurações globais.

REA e ferramentas de investigação não fazem parte deste repositório.
