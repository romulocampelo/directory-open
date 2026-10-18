# Contribuir para o Directory Open

Sugestões, relatos de problemas e melhorias pequenas são bem-vindos. Consulte o
[manual](docs/USO.md), o [roadmap](ROADMAP.md) e a [arquitetura](docs/ARQUITETURA.md)
para entender o comportamento existente antes de alterar os fluxos.

## Relatar um problema

Informe versão, Windows/Qt quando conhecido, passos para reproduzir, resultado
esperado e observado. Use arquivos de demonstração. Confira logs e capturas antes
de compartilhar, retirando dados pessoais desnecessários. Em uma operação de
arquivos, explique se houve erro, cancelamento ou resultado parcial.

## Alterar o código

1. Crie uma branch para a mudança e mantenha o escopo pequeno.
2. Preserve recursos existentes, sessões legadas e caminhos indisponíveis.
3. Operações próprias de arquivos passam pelo motor nativo e pela confirmação. Os modelos
   ficam somente leitura; cancelamento não desfaz itens já concluídos.
   Comandos explicitamente escolhidos no menu do Shell podem ser delegados ao
   Windows ou a extensões, com execução/confirmações externas e sem histórico próprio.
   Cópia TeraCopy pode ser delegada explicitamente, sem presumir conclusão.
   Backup restic e cópia rclone têm confirmação própria na toolsdialog e família
   de processos supervisionada no adaptador specializedtools. Restaurar em pasta
   nova com verificação; rclone somente copy com prévia e proteção --immutable.
   Senhas não pertencem à sessão, aos argumentos, aos logs ou ao Git.
   ExifTool/ImageMagick/FFmpeg passam por mediatools e confirmação da prévia;
   somente saídas novas, originais preservados, origem revalidada sob bloqueio,
   marca de origem preservada e publicação após verificação. Limpeza pendente é
   reportada com caminho; não ampliar esse contrato para edição direta silenciosa.
   ZIP/7z usam o adaptador archive com processo supervisionado e escrita de fluxos
   por arquivo em destino novo. Não trocar esse fluxo por extração direta `-o`
   em dados do usuário; conservar validação de nomes, reserva dos diretórios,
   marca de origem e publicação sem substituição.
4. Teste com fixtures e --no-session ou --session-file. Não usar documentos ou
   sessões pessoais para testar copiar/mover/Lixeira.
5. Execute as verificações relevantes de [COMPILAR.md](docs/COMPILAR.md). Para
   clipboard, PDF ou GUI, descreva também a verificação nativa feita.
6. Em mudanças de custo, meça tempo, CPU e pico de memória em tarefas equivalentes;
   diferencie primeira carga, cache aquecido, estimativas e o não medido.

Corrija o produtor e a propriedade dos dados antes de acrescentar contornos nos
consumidores. Não abandonar trabalhadores; aguardar seu encerramento e manter a
responsabilidade por recursos cuja limpeza falhou. Identidade, ausência, resultado
parcial e erro precisam permanecer distintos.

## Commits e pull requests

Use assuntos como feat: add folder comparison ou fix: preserve viewer focus.
Descreva problema, comportamento resultante e validação. Indique limitações e
verificações omitidas. Mocks não comprovam comportamento real de APIs nativas.

Não versionar builds, executáveis, bibliotecas compiladas, caches, logs de teste,
configurações pessoais ou instaladores de outros produtos. Capturas públicas usam
dados de demonstração. O código deve ser próprio ou ter origem e licença compatível
documentadas; não importar código ou imagens do Directory Opus.
