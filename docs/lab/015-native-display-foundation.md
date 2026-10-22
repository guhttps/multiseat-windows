# Laboratório 015 — base do backend gráfico nativo

Atualizado em 2026-10-22. **Resultado: leitor de texturas testado e candidata de driver compilada; display de uma sessão nativa ainda não demonstrado.** Este trabalho continua a [admissão local](014-native-local-admission.md) e mantém aberto o [gate nativo](../architecture/0007-native-engine-gate.md).

## Diagnóstico que orientou a implementação

A rodada seguinte confirmou novamente a identidade do cliente no serviço real e registrou a falha E_POINTER em OnConnected. O rastreamento de QueryInterface mostrou consultas às interfaces públicas IWRdsWddmIddProps e IWRdsWddmIddProps1, ainda não fornecidas pela candidata. A análise estática da imagem exata do serviço encontrou um ramo que retorna E_POINTER quando o modo WDDM IDD está habilitado e ambas as interfaces estão ausentes. Essa correspondência orienta a investigação do backend gráfico; não é uma pilha capturada da falha nem prova completa de sua causa. O console foi preservado e a restauração externa da VM passou.

A interface [IWRdsWddmIddProps](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdswddmiddprops) permite indicar um driver próprio e receber seu handle. Ela não fornece esse driver nem resolve entrada, autenticação ou saída física. O componente continua recusando capacidades que não consegue entregar; nenhuma consulta foi transformada em sucesso fictício para avançar a conexão.

## Código reutilizável e teste automático

A base pública está em [engine/native/display](../../engine/native/display/README.md). O teste CTest `native-frame-readback` passou em Release, com 25 verificações sobre texturas geradas pelo próprio teste em D3D11 WARP:

- Cópia real para memória de leitura da CPU e checksum dos pixels úteis, sem preenchimento das linhas.
- Mudança do conteúdo, troca entre BGRA8 linear e sRGB e mudança das dimensões da textura temporária.
- Recusa de textura de outro dispositivo, formato incompatível, imagem acima do limite e argumentos ausentes.
- Cancelamento e prazo vencido com resultado vazio; sequência incrementada somente nas cópias concluídas.

O limite inicial é 1280 × 720, sem MSAA. O resultado contém somente dimensões, sequência e checksum de diagnóstico; não apresenta ou exporta a imagem. As 14 verificações CTest da configuração comum e as 16 com o cliente RDS opcional passaram localmente. Esses testes não abrem sessões RDP nem capturam a área de trabalho.

## Candidata de driver, ainda sem carregamento

O WDK foi obtido de pacote oficial da Microsoft com assinatura verificada, sem instalar drivers no host. Uma candidata privada UMDF 2.33/IddCx 1.4 compilou com avisos tratados como erros e usa o mesmo leitor público. A implementação inclui inicialização do adaptador, modo virtual único, configuração inicial do display remoto, aquisição e conclusão de frames e encerramento do trabalhador. Os testes isolados do trabalhador usaram D3D11 WARP; outras 51 verificações cobriram autorização/permissões/arquivos e 16 cobriram associação, parada reentrante e disputa de gerações.

A revisão corrigiu a ausência de USE_SMALLEST_MODE e da chamada de configuração inicial. A [documentação de IDDs remotos](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds) exige aquela flag junto de REMOTE_SESSION_DRIVER e distingue chegada de monitor de ativação do caminho. A implementação usa a função atual [IddCxAdapterDisplayConfigUpdate](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadapterdisplayconfigupdate). Somente o caminho remoto criado pelo Windows pode inicializar esse tipo de adaptador; criar manualmente um dispositivo de console não substitui essa integração.

A candidata confere autorização temporária, arquivo protegido e identificador de sessão do dispositivo. [DEVPKEY_Device_SessionId](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/devpkey-device-sessionid) descreve o alcance de acesso do dispositivo; sua presença sozinha não comprova propriedade de um display nem isolamento de entrada. O encerramento por energia/remoção é conservador, e retomada no mesmo dispositivo ainda não está qualificada.

O INF passou nas verificações `/w` e `/h` do InfVerif do WDK. Isso valida o arquivo de instalação segundo essas regras; não é assinatura, certificação, instalação ou teste do driver em execução. Não foram criados catálogo ou certificado, não houve mudança de configuração de boot, instalação, carregamento, monitor virtual ou frame de desktop real. Código operacional, autorizações, credenciais e relatórios brutos permanecem fora do Git.

## Próxima prova necessária

Preparar e revisar assinatura de laboratório, recuperação e integração do pacote próprio; ligar a capacidade do provedor somente após resolver posse dos handles, sessão, geração e drenagem de I/O. A prova positiva deve conferir o dispositivo criado pelo Windows para a sessão correta, monitor ativo, frames reais e encerramento/restauração completos. A candidata não pode receber aprovação apenas porque compila ou porque o INF é válido.

Depois disso ainda faltam autenticação de outra conta, entrada por sessão e apresentação nos monitores físicos. Um IDD remoto cria um display virtual da sessão; não demonstra atribuição das saídas HDMI da GPU. Áudio, aceleração/desempenho, LoL, interface integrada e instalador do motor permanecem na [lista de entregas](../REMAINING-WORK.md).
