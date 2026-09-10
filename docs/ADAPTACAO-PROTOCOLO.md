# Protocolo da adaptação 1.0.4

Mantido o transporte JSON por BLE Nordic UART Service, com uma mensagem por linha. O nome anunciado é `DisplayConnect-LOLIN32`; os UUIDs NUS são os originais.

## Campos adicionados ao quadro `nav`

| Campo | Tipo | Significado |
|---|---|---|
| `remaining_m` | inteiro | Distância restante em metros; -1 quando desconhecida |
| `remaining_s` | inteiro | Tempo restante em segundos; -1 quando desconhecido |
| `off_route` | booleano | Posição fora da rota |
| `lang` | string | `pt-BR` ou `en` |
| `gps_weak` | booleano | Sinal/posição GPS insuficiente; último quadro pode ser mantido |

`distance_m` continua indicando a distância à instrução. Coordenadas da geometria são projetadas para 480 × 232. O limite de pontos da rota é 64, com `[-1,-1]` separando trechos que saem da tela; o renderizador não deve ligar esses intervalos.

Até 384 segmentos de ruas podem ser transmitidos. O limite da linha de recepção é 12 KiB e a fila ESP32 tem 24 KiB. App e firmware precisam ser atualizados juntos.

## Configuração de idioma

```json
{"type":"config","lang":"pt-BR"}
```

O ESP32 persiste a preferência e também lê o idioma nos quadros de navegação. Os textos são UTF-8, normalizados em NFC pelo Android. A fonte do firmware cobre ASCII e Latin-1; não cobre todo Unicode.

O documento original `PROTOCOL_V2.md` é mantido como referência histórica. Este arquivo descreve as extensões da adaptação.
