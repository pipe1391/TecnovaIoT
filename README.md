# TecnovaIoT

Librería para Arduino/ESP32 que conecta un dispositivo a la plataforma IoT
de Tecnova ([panel.ceetecnova.com](https://panel.ceetecnova.com)) sin tener
que lidiar con MQTT, TLS ni el protocolo del panel a mano.

Se encarga de:

- Conectar WiFi (con reintento y reinicio si falla).
- Autenticar el dispositivo contra el webhook del panel (`dId` + password) y
  obtener sus credenciales MQTT.
- Validar el certificado TLS del broker (MQTT sobre WebSocket seguro,
  `wss://`) sin que tengas que generar ni embeber ningún certificado vos.
- Reconectar solo si se corta el WiFi o el MQTT.
- Mapear las variables configuradas para tu dispositivo en el panel a
  nombres legibles, en vez de índices fijos -- funciona igual sin importar
  cuántas variables tenga tu dispositivo ni en qué orden estén.

Compatible con **PlatformIO** y con el **IDE de Arduino** (o `arduino-cli`).

## Instalación

### PlatformIO

Clonar (o agregar como submódulo git) este repositorio dentro de la carpeta
`lib/` de tu proyecto:

```bash
git clone https://github.com/pipe1391/TecnovaIoT.git lib/TecnovaIoT
```

PlatformIO la detecta sola. Asegurate de tener `ArduinoJson` en tus
`lib_deps` (o dejá que PlatformIO lo resuelva por la dependencia declarada
en `library.json`).

### IDE de Arduino

Descargá o cloná este repositorio dentro de tu carpeta de librerías de
Arduino (`Documentos/Arduino/libraries/TecnovaIoT`), o usá **Programa →
Incluir Librería → Añadir archivo .ZIP...** con el `.zip` del repo.

## Uso rápido

```cpp
#include <TecnovaIoT.h>

TecnovaIoT tecnova("<dId>", "<password>"); // los ves en "Dispositivos" en el panel

void setup() {
  Serial.begin(921600);

  // Se registra ANTES de begin(). "led" debe ser el nombre EXACTO de la
  // variable configurada en el panel para este dispositivo.
  tecnova.onCommand("led", [](JsonVariant value) {
    digitalWrite(LED_BUILTIN, value["value"] == "true" ? HIGH : LOW);
  });

  tecnova.begin("<ssid_wifi>", "<password_wifi>");
}

void loop() {
  tecnova.loop(); // publica lo que corresponda y reconecta solo

  tecnova.setValue("temperatura", 23.5); // se publica respetando la
                                          // frecuencia configurada en el panel
}
```

Ver [`examples/BasicSensor`](examples/BasicSensor/BasicSensor.ino) para un
ejemplo completo (sensor + actuador).

## API

| Método | Qué hace |
|---|---|
| `TecnovaIoT(dId, password, jsonCapacity=4096)` | Constructor. `jsonCapacity` son los bytes reservados temporalmente para parsear la respuesta del webhook -- solo hace falta subirlo si tenés muchísimas variables. |
| `onCommand(nombreVariable, callback)` | Registra qué hacer cuando llega un comando para esa variable. Llamar antes de `begin()`. |
| `begin(ssid, password)` | Conecta WiFi, pide credenciales, conecta MQTT. Bloqueante; reinicia el ESP32 solo si algo falla. |
| `loop()` | Llamar en cada vuelta de `loop()`. Publica variables vencidas y reconecta si hace falta. |
| `setValue(nombreVariable, valor, save=false)` | Actualiza el valor de una variable (`float`, `int`, `bool`, `String` o `JsonVariant`). Se publica sola en el próximo ciclo. `save` indica si el backend debe guardar este valor en el historial. |
| `isConnected()` | `true` si el MQTT está conectado ahora mismo. |
| `printStats(out=Serial)` | Debug: tabla con el estado de cada variable. Throttle interno, no imprime más seguido que cada 2s aunque la llames en cada `loop()`. |

## Sobre el certificado TLS

La librería trae embebido (`src/TecnovaRootCaBundle.h`) un bundle mínimo de
Autoridades Certificadoras raíz (GlobalSign Root CA + ISRG Root X1) --
necesario para validar el certificado que presenta
`panel.ceetecnova.com` al conectar por WSS. Es el mismo para **cualquier**
dispositivo de **cualquier** usuario que hable con esta plataforma -- no
hay que regenerarlo por dispositivo.

Solo habría que regenerarlo si el servidor rotara a una Autoridad
Certificadora fuera de esas dos. Para revisar la cadena real:

```bash
openssl s_client -connect panel.ceetecnova.com:443 -showcerts
```

## Compatibilidad de versiones del core ESP32

El struct de configuración del cliente MQTT nativo de ESP-IDF cambió de
forma entre IDF 4.x (plano) e IDF 5.x (anidado). La librería detecta
automáticamente cuál usa tu core instalado (vía `ESP_IDF_VERSION_MAJOR`) --
no hace falta que hagas nada al respecto, compila igual con arduino-esp32
2.x o 3.x.

## Licencia

MIT -- ver [`LICENSE`](LICENSE).
