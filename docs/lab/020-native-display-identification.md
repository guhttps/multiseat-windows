# Laboratório 020: identificação do pacote de display nativo

Atualizado em 2026-11-05. **Candidata privada implementada e qualificada localmente; sem execução desta candidata no serviço real da VM.** Ainda não há driver de display carregado, frames de desktop, segunda conta autenticada ou dois terminais nativos utilizáveis.

## Problema e mudança

O [laboratório 019](019-native-input-backend.md) observou a entrega dos dois canais reais de entrada ao serviço. A conexão parou na chamada seguinte, `GetHardwareId`, com `ERROR_NOT_READY`. Informar um texto constante sem conferir o pacote não demonstraria disponibilidade do driver nem segurança do vínculo.

A candidata liga `GetHardwareId` a uma ponte pertencente à conexão. Exige a autoridade composta autenticada do serviço e a atribuição viva recebida em `NotifySessionId`. Antes de publicar o identificador próprio, confere um registro protegido da implantação e o pacote efetivamente armazenado no DriverStore. O produtor desse registro ainda precisa ser integrado ao roteiro de preparação da VM; este laboratório não anuncia sucesso da chamada real.

## Autorização e ciclo de vida

- Registro com esquema, versão, tamanho e campos reservados fixos; mesma execução e provisionamento; processo real do serviço, instante de criação, imagem, token e prazos conferidos. Não contém credenciais nem concede direitos no Windows.
- Diretório e arquivo protegidos, proprietário administrativo e leitura limitada ao serviço; recusa de redirecionamentos, múltiplos vínculos e substituição de caminhos. Os handles conferidos permanecem retidos durante o uso.
- INF publicado resolvido por SetupAPI para o pacote próprio; DLL e catálogo conferidos por hash integral e certificado fixado. O INF é hasheado pelo mesmo handle retido. A confiança usa o cache local do Windows e não comprova atualização de revogação de certificados.
- Sessão e autorização revalidadas durante a conferência. O identificador publicado é próprio do projeto; nenhum identificador de hardware pessoal é publicado.
- Capacidade do buffer medida em elementos `WCHAR`, incluindo o terminador. Em falha, somente o primeiro elemento disponível é limpo; buffer insuficiente não inicia a conferência do pacote.
- Encerramento sinaliza cancelamento antes de aguardar a operação e drena a conferência antes de liberar os contextos emprestados. Um descarregamento de outra sessão não revoga os recursos desta conexão.

A conferência é somente de leitura: não instala pacote, cria licença ou dispositivo, modifica certificados ou altera a segurança do host. Chamadas de confiança do Windows não são forçadamente interrompíveis; os ensaios na VM continuam exigindo recuperação externa.

## Contrato do display

A candidata usa `IWRdsWddmIddProps1` do SDK 10.0.26100.0. Seu `OnDriverLoad` recebe `ULONG` e **texto emprestado `LPCWSTR` da instância**, conforme o header efetivamente compilado. Esse texto não é um handle e não deve ser fechado ou reinterpretado como um. A [projeção da interface da Microsoft](https://microsoft.github.io/windows-docs-rs/doc/windows/Win32/System/RemoteDesktop/struct.IWRdsWddmIddProps1.html) também distingue esse contrato.

O carregamento continua recusado com `ERROR_NOT_READY`. Esta candidata ainda não fornece a autorização separada de início do driver nem o canal da instância ou frames. As [regras de IDDs remotos](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds) exigem que a criação pelo Windows e as opções do adaptador correspondam à sessão remota; criar um dispositivo no console não substitui essa condição.

## Verificações locais concluídas

| Verificação | Resultado | Limite |
| --- | --- | --- |
| Identificação, registro, buffers e ciclo de vida | 62 verificações, zero falhas | Sucessos de pacote usam fixture explícita em executável separado |
| Revogação, conflito e cancelamento em andamento | Aprovados na mesma suíte | Não comprovam o prazo de chamadas de confiança do Windows |
| Fábrica de produção no processo comum do host | Recusa aprovada | Nenhum pacote ou driver foi aberto positivamente no host |
| Contratos da DLL do provedor | Aprovados | Preservam as recusas quando a ponte não está vinculada |
| Atribuição de sessão na DLL | 26 verificações, zero falhas | Teste local, separado da observação real do laboratório 019 |
| Compilação e análise estática | Aprovadas com avisos como erros | Não executam o driver |

O executável de testes usa I/O de fixture. A DLL fixa a fábrica de conferência real e não oferece uma chave para selecionar a fixture, um pacote falso ou um caminho arbitrário. O recibo registra fontes, dependências, binários, compilador e header RDS utilizados; referências congeladas foram reconferidas.

A [atualização para FreeRDP 3.33.0](../FREERDP-UPDATE-20261105.md) passou separadamente em um controle limitado com o serviço real e o provedor anterior congelado: identidade, fatos, atribuição e recusa esperada da entrada foram observados, com restauração e auditoria independente aprovadas. Esse controle de dependência **não executou o provedor deste laboratório** e não valida o display.

## Próxima prova

1. Integrar o produtor do registro protegido à preparação do pacote real e assinado na VM, usando fatos da implantação e a execução de uso único. Preparar o pacote antes do listener, preservando os limites originais de recuperação.
2. Executar nova rodada finita e conferir a chamada real de `GetHardwareId`: mesma identidade e sessão, conferência efetiva do pacote, retorno observado e próxima falha preservada. Um trace de preparação não equivale ao retorno nem ao carregamento.
3. Implementar a autorização do driver para a sessão efetivamente notificada, com produtor administrativo limitado e transporte autenticado. Preservar as permissões e condições de início do driver.
4. Integrar `OnDriverLoad` à observação real de instância, sessão, pacote e interface, usando canal próprio com cancelamento. Depois comprovar adapter/monitor, frames e consumo nativo da entrada.

Autenticação da segunda conta, saída física, áudio, jogos e distribuição do motor continuam pendentes no [gate nativo](../architecture/0007-native-engine-gate.md). Candidata, resultados brutos e binários não foram incluídos no instalador público; os checks públicos desta documentação não compilam o código privado.
