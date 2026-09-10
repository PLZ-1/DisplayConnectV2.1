# DisplayConnect — LOLIN32 Lite + ST7796S

Adaptação do [DisplayConnect, de malaq88](https://github.com/malaq88/DisplayConnect), para **LOLIN32 Lite e TFT SPI ST7796S de 3,5 polegadas**, em paisagem, com resolução de **480 × 320**.

O Android calcula a navegação e envia geometria por Bluetooth Low Energy. O ESP32 desenha o mapa vetorial, a posição e as instruções. A arquitetura e a licença MIT do projeto original foram preservadas.


## Alterações principais

- ST7796S em 480 × 320; mapa 480 × 232 e barra inferior de 88 pixels.
- Tema escuro, tempo e distância restantes no aplicativo e no display.
- Estatísticas nas configurações e remoção do botão de abrir Maps no navegador.
- Busca com cidade/estado, tratamento de abreviações, Nominatim e resultados complementares do Photon.
- Correções de conexão BLE, filas e mensagens atrasadas; maior capacidade de geometria.
- Projeção sem esticar os eixos, recorte nas bordas e distribuição das ruas pela tela.
- Download persistente das ruas por todo o percurso com margem para cobrir os cantos da tela.
- Servidores distintos para bicicleta, caminhada e carro. Moto ainda usa carro.
- Seleção de português/inglês e fonte com caracteres acentuados no ESP32.
- Filtros de idade, precisão e saltos do GPS; aviso de sinal fraco.

Consulte [as notas técnicas da 1.0.4](docs/ATUALIZACAO-1.0.4.md) e [as alterações no protocolo](docs/ADAPTACAO-PROTOCOLO.md).

## Organização

| Caminho | Conteúdo |
|---|---|
| `app/` | Aplicativo Android e testes |
| `DisplaySender/` | Firmware Arduino para o ESP32 |
| `hardware/User_Setup.h` | Configuração TFT_eSPI para o ST7796S |
| `docs/` | Documentação da adaptação |
| `docs/upstream/` | READMEs preservados do original; descrevem o CYD |

## Ligações

| Display | LOLIN32 Lite |
|---|---|
| SCK / CLK | GPIO18 |
| SDI / MOSI | GPIO23 |
| SDO / MISO | GPIO19 |
| CS | GPIO27 |
| DC / RS | GPIO26 |
| RST | GPIO25 |
| GND | GND |

Driver `ST7796_DRIVER`, SPI 27 MHz e rotação 1. Alimentação e iluminação dependem da placa adaptadora do módulo; não são controladas por GPIO neste código. Os sinais do ESP32 são de 3,3 V. O touch do CYD está desativado e o touch do novo módulo não foi implementado.

## Compilar o Android

Abra a raiz do repositório no Android Studio. Ambiente usado: JDK 21, SDK Android 36.1 e Gradle 9.4.1. O Android Studio configura o caminho local do SDK; `local.properties` não é versionado.

No Windows, execute `Compilar-App.cmd`. O comando compila, roda os testes unitários e gera `app/build/outputs/apk/debug/app-debug.apk`. É um APK de depuração, sem assinatura de distribuição.

Também é possível executar diretamente:

```powershell
.\gradlew.bat --no-daemon --console=plain :app:assembleDebug :app:testDebugUnitTest
```

## Compilar o ESP32

1. Instale o pacote ESP32 3.3.11 e selecione **WEMOS LOLIN32 Lite** (`esp32:esp32:lolin32-lite`).
2. Instale TFT_eSPI 2.5.43, NimBLE-Arduino 2.5.0 e ArduinoJson 7.4.2.
3. Faça backup da configuração da biblioteca TFT_eSPI e aplique `hardware/User_Setup.h` como `User_Setup.h` dessa biblioteca. Essa configuração também afeta outros sketches que usam a mesma instalação da biblioteca.
4. Copie `DisplaySender/config.h.example` para `DisplaySender/config.h`.
5. Abra `DisplaySender/DisplaySender.ino` no Arduino IDE e use **Verificar** e **Carregar**, selecionando a porta da placa.

`Compilar-ESP32.cmd` usa o Arduino CLI e as bibliotecas configuradas nele. Os resultados ficam em `hardware/firmware/`. O script apenas compila; não grava a placa. Para instalações diferentes, configure a variável `ARDUINO_CLI` com o caminho do executável. `Compilar-Tudo.cmd` chama os dois scripts em sequência.

Após gravar, o Serial a 115200 baud deve informar `TFT logical size: 480x320`. Atualize o app e o firmware juntos.

## Uso offline e limitações

Com internet, escolha o destino e o perfil, inicie a navegação e aguarde **Percurso pronto para usar offline**. Se houver falha, use a opção de retomada. As ruas salvas cobrem o percurso e a área visível com margem; na escala de 400 m isso exige cerca de 1.020 m de raio, devido à largura da tela.

GPS, BLE e a exibição da rota baixada podem funcionar sem internet. Não há cálculo de novas rotas offline nem recálculo automático. Para outro destino ou recálculo é necessário acesso ao serviço online. A última rota pode ser reutilizada com o mesmo destino/perfil, iniciando próximo ao traçado.

O mapa depende da cobertura do OpenStreetMap. Regiões muito densas ainda podem exceder o limite de 384 segmentos de ruas. O percurso tem limite de 512 áreas; áreas antigas fora do percurso podem ser removidas quando o cache ultrapassa 256 MB. O tempo restante é estimado sem trânsito em tempo real. Os filtros de GPS não garantem a precisão física do telefone.

## Créditos e licença

Projeto original: **Antonio Malaquias — [malaq88/DisplayConnect](https://github.com/malaq88/DisplayConnect)**. Esta adaptação não é apresentada como uma versão oficial do autor original.

Dados de mapa: **© OpenStreetMap contributors**. Roteamento: OSRM/FOSSGIS. Busca: Nominatim e Photon. Geometria das ruas: Overpass. O uso desses serviços está sujeito às políticas de seus operadores.

A [licença MIT original](LICENSE) e o aviso de copyright foram preservados.
