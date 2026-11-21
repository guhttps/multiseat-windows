# Laboratório 024: driver de display, tela de login da sessão 2 e login da segunda conta

Atualizado em 2026-11-20. **Marco: em VM com Windows 11 cliente, uma conexão do nosso provedor levou uma nova sessão até a tela de login do Windows, enquanto o console continuava com o usuário dele conectado.** Os processos `LogonUI.exe`, `dwm.exe`, `fontdrvhost.exe`, `winlogon.exe` e `csrss.exe` rodaram na sessão 2. Para isso foi usado o driver de display remoto que já vem no Windows (`RdpIdd_IndirectDisplay`). O nosso próprio driver ainda não inicia, e a segunda conta ainda não entrou. Nenhuma rodada aprovou um terminal utilizável. Todas foram restauradas, com auditoria independente.

## Resumo das rodadas

| Rodada | Mudança | Resultado |
| --- | --- | --- |
| V38 | Produtor SYSTEM da autorização de início do driver, com a sessão real observada via WTS, sem adivinhar o número; `OnDriverLoad` aceito só para a sessão atribuída | Aprovada no escopo; autorização gravada para a sessão 2; desconexão continua `0x11` |
| V39 | Diagnóstico somente leitura de dispositivos, eventos e `setupapi` | O Windows cria `SWD\RemoteDisplayEnum\MsRemoteIddLab_IndirectDisplay&SessionId_0002` e instala o pacote; o dispositivo falha com `CM_PROB_FAILED_ADD` |
| V40, V40b | Driver v8 espera a autorização por até 4 s; verificação do login do console mais tolerante | A falha continua cerca de 118 ms após o início: a espera não é a causa |
| V41, V41b, V42 | Driver v9 registra as etapas; ajustes de tempo | Falhas de infraestrutura (saída 14 do cliente, orçamento de 120 s, limite de 120 s no predicado de leitura) |
| V43 | Guarda de 180 s aprovado pelo usuário (biblioteca de recuperação v2, com uma única linha diferente) | Log UMDF: falha "ao carregar o driver no nível 0", `STATUS_WDF_BUSY` |
| V44 | Driver v10 grava as etapas de `DriverEntry` | `DriverEntry` roda, a autorização é aceita e `WdfDriverCreate` dá sucesso; a falha acontece antes de `DeviceAdd` |
| V45 | INF alinhado ao driver remoto da caixa (`rdpidd.inf`) | Sem mudança |
| V46 | Provedor informa o identificador do driver remoto da caixa | O driver da caixa carrega; `OnDriverLoad` é aceito para a sessão 2 e a desconexão passa a ter motivo 0 |
| V47, V48 | Pausa limitada depois do carregamento do driver, reavaliada a cada volta na V48 | **V48: sessão 2 na tela de login** (LogonUI e dwm), com o console preservado |
| V49, V50 | Conta fictícia por rodada, credenciais entregues via `GetClientData` e `GetUserCredentials`; listener com `fInheritAutoLogon=1` e `fPromptForPassword=0` | Sem login automático: apenas a tela de login |
| V51 | Diagnóstico de logs de segurança e do Winlogon | Só os logins internos da sessão 2 (`DWM-2`, `UMFD-2`); nenhuma tentativa de login da conta fictícia |

Os resultados reprovados foram preservados como reprovados. As rodadas V36 a V51 seguem o mesmo padrão:
- plano novo fixado por hash;
- restauração obrigatória;
- auditoria independente;
- console e processos centrais das sessões existentes verificados.

O guarda de recuperação passou de 120 para 180 segundos com aprovação explícita do usuário. A biblioteca de recuperação v2 é uma cópia idêntica, exceto pela linha do limite; a versão original continua intacta.

## O que foi demonstrado

- O caminho do provedor está correto: um driver de display remoto que funciona é carregado para a sessão atribuída, e `OnDriverLoad` é aceito.
- Uma sessão nova, criada pela nossa conexão, chega à tela de login do Windows 11 cliente com gráficos ativos, sem encerrar a sessão do console.

## O que não foi demonstrado

- O nosso driver IDD não inicia. Ele falha no framework entre `WdfDriverCreate` e `DeviceAdd` com `STATUS_WDF_BUSY`, e a causa ainda é desconhecida.
- Credenciais fornecidas pelo protocolo não produzem login automático. Nenhuma tentativa de login da segunda conta foi registrada.
- Ainda não existem: login de uma segunda conta, apresentação no segundo monitor físico, entrada roteada para a sessão 2, áudio, nem terminal utilizável.

## Próximos passos

O produto não depende de login automático: no multiseat, a segunda pessoa faz o login na tela de login com o próprio teclado. O próximo trabalho é levar a imagem e a entrada da sessão 2 até o segundo posto:

1. Manter a sessão 2 ativa pelo provedor, sem depender do cliente de teste.
2. Usar um agente local na sessão 2 para capturar a área de trabalho, incluindo a tela de login, e aplicar a entrada recebida.
3. No console, um apresentador mostra a imagem no segundo monitor e encaminha somente o segundo teclado e mouse.
4. Depois, comprovar o login interativo da segunda conta e a permanência das duas sessões ativas.

Em paralelo, continuar investigando o `STATUS_WDF_BUSY` do nosso driver.

Fontes, binários experimentais e resultados brutos continuam privados. A CI pública não compila a candidata privada. Consulte o [trabalho restante](../REMAINING-WORK.md).
