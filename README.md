# TecnovaIoT

Librería para Arduino/ESP32 que conecta un dispositivo a la plataforma IoT
de Tecnova (credenciales por HTTPS, datos por MQTT sobre WebSocket seguro
-- WSS) sin tener que lidiar con MQTT, TLS ni el protocolo del panel a
mano.

Este documento está escrito pensando en que quien lo lea puede estar
recién empezando con IoT/ESP32 — no asume que ya sabés qué es MQTT, TLS o
un portal cautivo. Si ya conocés estos conceptos, andá directo a
[Uso rápido](#uso-rápido) o a la [tabla de API](#api).

## Índice

- [¿Qué problema resuelve esta librería?](#qué-problema-resuelve-esta-librería)
- [Instalación](#instalación)
- [Conceptos básicos](#conceptos-básicos-para-quien-recién-empieza)
- [Uso rápido](#uso-rápido)
- [API](#api)
- [Consumo de energía](#consumo-de-energía)
- [Portal cautivo (TecnovaProvisioning)](#portal-cautivo-tecnovaprovisioning)
- [Sobre el certificado TLS](#sobre-el-certificado-tls)
- [Compatibilidad de versiones del core ESP32](#compatibilidad-de-versiones-del-core-esp32)
- [Errores comunes y cómo entenderlos](#errores-comunes-y-cómo-entenderlos)
- [Licencia](#licencia)

## ¿Qué problema resuelve esta librería?

Conectar un ESP32 a una plataforma IoT real (no solo "prender un LED por
WiFi", sino un dispositivo que va a estar en producción) implica resolver
varios problemas que **no tienen nada que ver con tu proyecto en
particular**, y que son fáciles de hacer mal:

1. Conectar WiFi de forma robusta (con reintentos, sin colgarse si la red
   no está disponible).
2. Autenticar el dispositivo contra un servidor y obtener credenciales.
3. Conectar por MQTT usando TLS (la versión "segura" de MQTT), validando
   el certificado del servidor correctamente.
4. Reconectar solo si se corta la red o el broker, sin perder el estado.
5. Traducir el "protocolo" propio de la plataforma (topics, formato de
   los mensajes) a algo simple para tu código: "leí un sensor, lo publico"
   / "llegó un comando, reacciono".

Todo esto **es siempre igual**, sin importar si tu proyecto mide
temperatura, humedad, o controla un motor. Por eso tiene sentido
resolverlo una sola vez, acá, y que cada proyecto nuevo solo escriba la
parte que sí es distinta: qué sensores lee y qué hace con los comandos que
recibe.

## Instalación

### PlatformIO

Cloná (o agregá como submódulo git) este repositorio dentro de la carpeta
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

## Conceptos básicos (para quien recién empieza)

Si ya sabés qué son MQTT, TLS y NVS, saltate esta sección.

**¿Qué es MQTT?** Es un protocolo de mensajería pensado para dispositivos
con poca memoria y conexiones inestables (justo el perfil de un ESP32). En
vez de que cada dispositivo hable directo con cada otro, todos se conectan
a un servidor central llamado **broker**, y se comunican publicando y
suscribiéndose a "canales" con nombre, llamados **topics**. Por ejemplo,
un sensor de temperatura *publica* su valor en un topic como
`usuario/dispositivo/temperatura/sdata`, y cualquiera que esté
*suscrito* a ese topic recibe el dato al instante. Esta librería arma esos
nombres de topic por vos, siguiendo el protocolo que usa el panel Tecnova.

**¿Qué es TLS y por qué importa acá?** Es la tecnología que cifra la
conexión entre el dispositivo y el broker (la misma familia que el
"candadito" 🔒 de HTTPS en el navegador). Sin TLS, cualquiera en la misma
red podría leer o falsificar los datos que manda tu dispositivo. Para que
el ESP32 confíe en que está hablando con el broker real (y no con un
impostor), necesita una lista de "Autoridades Certificadoras" en las que
confiar — esta librería ya trae esa lista embebida (ver [Sobre el
certificado TLS](#sobre-el-certificado-tls)), así que no tenés que generar
nada vos.

**¿Qué es NVS?** Es la zona de la memoria flash del ESP32 reservada para
guardar datos chiquitos que sobreviven a un reinicio o corte de luz —
similar a `localStorage` en un navegador. La usa internamente el módulo de
portal cautivo (`TecnovaProvisioning`) para recordar las credenciales
entre encendidos.

## Uso rápido

```cpp
#include <TecnovaIoT.h>

TecnovaIoT tecnova("<dId>", "<password>"); // los ves en "Dispositivos" en el panel

void setup() {
  Serial.begin(921600);

  // Se registra ANTES de begin(). "led" debe ser el nombre EXACTO de la
  // variable configurada en el panel para este dispositivo.
  tecnova.onCommand("led", [](JsonVariant value) {
    // El valor puede llegar como booleano nativo ({"value":true}) o como
    // texto ({"value":"true"}) -- ver "Errores comunes" mas abajo.
    JsonVariant v = value["value"];
    bool encender = v.is<bool>() ? v.as<bool>() : (v.as<String>() == "true");
    digitalWrite(LED_BUILTIN, encender ? HIGH : LOW);
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
ejemplo completo (sensor + actuador), o
[`examples/CaptivePortal`](examples/CaptivePortal/CaptivePortal.ino) para
la variante sin credenciales hardcodeadas (ver más abajo).

### Más ejemplos (sensores y actuadores reales)

Todos siguen el mismo patrón que el de arriba -- lo único que cambia entre
uno y otro es CÓMO se lee el sensor o se maneja el actuador; la parte de
TecnovaIoT (`setValue`/`onCommand`/`loop`) es idéntica siempre. Sirven
también como referencia de cómo conectar tu propio sensor aunque no sea
exactamente uno de estos.

| Ejemplo | Qué muestra | Librería(s) extra necesaria(s) |
|---|---|---|
| [`PhSensor`](examples/PhSensor/PhSensor.ino) | Sensor analógico de pH (ej. DFRobot Gravity/SEN0161) leído a través de un ADS1115 (ADC externo de 16 bits), no con el ADC interno del ESP32 -- el propio ejemplo explica por qué. Calibración con dos puntos. | `adafruit/Adafruit ADS1X15` |
| [`ADS1115`](examples/ADS1115/ADS1115.ino) | Plantilla genérica: leer los 4 canales de un ADS1115 (ADC externo por I2C) -- útil como base para cualquier sensor analógico que necesite más precisión que el ADC interno del ESP32. | `adafruit/Adafruit ADS1X15` |
| [`BME280Sensor`](examples/BME280Sensor/BME280Sensor.ino) | Temperatura, humedad y presión por I2C con un BME280. | `adafruit/Adafruit BME280 Library`, `adafruit/Adafruit Unified Sensor` |
| [`DHT11Sensor`](examples/DHT11Sensor/DHT11Sensor.ino) | Temperatura y humedad con un DHT11 (el sensor "clásico" de los kits de iniciación). | `adafruit/DHT sensor library`, `adafruit/Adafruit Unified Sensor` |
| [`RGBLed`](examples/RGBLed/RGBLed.ino) | **Actuador**: reacciona a un comando `onCommand()` para poner un color en un LED RGB por PWM. | Ninguna (solo `analogWrite`). |
| [`GPSTracker`](examples/GPSTracker/GPSTracker.ino) | Latitud/longitud leyendo un módulo GPS NEO-6M/NEO-M8N por UART. | `mikalhart/TinyGPSPlus` |
| [`DeepSleepSensor`](examples/DeepSleepSensor/DeepSleepSensor.ino) | Dispositivo a batería que se despierta, publica, y vuelve a dormir -- ver [Consumo de energía](#consumo-de-energía). | Ninguna. |

Cada ejemplo trae en su propio encabezado el detalle de conexión física
(qué pin va a qué pata del sensor) y, si hace falta, la línea exacta para
agregar a tu `platformio.ini`.

## API

| Método | Qué hace |
|---|---|
| `TecnovaIoT(dId, password, jsonCapacity=4096)` | Constructor. `jsonCapacity` son los bytes reservados temporalmente para parsear la respuesta del webhook -- solo hace falta subirlo si tenés muchísimas variables. |
| `onCommand(nombreVariable, callback)` | Registra qué hacer cuando llega un comando para esa variable. Llamar antes de `begin()`. |
| `begin(ssid, password)` | Conecta WiFi, pide credenciales, conecta MQTT. Bloqueante; reinicia el ESP32 solo si algo falla. Si el WiFi ya está conectado (por ejemplo, porque lo conectó `TecnovaProvisioning` antes), no vuelve a intentarlo. |
| `loop()` | Llamar en cada vuelta de `loop()`. Publica variables vencidas y reconecta si hace falta. |
| `setValue(nombreVariable, valor, save=false)` | Actualiza el valor de una variable (`float`, `int`, `bool`, `String` o `JsonVariant`). Se publica sola en el próximo ciclo, respetando la frecuencia configurada en el panel para esa variable. `save` indica si el backend debe guardar este valor en el historial. |
| `isConnected()` | `true` si el MQTT está conectado ahora mismo. |
| `printStats(out=Serial)` | Debug: tabla con el estado de cada variable. Throttle interno, no imprime más seguido que cada 2s aunque la llames en cada `loop()`. |
| `enablePowerSave()` | Activa el modem-sleep de WiFi -- ahorra energía sin perder la sesión MQTT ni dejar de recibir comandos. Ver [Consumo de energía](#consumo-de-energía). |
| `deepSleepSeconds(segundos)` | Apaga el ESP32 en deep sleep durante ese tiempo. **Solo para dispositivos que nunca reciben comandos.** No retorna -- ver [Consumo de energía](#consumo-de-energía). |

## Consumo de energía

Si tu dispositivo va a funcionar a batería, esto es importante. MQTT
funciona por **empuje** (push): el broker manda el mensaje apenas alguien
publica algo, no hay forma de "pedirlo" después. Eso divide a los
dispositivos en dos familias, con estrategias de ahorro distintas -- y la
que corresponde depende de si tu dispositivo **recibe comandos o no**,
no de qué tan seguido publica.

### Dispositivos que SOLO publican (sensores)

Si tu dispositivo nunca tiene una variable de tipo `output` con
`onCommand()` registrado -- no importa que esté "sordo" un rato, porque
nadie le va a mandar nada -- podés usar **deep sleep**: apaga
prácticamente todo el ESP32 entre lecturas (consumo de microamperios,
meses o años de batería) y se despierta solo por temporizador.

```cpp
tecnova.setValue("temperatura", leerTemperatura());

unsigned long inicio = millis();
while (millis() - inicio < 8000) { // le da tiempo a publicar de verdad
  tecnova.loop();
  delay(50);
}

tecnova.deepSleepSeconds(5 * 60); // duerme 5 minutos; no retorna
```

Ver el ejemplo completo en
[`examples/DeepSleepSensor`](examples/DeepSleepSensor/DeepSleepSensor.ino).

**Ojo con esto:** para el ESP32, despertar de un deep sleep es
indistinguible de un reinicio -- vuelve a correr `setup()` desde cero,
reconectando WiFi y MQTT cada vez. Eso tiene un costo real de tiempo (unos
segundos) y de batería por ciclo, así que este patrón rinde con
intervalos de **minutos**, no de segundos -- si necesitás publicar muy
seguido, no te conviene dormir, usá el patrón normal (`examples/BasicSensor`).

### Dispositivos que RECIBEN comandos (actuadores) o son MIXTOS

Si tu dispositivo tiene aunque sea una variable con `onCommand()`
registrado, **no uses `deepSleepSeconds()`**: un comando que llegue
mientras el dispositivo está dormido se pierde para siempre (esta
librería usa QoS 0 -- sin cola de mensajes pendientes en el broker). En
su lugar, usá `enablePowerSave()` después de `begin()`:

```cpp
tecnova.begin(WIFI_SSID, WIFI_PASSWORD);
tecnova.enablePowerSave(); // el radio WiFi ahorra energia entre actividad,
                            // pero la sesion MQTT sigue viva
```

Esto activa el modo de ahorro de energía del radio WiFi (se apaga entre
los "beacons" periódicos del router y se prende justo para escucharlos,
en vez de estar recibiendo todo el tiempo). El ahorro es bastante más
modesto que un deep sleep, pero el dispositivo **sigue alcanzable en todo
momento** -- con algo más de latencia (de milisegundos a un par de
segundos) para recibir un comando.

Ver el ejemplo completo en
[`examples/RGBLed`](examples/RGBLed/RGBLed.ino).

## Portal cautivo (TecnovaProvisioning)

### El problema que resuelve

El ejemplo de arriba (`Uso rápido`) tiene un defecto para uso real: el
SSID de WiFi, su password, y el `dId`/password del dispositivo quedan
**escritos en el código fuente**. Eso funciona bien para un prototipo en
tu propio banco de pruebas, pero se vuelve un problema apenas querés que
otra persona (un alumno, un colega, alguien en otra ubicación) instale el
mismo dispositivo: tendría que editar el código y volver a programar el
ESP32 solo para poner su propia red WiFi.

`TecnovaProvisioning` resuelve esto con un **portal cautivo**: la primera
vez que se enciende el dispositivo (o cuando se le pide explícitamente),
en vez de intentar conectarse solo, el ESP32 crea su propia red WiFi
temporal. Uno se conecta a esa red desde el celular, completa un
formulario, y desde ahí en más el dispositivo ya sabe todo lo que
necesita — sin volver a tocar el código.

### Requisito adicional

Este módulo usa la librería [WiFiManager](https://github.com/tzapu/WiFiManager)
para el trabajo pesado del portal (servidor web, DNS, detección automática
en el celular). **No es una dependencia obligatoria de TecnovaIoT** -- si
tu proyecto no usa `TecnovaProvisioning`, no la necesitás. Si sí la usás,
agregala a tu `platformio.ini`:

```ini
lib_deps =
    bblanchon/ArduinoJson@^6.19.4
    tzapu/WiFiManager@^2.0.17
```

(En el IDE de Arduino: Gestor de Librerías → buscar "WiFiManager" de
tzapu → Instalar.)

### Cómo se usa

```cpp
#include <TecnovaIoT.h>
#include <TecnovaProvisioning.h>

TecnovaIoT *tecnova = nullptr;

void setup() {
  Serial.begin(921600);

  String wifiSsid, wifiPassword, deviceId, devicePassword;
  TecnovaProvisioning::begin(wifiSsid, wifiPassword, deviceId, devicePassword);

  // Recien ACA sabemos el dId/password -- por eso TecnovaIoT se crea
  // DESPUES de la línea anterior, no antes.
  tecnova = new TecnovaIoT(deviceId, devicePassword);
  tecnova->begin(wifiSsid.c_str(), wifiPassword.c_str());
}

void loop() {
  TecnovaProvisioning::checkReconfigureButton(); // mantener BOOT 3s reabre el portal
  tecnova->loop();
}
```

Ver el ejemplo completo en
[`examples/CaptivePortal`](examples/CaptivePortal/CaptivePortal.ino).

### Qué pasa paso a paso

1. **Primer encendido** (no hay nada guardado): el ESP32 crea la red WiFi
   `TecnovaIoT-Setup`. Te conectás desde el celular y aparece un
   formulario: la red WiFi de destino (con su password) + el `dId` y
   password del dispositivo.
2. Al guardar, el ESP32 intenta conectarse con esos datos. Si funciona,
   los guarda en NVS (ver [Conceptos básicos](#conceptos-básicos-para-quien-recién-empieza))
   y sigue el arranque normal.
3. **Próximos encendidos**: como ya está todo guardado, se conecta solo,
   sin mostrar nada.
4. **Si necesitás cambiar la configuración** más adelante (otra red WiFi,
   otro dispositivo): con el equipo ya prendido y funcionando, mantené
   apretado el botón **BOOT** 3 segundos. Se reinicia solo y vuelve a
   mostrar el portal.

### Por qué el botón se revisa "en caliente" y no al resetear

Es un detalle de hardware que vale la pena entender, porque es un error
muy fácil de cometer: en el ESP32, el pin **GPIO0** (el mismo que suele
estar conectado al botón "BOOT" en las placas de desarrollo) cumple una
doble función. Además de poder usarse como una entrada digital común, el
propio chip lo revisa en el instante exacto de un reset o power-up para
decidir si arranca el programa grabado en la flash, o si entra al modo de
grabación por USB (el que usa `esptool`/PlatformIO para programarlo).

Si mantenés ese botón apretado **justo** en el momento de resetear, corrés
el riesgo de que el ESP32 nunca llegue a ejecutar tu firmware -- se queda
esperando una programación por USB. Por eso `checkReconfigureButton()` se
revisa **en `loop()`, con el chip ya arrancado hace rato** (no en
`setup()` ni en ningún código que corra apenas se resetea): en ese momento
GPIO0 ya volvió a ser un pin de entrada común, sin ningún significado
especial para el hardware.

### Referencia rápida del módulo

| Función | Qué hace |
|---|---|
| `TecnovaProvisioning::begin(wifiSsid, wifiPassword, deviceId, devicePassword, apName="TecnovaIoT-Setup", configButtonPin=0)` | Junta las 4 credenciales (de NVS o del portal) y deja el WiFi conectado. Bloqueante. |
| `TecnovaProvisioning::checkReconfigureButton(configButtonPin=0, holdMs=3000)` | Llamar en cada `loop()`. Reabre el portal si se mantiene el botón apretado. |
| `TecnovaProvisioning::forget()` | Borra el `dId`/password guardados (no toca el WiFi), para forzar reconfiguración completa. |

## Sobre el certificado TLS

La librería trae embebido (`src/TecnovaRootCaBundle.h`) un bundle mínimo de
Autoridades Certificadoras raíz (GlobalSign Root CA + ISRG Root X1) --
necesario para validar el certificado que presenta el broker al conectar
por WSS. Es el mismo para **cualquier** dispositivo de **cualquier**
usuario que hable con esta plataforma -- no hay que regenerarlo por
dispositivo, porque valida al *servidor*, no al dispositivo que se
conecta.

Solo habría que regenerarlo si el servidor rotara a una Autoridad
Certificadora fuera de esas dos. Para revisar la cadena real de un
servidor (reemplazando `<host>` por el que corresponda):

```bash
openssl s_client -connect <host>:443 -showcerts
```

## Compatibilidad de versiones del core ESP32

El struct de configuración del cliente MQTT nativo de ESP-IDF cambió de
forma entre IDF 4.x (plano) e IDF 5.x (anidado). La librería detecta
automáticamente cuál usa tu core instalado (vía `ESP_IDF_VERSION_MAJOR`) --
no hace falta que hagas nada al respecto, compila igual con arduino-esp32
2.x o 3.x.

## Errores comunes y cómo entenderlos

Esta sección documenta problemas reales con los que nos topamos
desarrollando y probando esta librería -- se dejan acá para que quien la
use (o quien esté aprendiendo del código) entienda la causa, no solo la
solución.

- **`"Failed to attach bundle"` al conectar MQTT**: significa que el
  bundle de certificados TLS (ver arriba) no se cargó. Si estás usando
  `TecnovaIoT` normalmente esto no debería pasar (la librería lo carga
  sola), pero si lo ves, revisá que no haya dos copias de la librería
  instaladas en conflicto.
- **El webhook devuelve 302 en vez de 200**: si tu propio servidor está
  detrás de Cloudflare Access (o algo similar), puede estar exigiendo un
  login interactivo que un dispositivo no puede completar. Hay que
  excluir el endpoint del webhook (y el de `/mqtt`) de esa protección --
  esto es una configuración del lado del servidor, no del firmware.
- **El dispositivo "flooda" el broker con mensajes**: si una variable no
  tiene `variableSendFreq` configurado (o es 0), la librería usa un piso
  de 1 segundo automáticamente, así que esto no debería pasar -- pero si
  ves publicaciones muchísimo más seguido de lo esperado, revisá la
  configuración de esa variable en el panel.
- **Compila pero no conecta, y en el panel el dispositivo tiene menos
  variables de las que tu código espera**: `setValue()`/`onCommand()`
  devuelven `false` silenciosamente (revisá el valor de retorno) si el
  nombre no coincide con ninguna variable configurada para ese
  dispositivo en el panel. La comparación **no distingue mayúsculas de
  minúsculas** (`"Temperatura"` y `"temperatura"` matchean igual), pero
  el resto del texto sí tiene que ser idéntico -- typos, espacios de más,
  tildes, etc. sí importan.
- **`onCommand()` se dispara pero el actuador nunca "prende"**: revisá con
  qué *tipo* llega el valor, no solo con qué valor. Para una variable
  booleana, el panel puede mandar `{"value":true}` (booleano JSON nativo)
  o `{"value":"true"}` (texto) según cómo esté configurada -- son tipos
  distintos para ArduinoJson, y `value["value"] == "true"` da `false`
  **siempre** si lo que llegó fue un booleano de verdad (nunca son
  "iguales" entre sí, aunque representen lo mismo). Se soluciona
  contemplando los dos casos: `JsonVariant v = value["value"]; bool x =
  v.is<bool>() ? v.as<bool>() : (v.as<String>() == "true");` (así están
  escritos ya los ejemplos `BasicSensor` y `CaptivePortal`, que reciben un
  booleano). El síntoma es engañoso: `printStats()`/`Last incoming msg` muestran que el
  comando llegó bien, y el `Count` de la variable sube -- el problema está
  puntualmente en la comparación de tipos, no en la conexión.
- **`error: call of overloaded 'setValue(...)' is ambiguous`**: pasa
  cuando le mandás a `setValue()` un valor de tipo `double` (por ejemplo,
  el resultado de una función de una librería de sensor/GPS que devuelve
  `double`, no `float`) -- el compilador no sabe si convertirlo al
  overload `float` o al `int`, y ninguno de los dos es "más correcto" que
  el otro, así que se niega a elegir. Se soluciona casteando a mano:
  `tecnova.setValue("variable", (float)valor);` (ver
  [`examples/GPSTracker`](examples/GPSTracker/GPSTracker.ino), que se topa
  justo con este caso porque `TinyGPSPlus` devuelve `double`).

## Licencia

MIT -- ver [`LICENSE`](LICENSE).
