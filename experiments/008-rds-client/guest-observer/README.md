# Observador auxiliar no guest RDS

Experimento exclusivo para a VM isolada com Windows Server 2025 Desktop Experience. Não é um motor multiseat para Windows cliente. O executável não instala serviço, altera segurança, injeta entrada, usa clipboard ou requer administrador.

Compile com `Build-GuestObserver.ps1`. O build executa apenas `--self-test`, sem consultar hardware, ler o marker ou abrir aplicativos. A execução real deve ocorrer no logon de cada conta local não administrativa `LabSeatA`/`LabSeatB`:

```text
RdsLabObserver.exe --run-id <GUID-do-laboratorio>
```

O host prepara o autostart somente no guest. As duas contas precisam de travessia no diretório `C:\ProgramData\MultiseatRdsLab` e leitura no `lab-marker.json`, sem escrita nem acesso ao estado administrativo. O marker deve ter `schemaVersion: 1`, `purpose: "MultiseatRdsLab.RdsAuxiliary"` e o mesmo `runId`.

O observador abre um Notepad Win32 externo vazio e verifica o caminho do binário, token SID, sessão e PID iniciado. Só consulta o controle `Edit` desse PID, usando `SendMessageTimeout` com 250 ms e buffer de onze caracteres. Notepad com delegação para outro processo ou controle diferente falha fechado. O programa posiciona sua janela e tenta estabelecer o foco inicial; o cliente deve clicar dentro de `edit_rect` antes de digitar.

As entradas de teste são exatamente `rdp-seat-a` em A e `rdp-seat-b` em B, sem Enter. Outros documentos, títulos, teclas e conteúdo do clipboard não são coletados. Nenhum texto lido é persistido. A evidência contém somente estados, identificação privada do processo/sessão/token, horário e coordenadas em pixels do desktop do guest:

```text
%LOCALAPPDATA%\MultiseatRdsLab\<GUID>\observer.json
```

O relatório inclui `owner_validated`, `window_validated`, `edit_validated`, `document_empty_now`, `exact_fixture_now/seen`, `other_fixture_now/seen` e os retângulos `window_rect`/`edit_rect`. Antes do envio sintético, o coletor deve exigir consulta bem-sucedida e `document_empty_now`: a ausência de uma fixture exata também pode indicar texto parcial de uma tentativa anterior. O experimento usa uma sessão por conta. O coletor deve conferir RunId, SID esperado, sessão, atualização recente, ausência de `error_code` e os estados `notepad_exited`/`observer_finished`. O texto observado no aplicativo externo é a evidência; o retorno de uma API de envio de entrada não demonstra sucesso. Esses relatórios são privados do laboratório e não devem entrar no GitHub.

Para acompanhar digitação manual sem exigir uma frase fixa, o relatório também inclui `document_length_observed`, `document_character_count` e `document_length_capped`. A contagem usa o tamanho declarado pelo controle Unicode e satura em 256 unidades UTF-16. Apenas o tamanho é consultado para esse diagnóstico; a leitura de texto continua limitada às comparações das duas fixtures de dez caracteres. Leitura indisponível ou tamanho inválido deixa `document_length_observed=false` e não significa documento vazio. Para comparar mudanças de tamanho, exija amostras recentes, os mesmos processos/sessões e `document_length_capped=false`. A contagem isolada não identifica o texto nem comprova independência de foco; a prova sintética continua exigindo a fixture exata.

O observador termina ao fechar seu Notepad ou após duas horas. Ele não fecha aplicativos. Nenhuma prova de teclado/mouse físico, segurança entre estações ou compatibilidade com jogos é inferida deste experimento de transporte.
