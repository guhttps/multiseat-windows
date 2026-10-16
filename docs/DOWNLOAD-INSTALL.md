# Download, instalação e primeiro uso

O **Multiseat Windows Prototype** é um painel experimental para organizar um plano local e testar dispositivos. Ele ainda não executa aplicativos externos ou jogos como estações independentes. Duas contas de League of Legends simultâneas não foram validadas.

## Disponibilidade

Versão planejada: **`v0.1.0-experimental.1`**. A publicação ainda precisa ser confirmada em [Releases](https://github.com/guhttps/multiseat-windows/releases). Os nomes abaixo descrevem os arquivos preparados para essa versão; use os downloads somente quando aparecerem no release público:

| Arquivo | Uso |
|---|---|
| `MultiseatPrototype-0.1.0-experimental.1-windows-x64-installer.exe` | Instala o painel e cria um atalho no menu Iniciar |
| `MultiseatPrototype-0.1.0-experimental.1-windows-x64.zip` | Versão que pode ser extraída e aberta sem instalador |
| `SHA256SUMS.txt` | Hashes para conferir a integridade dos arquivos publicados |
| `release-manifest.json` | Identifica a versão e os arquivos da distribuição |

Se a versão ainda não aparecer, siga [Compilar o código](START-HERE.md#compilar-o-código). O repositório público, por si só, não significa que já existe um instalador disponível.

## Requisitos

- PC Windows x64, com processador Intel ou AMD; esta edição não é para ARM64.
- .NET Framework 4.x disponível no Windows.
- Para o teste de dois pares: dois teclados e dois mouses. Dois monitores permitem observar os painéis em telas diferentes.

O alvo do projeto é Windows 11. O instalador permite Windows 10 x64 a partir da versão 2004; compatibilidade completa com cada versão de Windows, GPU e aplicativo continua em validação. Consulte as notas da versão. Para abrir o painel, não é necessário instalar Visual Studio, CMake, driver ou serviço.

## Conferir o arquivo baixado

Baixe o pacote e `SHA256SUMS.txt` do mesmo release em `github.com/guhttps/multiseat-windows`. Na pasta em que salvou o instalador, abra o PowerShell e execute:

```powershell
Get-FileHash '.\MultiseatPrototype-0.1.0-experimental.1-windows-x64-installer.exe' -Algorithm SHA256
```

Compare o campo **Hash** com a linha do mesmo arquivo em `SHA256SUMS.txt`. Para o ZIP, troque o nome no comando. Se os valores forem diferentes, descarte esse download e confira a origem antes de tentar novamente.

Esta entrega experimental ainda não tem assinatura de código. O Windows pode mostrar editor desconhecido ou SmartScreen; a conferência de hash verifica integridade em relação ao valor publicado, não substitui uma assinatura do editor. Não desative proteções do Windows para testar o painel.

## Instalar

1. Abra o instalador que você conferiu.
2. Leia o aviso sobre o estado experimental e conclua a instalação por usuário.
3. Abra **Multiseat Windows Prototype** pelo menu Iniciar ou pela opção ao final do instalador.

O local padrão é `%LOCALAPPDATA%\Programs\MultiseatPrototype`. O instalador usa a conta atual e não precisa de administrador. O pacote contém o painel e diagnósticos locais; não instala um motor de estações, drivers, serviços ou os experimentos administrativos do repositório.

## Usar o ZIP

1. Extraia o ZIP inteiro para uma pasta de sua escolha.
2. Mantenha os arquivos juntos e abra `MultiseatPrototype.exe` na pasta extraída.
3. Se usar os testes, deixe os executáveis de diagnóstico que acompanham o painel na mesma pasta.

O ZIP e a instalação usam o mesmo rascunho do perfil Windows atual. Abrir o executável de dentro do ZIP, sem extrair todos os arquivos, pode deixar os testes indisponíveis.

## Primeiro uso

1. Clique em **Identificar monitores** para ver a numeração nas telas.
2. Escolha um monitor para cada estação no painel.
3. Na estação 1, clique em **Associar teclado + mouse**. Com o painel em foco, pressione **F6** no teclado escolhido e clique com o botão direito do mouse escolhido dentro do painel. Repita para a estação 2.
4. Escolha as saídas de áudio no plano, se disponíveis. Isso ainda não altera a saída dos aplicativos.
5. Clique em **Salvar configuração** para guardar o rascunho. Você pode manter um plano parcial e completar depois.
6. Clique em **Testar dois pares** para abrir os painéis próprios. Siga a associação pedida nessa janela; o teste ainda não lê as seleções salvas no painel principal.

**Iniciar postos** fica desabilitado nesta versão. O teste demonstra eventos separados dentro de painéis do projeto; não demonstra dois usuários Windows ou dois jogos independentes. Fechar um painel de teste encerra a demonstração. Os diagnósticos **Diagnóstico de ambiente** e **Sessões e monitores** mostram observações locais, sem habilitar RDP ou criar novas sessões.

Depois de trocar dispositivos, cabos, resolução ou topologia, clique em **Atualizar dispositivos** e revise as seleções antes de salvar. A configuração desta versão é um plano de pesquisa, sem garantia de identidade permanente de cada monitor físico.

## Configuração local

O arquivo de seleções fica em `%LOCALAPPDATA%\MultiseatPrototype\settings.json`. Ele guarda o plano das duas estações e identificadores técnicos dos dispositivos em forma de hashes. O painel não grava senhas nem o conteúdo digitado; a associação usa F6 e clique direito enquanto o próprio painel está em foco.

Os identificadores continuam sendo dados locais do equipamento. Não publique esse arquivo, relatórios brutos ou capturas com dados pessoais em Issues. Relate a ação, o resultado esperado e a mensagem observada, após revisar o texto.

Se o arquivo não puder ser carregado, o painel preserva o original e informa a falha. Faça uma cópia local antes de substituir uma configuração que queira conservar.

## Remover

Para a versão instalada, use **Configurações do Windows → Aplicativos**, selecione **Multiseat Windows Prototype** e escolha **Desinstalar**. Os arquivos instalados e o atalho são removidos; o rascunho local é preservado.

Para a versão ZIP, feche o painel e os testes antes de excluir a pasta extraída. Isso também preserva o rascunho no perfil Windows.

Se quiser apagar suas seleções, feche o aplicativo, abra `%LOCALAPPDATA%\MultiseatPrototype` no Explorador e exclua apenas `settings.json`. Na próxima abertura, o painel começa com um plano vazio.

## Próximo passo do projeto

O trabalho principal continua sendo o [motor nativo](architecture/0007-native-engine-gate.md): duas contas, monitores físicos, foco, entrada e GPU simultâneos, com recuperação verificável. Veja o [estado do projeto](STATUS.md), o [roteiro de desenvolvimento](DEVELOPMENT-NEXT.md) e a [política de segurança](../SECURITY.md).
