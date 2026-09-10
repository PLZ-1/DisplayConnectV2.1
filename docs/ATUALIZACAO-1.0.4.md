# DisplayConnect 1.0.4 — ruas offline, bicicleta, idiomas e GPS

## Uso

1. Atualize o app e o firmware juntos. Escolha Português (Brasil) ou English nas configurações.
2. Com internet, escolha o tipo de trajeto, busque o destino e inicie a navegação.
3. Aguarde **Percurso pronto para usar offline**. O cartão mostra as áreas salvas. Se a conexão cair, **Retomar download** baixa apenas o que falta.
4. Depois dessa confirmação, GPS, Bluetooth, rota e ruas do corredor baixado podem funcionar sem internet. A última rota e as ruas ficam salvas no celular, inclusive após fechar o app; para reutilizar a rota, mantenha o destino e o perfil e reinicie próximo do traçado.

Para outro destino, outra rota ou um desvio para fora da área salva, é necessário acesso à internet. A escala selecionada passa a valer ao iniciar a navegação; uma escala maior pode precisar de novas áreas. O mapa utiliza os dados disponíveis no OpenStreetMap, que podem conter lacunas.

## Cobertura e desenho

A escala de 400 m é o alcance vertical a partir do centro. Na tela 480 × 232, a metade horizontal corresponde a cerca de 828 m, e os cantos ficam a cerca de 920 m. O download usa essa distância mais 100 m de margem e cobre continuamente todo o percurso, inclusive entre pontos afastados da rota. Portanto, um raio fixo de 500 m não cobriria a tela inteira nesse ajuste.

As áreas são baixadas em lotes pequenos, com pausa entre pedidos, geometrias completas e gravação persistente. Respostas incompletas ou erros do servidor não contam como áreas prontas. Há limite de 512 áreas por percurso; rotas que excedem esse limite precisam ser divididas, com aviso na tela. Áreas antigas fora do percurso atual podem ser removidas quando o cache ultrapassa 256 MB.

O limite de ruas transmitidas subiu de 128 para 384 segmentos. A simplificação é aplicada ao conjunto visível antes do descarte, e a seleção distribui ruas pela tela, em vez de concentrá-las no centro. Áreas extremamente densas ainda podem exigir simplificação/descarte. O limite de mensagem BLE passou para 12 KiB e a fila ESP32 para 24 KiB.

## Perfis e idioma

- Bicicleta usa o servidor OSRM **routed-bike**; caminhada usa **routed-foot**; carro e moto usam **routed-car**. A versão anterior mudava apenas o texto do perfil na URL, sem mudar o grafo de roteamento. Moto continua usando o perfil de carro.
- Português e inglês podem ser selecionados nas configurações, com preferência persistida e instruções de navegação no idioma escolhido.
- O firmware inclui uma fonte raster com caracteres ASCII e Latin-1, incluindo ã, õ, ç, á, é, ê, à e maiúsculas. O texto enviado é normalizado em Unicode e seus limites de armazenamento preservam caracteres UTF-8 completos.

## GPS e travamentos

A versão anterior aceitava posições sem verificar precisão, idade ou ordem temporal. Parte do cálculo do mapa acontecia na tarefa da interface. Esses fatores poderiam contribuir para posições erradas ou atrasadas, mas não há registro do trajeto relatado que permita determinar a causa específica.

Agora o app solicita localização precisa, não pede lotes atrasados e filtra posições com mais de 5 segundos, precisão pior que 40 m, ordem temporal incorreta e saltos isolados incompatíveis com o deslocamento. Uma recuperação consistente do GPS pode ser aceita após três amostras. Pequenos desvios a até 20 m do traçado podem ser ajustados à rota, levando em conta direção e precisão; desvios maiores não são forçados de volta à pista.

O app mostra a precisão ou o aviso de GPS fraco. O display também indica GPS fraco quando mantém a última posição confiável. O cálculo e a leitura dos mapas ocorrem fora da tarefa da interface, e posições antigas na fila são descartadas para processar a mais recente. Isso não substitui um sinal GPS adequado nem garante precisão física do telefone.

## Validação e instalação

37 testes Kotlin/JUnit passaram: cobertura dos cantos e dos trechos, persistência e reabertura sem chamadas à rede, retomada de download parcial, arquivo corrompido, cancelamento, separação dos perfis, distribuição das ruas, progresso da viagem e filtros de GPS. Os recursos dos dois idiomas foram conferidos e a fonte foi renderizada para inspeção dos acentos.

Todos os fontes Kotlin do aplicativo também passaram pela compilação Kotlin/Compose usando as classes de API já disponíveis no cache local. Essa conferência verifica o código, mas não empacota os recursos Android, não gera um APK e não executa a interface no celular.

Compilação completa da versão 1.0.4 ainda pendente. Apesar da liberação solicitada nesta sessão, o Windows continuou negando acesso a arquivos do SDK Android e à execução do Arduino CLI. Os APKs e binários anteriores são da 1.0.3. Execute **Compilar-Tudo.cmd** na pasta do projeto, instale o APK gerado e use **Carregar** no Arduino IDE para gravar o ESP32. Os 37 testes locais não substituem essa compilação nem os testes físicos de navegação.

Fontes de referência: [perfis OSRM/FOSSGIS](https://routing.openstreetmap.de/about.html), [Overpass](https://dev.overpass-api.de/overpass-doc/en/preface/commons.html), [solicitação de localização Android](https://developers.google.com/android/reference/com/google/android/gms/location/LocationRequest.Builder).
