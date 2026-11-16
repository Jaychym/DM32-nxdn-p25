# DM32-nxdn-p25 — NXDN-P25-DM32 multimodo (FM · DMR · P25 · NXDN) para el Baofeng DM-32UV

[![Regálame un radio en Throne](https://img.shields.io/badge/Throne-Reg%C3%A1lame%20un%20radio-ff69b4?style=for-the-badge)](https://throne.com/lyraarsgoetia)

**¿Quieres firmware para tu radio?** Regálamelo desde mi lista de deseos y lo porto: **<https://throne.com/lyraarsgoetia>** · *Want firmware for your radio? Gift it from my wishlist and I'll port it.*

Firmware abierto y multimodo para el **Baofeng DM-32UV**: FM, DMR, **APCO P25 fase 1** y **NXDN48** (RX y TX) en canales convencionales, más recepción de troncal P25. Es una derivación de [Open25-DM32](https://github.com/tylerwatt12/Open25-DM32) (tylerwatt12), construida como serie de parches sobre [RadioFoundry](https://github.com/RadioFoundry/RadioFoundry). Esta rama la mantiene **lyra XE1POC**.

> Firmware experimental. Úsalo bajo tu responsabilidad, solo en frecuencias en las que tengas licencia y con un respaldo de tu radio.

## Estado (15 de noviembre de 2026)

| Función | Estado |
|---|---|
| FM y DMR | Funcionan (base RadioFoundry) |
| Mensajes DMR de texto libre | Hasta 140 caracteres desde Contactos; revisión antes de enviar. Compilado y probado en simulador, pendiente de probar en el aire |
| P25 convencional, RX y TX | Verificado en la radio (voz IMBE en ambos sentidos) |
| NXDN48, RX | Verificado en la radio (tramas, llamada y voz AMBE sin errores) |
| NXDN48, TX | El receptor del firmware y dsd-fme decodifican la TX del DM-32 (RAN 1, TG 1, voz), capturada con un Pluto+. Hay reportes de radios Icom, un repetidor Kenwood y hotspots MMDVM que no la decodifican; pendiente medir la desviación. Solo NXDN48 (Icom «VN», 6,25 kHz), no NXDN96 |
| Troncal P25 | Solo recepción; la TX troncal está desactivada |
| APRS y posición GPS | Activados en esta versión; sin probar en el aire |
| Tono de permiso estilo Motorola | P25/NXDN: TPT ASTRO 25 (3 bips de 910 Hz); DMR: chirp MOTOTRBO |
| Tema «Rosa» | Tema claro rosa (Ajustes › Pantalla › Tema › Rosa) |
| AM de aviación (108-136 MHz) | El squelch abre con los niveles 1-9 (antes solo con 0). Probado en la radio |
| Recepción ampliada | Solo RX: 174-260, 350-400 y 480-520 MHz, con menos sensibilidad fuera de los filtros. TX sigue en 136-174 / 400-480 |

## Hotspots, repetidores y otras radios

NXDN es solo NXDN48 (Icom «VN»). El NAC/RAN de **TX** debe coincidir con el del otro equipo; Pi-Star usa NAC 293
y RAN 1. Si el hotspot necesita un offset de cientos de Hz, corrige la radio en **Ajustes › Avanzado › Ajuste frec** (Freq trim).
Véase la [guía de usuario](docs/user-guide.md#hotspots-repeaters-and-other-radios).

## AM regression rollback

El Hotfix AM `527f5229` queda retirado: se reportó audio más áspero y sobrecargado que antes. Se revierte su
parche AM completo y se recupera el comportamiento de `b736e952`; se conservan las correcciones DMR, la imagen
de arranque y el VFO FM/DMR/P25/NXDN. La versión retirada deja de ser estable y el instalador ofrece la restauración.

**Esto revierte el empeoramiento; no da por resuelta la distorsión original con aviones cercanos.** El siguiente
ajuste de AM necesita una comparación física antes de recomendarlo. Véase [el diagnóstico](docs/am-regression.md).

## Hotfix — DMR PTT, boot screen, and four-mode VFO

The color boot screen now uses the supplied avatar and says **NXDN-P25-DM32**. The owner's two codeplug boot lines
are preserved. DMR voice release has an absolute 1.5 s limit, including the terminator drain; RF slot notifications
cannot keep it alive indefinitely. The HR_C7000 aborts immediately without waiting for another slot, and its PA
timer is canceled on abort. The firmware version shown on the radio identifies this repository's build commit.

El VFO ahora permite **FM → DMR → P25 → NXDN → FM**: abre el VFO y pulsa **\* → 4** (Modo).
El modo se guarda al reiniciar. P25 inicia con NAC 293, NXDN con RAN 1, ambos con TG 1; para otro NAC/RAN o grupo,
configura un canal y cópialo al VFO. Se conserva el ID de la radio y siguen vigentes los límites de TX.

Host regressions reproduce stuck release/timeout conditions before the fix and pass afterwards. **The reported
radio has not been tested with this build. Intermittent repeater access is not confirmed resolved.** See [validation and hardware checks](docs/dmr-ptt-release.md).

![New boot screen (simulator)](docs/boot-screen.png)

## Instalar

1. Descarga la imagen `.bin` sin códec de la última [release](../../releases), o usa directamente el **web flasher**: <https://donttellmeimcute.github.io/dm32-web-cps-flasher/> (Chrome o Edge de escritorio, Web Serial).
2. Pon la radio en modo actualización: apagada, mantén **PTT + SK1** al encenderla (LED verde).
3. El flasher hace un respaldo, toma el códec AMBE de tu propia radio (o del archivo de firmware de fábrica que elijas) y lo instala junto al firmware.

**Sobre el AMBE:** el códec AMBE+2 (voz DMR y NXDN) es de DVSI y Baofeng. **No se distribuye aquí**: ni la release ni el repositorio lo contienen. Solo pasa entre tu radio, tus archivos y tu navegador. Sin él funcionan FM y P25 (IMBE libre); DMR y NXDN necesitan el códec de tu radio.

Si necesitas el archivo original, Baofeng ofrece el [firmware oficial DM-32UV v1.01.048 (ZIP)](https://baofeng.s3.amazonaws.com/Baofeng_DM-32UV_Firmware_v1.01.048.zip), enlazado desde su [página de descargas](https://www.baofengradio.com/pages/download). El flasher reconoce ese ZIP en el selector de firmware original; no necesitas instalar primero el firmware de fábrica. No hay una descarga independiente de AMBE alojada en este proyecto. La [licencia pública de DVSI, sección 2.3](https://www.dvsinc.com/papers/DVSI%20End%20User%20Product%20Liceanse%20Agreement.pdf), exige consentimiento escrito para distribuir su software a terceros; no contamos con ese permiso. La disponibilidad del ZIP no concede derechos para redistribuir el códec ni sustituye las condiciones de licencia aplicables.

## Novedades de esta rama

Parches de la serie (`integration/radiofoundry/patches/`):

| Parche | Qué hace |
|---|---|
| `0009-digital-voice-status-recovery-and-credits` | Re-selección de canal digital sin perder la sesión, recuperación automática tras fallos del módem, pantalla «Digital: iniciando / fallo del módem» honesta, créditos |
| `0010-nxdn-fm-tx-ram` | TX NXDN por la TX RAM del HR_C7000: la que hace que la TX NXDN se decodifique |
| `0011-motorola-talk-permit` | Tono de permiso para hablar como Motorola; en P25/NXDN suena antes de dar portadora |
| `0012-pink-theme` | El tema claro pasa a ser el tema «Rosa» |
| `0013-tools-menu-built-features` | APRS y posición activados; Herramientas oculta lo que no está en la compilación (hotspot, red USB, repetidor de 1 frecuencia) |
| `0014-editor-nxdn-ran-on-mode-change` | Editor web de codeplug: al pasar un canal entre P25 y NXDN toma el NAC o RAN por defecto de su familia, y el formulario solo valida los campos visibles |
| `0016-dmr-free-text-messages` | Habilita «Escribir…» en Mensaje: teclado, hasta 140 caracteres, revisión con destinatario y envío DMR; reutiliza el búfer de texto existente |
| `0017-radio-p25-nac-entry` | En la radio, el NAC de un canal P25 se escribe: teclas para los dígitos hex, perilla/▲▼ para pasar el último por A-F; OK sin escribir alterna RX «cualquiera». Un cambio solo del NAC TX cuenta como cambio al guardar |
| `0018-editor-p25-talkgroup-on-mode-change` | Editor web: un canal que pasa a P25/NXDN/troncal sin grupo toma el grupo 1 (si no, el formulario no guardaba, tampoco el NAC), y los campos que reaparecen muestran el valor actual |
| `0019-nxdn-rx-any-ran-and-p25-f7f` | «Cualquier RAN» por canal en la radio: en la fila RAN de un canal NXDN, OK sin escribir alterna RX entre su RAN y todos (RX RAN 0) sin tocar el RAN TX; inicio muestra `RAN n/any`. En P25, escribir F7E o F7F pone RX «cualquiera» y conserva el NAC TX. El editor web y el CSV aceptan `any` en RX RAN; F7F en RX NAC se guarda como F7E |
| `0020-p25-nxdn-digital-monitor` | Monitor digital como en DMR para P25 y NXDN: una pulsación corta de SK1 (tecla monitor) oye todos los NAC/RAN y grupos del canal sin editarlo; `DMon` en la pantalla. Otra pulsación, Atrás o cambiar de canal lo quita. La TX sigue con el NAC/RAN y grupo del canal. En troncal P25: todos los grupos |
| `0021-dmr-ras-monitor-and-stock-air-band` | Repetidores DMR con RAS: con el monitor digital (SK1) una supertrama de voz en orden sin LC verificable se reproduce como llamada de grupo desconocida (TG/ID 0). La conversión del codeplug original conserva los canales de banda aérea (también sin frecuencia TX), como solo RX |
| `0022-dm32-air-band-squelch` | AM de aviación: el DM-32 lee la banda aérea unos 20 dB más baja que la radio con la que se ajustó el squelch AM de RadioFoundry, así que ningún nivel salvo 0 abría. La radio suma 20 dB a su RSSI en AM; el nivel 5 abre unos 8 dB sobre el ruido |
| `0023-dm32-rx-only-bands` | Bandas solo RX 174-260, 350-400 y 480-520 MHz (VFO, canales, escaneo). La TX sigue limitada a 136-174 / 400-480. El aviso de bandas muestra rangos unidos («65-260 350-520») y el de TX denegada solo las bandas de TX |
| `0024-boot-screen-picture` | Custom avatar and NXDN-P25-DM32 boot name; owner boot text preserved |
| `0025-dmr-ptt-release-recovery` | Bounded DMR release and timeout; immediate HR_C7000 abort and PA timer cancellation |
| `0026-conventional-digital-vfo` | FM/DMR/P25/NXDN VFO mode cycle, saved mode and NAC/RAN/group, and digital channel copy |

Fuera de la serie: recepción NXDN corregida (filtros del FD6818 en la ruta plana, FSW en ambas polaridades, ganancia AF) en `integration/radiofoundry/nxdn/` y `src/nxdn/`.

## Compilar

Requiere Linux (o WSL) y el toolchain C-SKY que se construye localmente; ver [docs/build.md](docs/build.md). La imagen de la release se generó con:

```sh
python3 tools/build_radiofoundry.py --speed-codec --stack-check --with-local-stock
python3 tools/rf_patch.py check        # "derivative tree matches the series"
```

**Compilación y release automáticas:** cada commit a `main` hace que la acción `Firmware` (`.github/workflows/firmware.yml`) compile la imagen sin códec y publique una release nueva `v<AAAA.MM.DD>-<commit>` con `rfoundry-dm32-<versión>.bin`, su manifiesto `.json` y `SHA256SUMS`. El web flasher la instala solo en unos 15 minutos (su acción `Sync firmware`). También se puede lanzar a mano en Actions › Firmware › Run workflow, con un nombre de versión propio. Nunca descarga el firmware de fábrica ni el AMBE.

`--speed-codec` es obligatorio (el códec IMBE por defecto es ~4× más lento que tiempo real). `--with-local-stock` toma el AMBE de tu propio firmware de fábrica en `research/originals` y genera además `rfoundry_dm32.codec.bin`; esa imagen es solo para uso local y no se publica (la release lleva únicamente `rfoundry_dm32.bin`, que no cambia con esa opción). Pruebas de host: `make -C build/radiofoundry-p25/tests/host run`, `tools/test_nxdn.py`, `test_p25t.py`, `test_voice.py`, `test_native_modem.py`, `test_receive_audio.py`.

## Apoya el proyecto

¿Quieres soporte para otro radio? Si me regalas uno desde mi lista de deseos, puedo hacer firmware para él:
**<https://throne.com/lyraarsgoetia>**

## Créditos y licencia

- **Open25-DM32** original: tylerwatt12 (KD8ZMM). Puedes apoyar su trabajo en [GitHub Sponsors](https://github.com/sponsors/tylerwatt12?metadata_project=open25-dm32) o [Ko-fi](https://ko-fi.com/kd8zmm).
- **RadioFoundry**: The RadioFoundry Authors (MIT; avisos conservados).
- Derivación P25/NXDN, recepción NXDN, TPT Motorola, tema rosa y APRS: **lyra XE1POC**.

El proyecto se distribuye bajo [GPL-3.0-or-later](LICENSE) salvo que un archivo indique otra cosa. Procedencia de cada fuente: [docs/licenses.md](docs/licenses.md).

---

## English summary

Multi-mode open firmware (FM, DMR, P25 Phase 1, NXDN48, P25 trunking receive) for the Baofeng DM-32UV, derived from [Open25-DM32](https://github.com/tylerwatt12/Open25-DM32) on top of [RadioFoundry](https://github.com/RadioFoundry/RadioFoundry), maintained by lyra XE1POC. P25 and NXDN voice work both ways: the radio's NXDN transmission is decoded by the firmware's own receiver and by dsd-fme. Releases ship the image **without** the proprietary AMBE codec; the [web flasher](https://donttellmeimcute.github.io/dm32-web-cps-flasher/) takes the codec from your own radio or stock file inside the browser. GPL-3.0-or-later.

**Support:** want firmware for another radio? Gift one from my wishlist and I can port it:
<https://throne.com/lyraarsgoetia>

## Developer resources

- [User guide: FM, DMR, P25, NXDN and P25 trunk channels](docs/user-guide.md)
- [Resources of the multi-mode image](docs/resources.md) and [the hardware-in-the-loop checklist](docs/hil-checklist.md)
- [Build and test guide](docs/build.md)
- [P25 conventional channels](docs/p25-channels.md)
- [NXDN48 conventional channels](docs/nxdn-channels.md) and [the NXDN48 modem simulation](docs/nxdn-modem.md)
- [P25 trunking](docs/p25-trunking.md) and [P25 Phase 2 (TDMA)](docs/p25-phase2.md)
- [Independent validation](docs/validation.md)
- [P25 framing, error correction, and synthetic test vectors](docs/protocol.md)
- [Modem research and RF test criteria](docs/modem.md)
- [Hardware validation and recovery](docs/hardware-tests.md)
- [Source provenance and license notices](docs/licenses.md)
- [Progress log of the original project](PROGRESS.md)
