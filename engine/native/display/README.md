# Leitura de texturas do motor nativo

`FrameReadback` lê uma textura D3D11 já fornecida pelo chamador, usando uma cópia para memória de leitura da CPU. O resultado contém dimensões, sequência e checksum de diagnóstico dos pixels, excluindo o preenchimento das linhas. Não contém a imagem. O checksum não autentica dados.

Esta base aceita BGRA8 linear ou sRGB, uma superfície sem MSAA e dimensões de até 1280 × 720. O dispositivo da textura precisa ser o mesmo do leitor. Falha, cancelamento ou prazo vencido limpam o resultado e não avançam a sequência. O chamador mantém textura e evento de cancelamento válidos e serializa as chamadas e o uso do contexto imediato D3D11. Chamadas ao driver gráfico podem bloquear; as verificações de prazo não garantem interrupção de um driver com falha.

O módulo não obtém desktops, inicia sessões, carrega drivers, apresenta imagem nos monitores ou encaminha entrada. É uma parte do backend gráfico em desenvolvimento; não aprova o [gate do motor](../../../docs/architecture/0007-native-engine-gate.md). O [laboratório 015](../../../docs/lab/015-native-display-foundation.md) distingue essa prova da integração ainda pendente.

Na Developer PowerShell x64, na raiz do repositório:

```powershell
cmake -S . -B build/native-display -A x64 -DBUILD_TESTING=ON
cmake --build build/native-display --config Release
ctest --test-dir build/native-display -C Release -R native-frame-readback --output-on-failure
```

O teste gera suas próprias texturas no renderizador de software WARP. Confere mudança real de pixels, formato, dimensões, sequência, cancelamento e recusa de textura de outro dispositivo. Não captura a tela, abre conexão ou muda a configuração do Windows. Basta o Windows SDK; esse teste não precisa do WDK, de VM ou de administrador.
