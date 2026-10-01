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
- [Variables que miden y variables que accionan](#variables-que-miden-y-variables-que-accionan)
- [Qué manda cada control del panel](#qué-manda-exactamente-cada-control-del-panel)
- [API](#api)
- [Consumo de energía](#consumo-de-energía)
- [Portal cautivo (TecnovaProvisioning)](#portal-cautivo-tecnovaprovisioning)
- [Dispositivos con pantalla (o un loop() que no puede esperar)](#dispositivos-con-pantalla-o-un-loop-que-no-puede-esperar)
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
4. Recuperarse solo si se corta la red o el broker. El MQTT se reconecta
   solo; ante un corte de WiFi, con la configuración por defecto, la
   librería reinicia el ESP32 para empezar de cero (con
   `setAutoRestart(false)` reintenta sin reiniciar: ver
   [Dispositivos con pantalla](#dispositivos-con-pantalla-o-un-loop-que-no-puede-esperar)).
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
  // variable configurada en el panel para este dispositivo, y esa variable
  // tiene que estar marcada como "El panel la acciona" (ver más abajo).
  tecnova.onCommand("led", [](JsonVariant value) {
    // El interruptor del panel manda un booleano JSON. Se lee con
    // .as<bool>() -- nunca comparando contra el texto "true".
    digitalWrite(LED_BUILTIN, value["value"].as<bool>() ? HIGH : LOW);
  });

  tecnova.begin("<ssid_wifi>", "<password_wifi>");
}

void loop() {
  tecnova.loop(); // publica lo que corresponda y vigila la conexión

  tecnova.setValue("temperatura", 23.5); // se publica respetando la
                                          // frecuencia configurada en el panel
}
```

Ver [`examples/BasicSensor`](examples/BasicSensor/BasicSensor.ino) para un
ejemplo completo (sensor + actuador), o
[`examples/CaptivePortal`](examples/CaptivePortal/CaptivePortal.ino) para
la variante sin credenciales hardcodeadas (ver más abajo).

### Variables que MIDEN y variables que ACCIONAN

Al crear una variable en el panel (sección **Variables**) hay que
contestar una pregunta: **"¿Qué hace esta variable?"**.

| Respuesta en el panel | Qué es | Cómo se usa desde el código |
|---|---|---|
| **El equipo la mide** | Un sensor: temperatura, humedad, nivel de un estanque. | Se publica con `setValue()`. |
| **El panel la acciona** | Un actuador: una luz, un relé, una bomba. | Se recibe con `onCommand()`. |

Si te equivocás acá no salta ningún error, y por eso conviene mirarlo: el
panel te va a dejar ponerle un interruptor igual y el comando va a llegar
lo mismo. Lo que te vas a perder es todo lo demás -- esa variable **no
aparece en Automatizaciones** (así que no podés escribir "si oscurece,
encendé la luz"), y la lista de Variables te muestra una "frecuencia de
envío" que en un actuador no significa nada.

Al revés sí es mudo de verdad: `setValue()` sobre una variable de salida
**no publica nada**, aunque devuelva `true` (la librería te lo avisa por
el monitor serie la primera vez que lo intentás).

> **Si venís de antes del 25 de septiembre de 2026**, revisá tus
> actuadores uno por uno: hasta esa fecha toda variable creada a mano en
> **Variables** quedaba como "El equipo la mide", porque la pregunta ni
> siquiera se hacía. Se corrige entrando a la variable y cambiando la
> respuesta.

### Qué manda exactamente cada control del panel

El valor lo decide el **tipo de control** que pongas en el panel, y
siempre llega **tipado**. No hay ningún control que mande texto.

| Control en el panel | Qué publica | Cómo se lee en el firmware |
|---|---|---|
| **Interruptor** | `{"value": true}` / `{"value": false}` | `value["value"].as<bool>()` |
| **Botón de pulso** | `{"value": true}` — siempre, no tiene estado | `value["value"].as<bool>()` |
| **Deslizador** | `{"value": 128}` — un número | `value["value"].as<int>()` o `.as<float>()` |

Tres cosas que ahorran una tarde:

- **Para encender y apagar, usá un Interruptor, no un Botón.** Un botón
  de pulso manda siempre `true`: sirve para abrir una cerradura o dar un
  riego, pero nunca va a poder apagar nada.
- **Nunca compares contra el texto `"true"`.** `value["value"] == "true"`
  da `false` ante un booleano JSON -- para ArduinoJson son tipos
  distintos y nunca son "iguales", aunque representen lo mismo. El
  comando llega, el actuador no se mueve, y no hay ningún error a la
  vista.
- **Adentro de `onCommand()` no uses `delay()` ni lazos largos.** Ese
  código **no corre en `loop()`**: corre en la tarea que atiende el MQTT.
  Si la trabás unos segundos, el equipo deja de recibir y se puede
  desconectar solo. Para una acción que dura (un riego de 5 s, una
  cerradura), en el callback solo anotá qué hay que hacer y apagá desde
  `loop()`:

  ```cpp
  unsigned long regandoHasta = 0;

  tecnova.onCommand("riego", [](JsonVariant value) {
    digitalWrite(BOMBA, HIGH);
    regandoHasta = millis() + 5000;   // anotar, no esperar
  });

  void loop() {
    tecnova.loop();
    if (regandoHasta && millis() > regandoHasta) {
      digitalWrite(BOMBA, LOW);
      regandoHasta = 0;
    }
  }
  ```

> **De dónde venía el lío.** Hasta el 24 de septiembre de 2026 el botón
> del panel publicaba, tal cual, el texto libre de un campo llamado
> "Mensaje a enviar". Ese campo era opcional y solía quedar vacío, así
> que salía al aire `{"value":""}` -- que todo firmware razonable lee
> como "apagar". El campo ya no existe y ningún control lo lee.
>
> **Esto cambió en el panel, no en la librería**: aplica a tus
> dispositivos hoy, tengan la versión de TecnovaIoT que tengan.
> Actualizar la librería no cambia lo que publica el botón; lo que la
> 1.4.0 arregla es la **documentación y los ejemplos**, que enseñaban a
> leer el valor de una forma que ya no corresponde.
>
> Si tu código tiene la forma defensiva
> `v.is<bool>() ? v.as<bool>() : (v.as<String>() == "true")`, sigue
> funcionando, pero ya no hace falta: alcanza con `.as<bool>()`.

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
| [`RGBLed`](examples/RGBLed/RGBLed.ino) | **Actuador**: tres variables de salida (`rojo`, `verde`, `azul`), cada una con un **Deslizador** de 0 a 255 en el panel, mezcladas por PWM. Muestra también el caso de varias salidas en un mismo dispositivo. | Ninguna (solo `analogWrite`). |
| [`GPSTracker`](examples/GPSTracker/GPSTracker.ino) | Latitud/longitud leyendo un módulo GPS NEO-6M/NEO-M8N por UART. | `mikalhart/TinyGPSPlus` |
| [`DeepSleepSensor`](examples/DeepSleepSensor/DeepSleepSensor.ino) | Dispositivo a batería que se despierta, publica, y vuelve a dormir -- ver [Consumo de energía](#consumo-de-energía). | Ninguna. |
| [`NetworkTask`](examples/NetworkTask/NetworkTask.ino) | La librería en su propia tarea de FreeRTOS, **sin reinicios**, con `getState()` y `sendNow()`: la base para un equipo con pantalla -- ver [Dispositivos con pantalla](#dispositivos-con-pantalla-o-un-loop-que-no-puede-esperar). | Ninguna. |

Cada ejemplo trae en su propio encabezado el detalle de conexión física
(qué pin va a qué pata del sensor) y, si hace falta, la línea exacta para
agregar a tu `platformio.ini`.

## API

| Método | Qué hace |
|---|---|
| `TecnovaIoT(dId, password, jsonCapacity=4096)` | Constructor. `jsonCapacity` son los bytes reservados temporalmente para parsear la respuesta del webhook -- solo hace falta subirlo si tenés muchísimas variables. |
| `onCommand(nombreVariable, callback)` | Registra qué hacer cuando llega un comando para esa variable. Llamar antes de `begin()`. La variable tiene que estar marcada en el panel como **"El panel la acciona"**. El callback recibe el JSON completo (`{"value": ...}`) y el valor llega tipado según el control: booleano para interruptor y botón, número para deslizador. **No devuelve nada**: si el nombre no coincide con ninguna variable, el callback simplemente no se dispara nunca. **Corre en la tarea del MQTT, no en `loop()`** -- nada de `delay()` adentro. |
| `begin(ssid, password)` | Conecta WiFi, pide credenciales, conecta MQTT. Bloqueante; reinicia el ESP32 solo si algo falla. Si el WiFi ya está conectado (por ejemplo, porque lo conectó `TecnovaProvisioning` antes), no vuelve a intentarlo. Con `setAutoRestart(false)` no espera nada: vuelve enseguida y la conexión la hace `loop()`. |
| `loop()` | Llamar en cada vuelta de `loop()`. Publica variables vencidas. Si se corta el MQTT, se reconecta solo (a los 30 s sin volver pide credenciales de nuevo). **Si se corta el WiFi no lo reconecta: espera 15 s y reinicia el ESP32.** Con `setAutoRestart(false)` no reinicia nunca: reintenta de a pasos (ver [Dispositivos con pantalla](#dispositivos-con-pantalla-o-un-loop-que-no-puede-esperar)). |
| `setValue(nombreVariable, valor, save=false)` | Actualiza el valor de una variable (`float`, `int`, `bool`, `String` o `JsonVariant`). Se publica sola en el próximo ciclo, respetando la frecuencia configurada en el panel para esa variable. `save` indica si el backend debe guardar este valor en el historial. **Solo sirve en variables de entrada** ("El equipo la mide"): sobre una de salida devuelve `true` pero no publica nunca -- la librería avisa por el monitor serie la primera vez. |
| `isConnected()` | `true` si el MQTT está conectado ahora mismo. |
| `printStats(out=Serial)` | Debug: tabla con el estado de cada variable. Throttle interno, no imprime más seguido que cada 2s aunque la llames en cada `loop()`. |
| `enablePowerSave()` | Activa el modem-sleep de WiFi -- ahorra energía sin perder la sesión MQTT ni dejar de recibir comandos. Ver [Consumo de energía](#consumo-de-energía). |
| `deepSleepSeconds(segundos)` | Apaga el ESP32 en deep sleep durante ese tiempo. **Solo para dispositivos que nunca reciben comandos.** No retorna -- ver [Consumo de energía](#consumo-de-energía). |
| `setAutoRestart(activado)` | `true` (por defecto): el comportamiento de siempre, que reinicia el ESP32 ante un corte. `false`: nunca reinicia ni espera; `loop()` reintenta de a pasos. Llamar **antes** de `begin()`. Pensado para equipos con pantalla -- ver [Dispositivos con pantalla](#dispositivos-con-pantalla-o-un-loop-que-no-puede-esperar). |
| `getState()` | En qué paso está la conexión (un `TecnovaState`: `TECNOVA_WIFI_CONNECTING`, `TECNOVA_CONNECTED`, etc.). Se puede llamar desde cualquier tarea y en los dos modos: no toma candados ni espera a la red. |
| `stateName(estado)` | El estado en texto, sin tildes (`"conectando WiFi"`, `"conectado"`...), para el monitor serie o una pantalla. Es `static`: `TecnovaIoT::stateName(...)`. |
| `sendNow(nombreVariable)` | Publica ya el último valor de `setValue()`, sin esperar la frecuencia del panel: para un control que alguien acaba de tocar. No publica adentro de la llamada (lo hace `loop()`), así que se puede llamar desde cualquier tarea. Como mucho un envío cada 250 ms por variable; el **último** valor siempre sale, y si la conexión se cae (aunque sea justo en el envío) sale apenas vuelva. Eso vale entero con `setAutoRestart(false)`: en el modo por defecto, un corte de WiFi reinicia el ESP32 y 30 s sin MQTT rearman la lista de variables, y en los dos casos el pedido se pierde (hay que volver a llamar a `setValue()`). Devuelve `false` si la variable no existe o es de salida. |

## Consumo de energía

Si tu dispositivo va a funcionar a batería, esto es importante. MQTT
funciona por **empuje** (push): el broker manda el mensaje apenas alguien
publica algo, no hay forma de "pedirlo" después. Eso divide a los
dispositivos en dos familias, con estrategias de ahorro distintas -- y la
que corresponde depende de si tu dispositivo **recibe comandos o no**,
no de qué tan seguido publica.

### Dispositivos que SOLO publican (sensores)

Si tu dispositivo nunca tiene una variable marcada en el panel como "El
panel la acciona" con
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

  // Confirma que las credenciales cargadas sirvieron de verdad -- ver
  // "Qué pasa si cargás datos incorrectos" mas abajo.
  TecnovaProvisioning::confirmSuccess();
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
2. Al tocar **Guardar**, los datos quedan guardados en NVS (ver
   [Conceptos básicos](#conceptos-básicos-para-quien-recién-empieza)) en
   ese mismo momento -- **antes** de probar si sirven -- y el ESP32 intenta
   conectarse con esa red. Si conecta, sigue el arranque normal; si no, el
   portal sigue abierto para corregirlos. (Por eso, si cargás un
   `dId`/password equivocado, el equipo lo va a seguir usando hasta que lo
   corrijas: ver la recuperación automática, más abajo.)
3. **Próximos encendidos**: como ya está todo guardado, se conecta solo,
   sin mostrar nada.
4. **Si necesitás cambiar la configuración** más adelante (otra red WiFi,
   otro dispositivo): con el equipo ya prendido y funcionando, mantené
   apretado el botón **BOOT** 3 segundos. Se reinicia solo y vuelve a
   mostrar el portal.

### Qué pasa si cargás datos incorrectos (recuperación automática)

Puede pasar -- a propósito o por error -- que cargues un `dId`/password que
el panel rechaza, o una red WiFi que nunca conecta. En ese caso,
`TecnovaIoT`/`WiFiManager` reinician el ESP32 solos desde *adentro* de
`begin()`, sin llegar nunca a `loop()` -- y como `checkReconfigureButton()`
vive en `loop()`, **mantener apretado el botón no hace nada**: no hay
código corriendo todavía que lo esté escuchando. Sin ningún mecanismo
extra, el dispositivo quedaría reiniciando en loop para siempre, sin
forma de recuperarlo salvo reprogramarlo por USB.

Por eso `TecnovaProvisioning::begin()` lleva la cuenta (persistida en NVS)
de cuántos arranques seguidos NO terminaron en un
`TecnovaProvisioning::confirmSuccess()`. Al tercer arranque seguido sin
`confirmSuccess()` -- o sea, después de **dos** arranques fallidos --
**reabre el portal por su cuenta**, sin que haga falta tocar ningún
botón: solo hay que conectarse de nuevo a `TecnovaIoT-Setup` y cargar los
datos correctos esta vez.

Por esto es importante llamar a `confirmSuccess()` -- si tu sketch no lo
llama nunca, `begin()` va a pensar que TODOS los arranques fallan, y va a
terminar reabriendo el portal solo cada 3 reinicios aunque las
credenciales estén perfectas.

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

### Seguridad del portal

Mientras el portal está abierto, su red WiFi es **abierta** (sin
contraseña): cualquiera que esté cerca se puede conectar. Y una vez
conectado:

- Con la configuración por defecto, el formulario trae **precargado el
  password del dispositivo** que está guardado, y se puede leer. Con
  `setAutoRestart(false)` el campo viene vacío (vacío = no cambia).
- WiFiManager deja habilitadas, sin ninguna clave, sus páginas para
  **subir otro firmware** (`/update`) y para **borrar la configuración**
  (`/erase`). Un firmware ajeno podría leer lo que está guardado en el
  equipo, incluida la clave del WiFi.

Por eso: abrí el portal solo cuando lo vas a usar, cerralo (completándolo)
enseguida, y si el `dId` y el password del dispositivo quedaron a la vista
de alguien que no debía, cambiá el password del dispositivo en el panel.

### Referencia rápida del módulo

| Función | Qué hace |
|---|---|
| `TecnovaProvisioning::begin(wifiSsid, wifiPassword, deviceId, devicePassword, apName="TecnovaIoT-Setup", configButtonPin=0)` | Junta las 4 credenciales (de NVS o del portal) y deja el WiFi conectado. Bloqueante. Reabre el portal solo si los 2 arranques anteriores fallaron. Con `setAutoRestart(false)` no conecta el WiFi ni reinicia: devuelve lo guardado enseguida y abre el portal solo si faltan datos o se pidió reconfigurar. |
| `TecnovaProvisioning::confirmSuccess()` | Llamar justo después de que `tecnova->begin()` retorne. Resetea el contador de arranques fallidos que usa la recuperación automática. (Con `setAutoRestart(false)` no hace falta.) |
| `TecnovaProvisioning::checkReconfigureButton(configButtonPin=0, holdMs=3000)` | Llamar en cada `loop()`. Reabre el portal si se mantiene el botón apretado. |
| `TecnovaProvisioning::forget()` | Borra el `dId`/password guardados (no toca el WiFi), para forzar reconfiguración completa. |
| `TecnovaProvisioning::setAutoRestart(activado)` | Antes de `begin()`. `false`: modo para equipos con pantalla -- no cuenta arranques, no reinicia nunca y no conecta el WiFi (lo hace `TecnovaIoT`, también con `setAutoRestart(false)`). Un WiFi caído al arrancar **no** abre el portal. El formulario no trae precargado el password del dispositivo (vacío = no cambia). |
| `TecnovaProvisioning::setPortalTimeout(segundos)` | Antes de `begin()`. Cuánto espera el portal sin que nadie lo use. Por defecto 300; `0` = sin límite. |
| `TecnovaProvisioning::onPortalOpen(funcion)` | Antes de `begin()`. Función que se llama al abrirse el portal, con el nombre de la red que crea el equipo (por ejemplo, para mostrar un QR). Corre adentro del portal: solo anotar o encolar. |
| `TecnovaProvisioning::requestReconfigure()` | Pide abrir el portal desde el código (un botón en una pantalla táctil): guarda el pedido y reinicia. Es lo mismo que mantener el botón BOOT. |

## Dispositivos con pantalla (o un loop() que no puede esperar)

### Por qué hace falta otro modo

Con la configuración por defecto, la librería está pensada para un sensor
suelto: `begin()` espera al WiFi y al panel, y ante un problema (un corte
de WiFi, un panel que no contesta) **reinicia el ESP32** para empezar de
cero. Cuando nadie está mirando el equipo, es lo más simple y robusto.

En un equipo con pantalla, en cambio, `loop()` tiene que correr todo el
tiempo (dibujar, leer el táctil): mientras la librería espera, la pantalla
queda congelada, y un reinicio se ve como un apagón. Para esos casos está
`setAutoRestart(false)`:

- `begin()` no espera nada: guarda una copia de los datos y vuelve.
- `loop()` hace la conexión de a pasos (ver `getState()`) y reintenta solo
  lo que falle -- WiFi, credenciales, MQTT --, cada vez más espaciado y
  **sin reiniciar nunca**.
- Lo único que puede tardar es el pedido HTTPS de credenciales (1 a 3 s lo
  normal). Por eso la librería va en una **tarea propia** de FreeRTOS, y
  no en el `loop()` que dibuja.

Con la configuración por defecto nada de esto cambia: todo lo nuevo hay
que pedirlo.

### La receta

1. `setAutoRestart(false)` en las **dos** clases (`TecnovaIoT` y
   `TecnovaProvisioning`), antes de sus `begin()`.
2. `TecnovaProvisioning::begin()`, `tecnova->begin()` y `tecnova->loop()`
   van en una tarea propia, en el núcleo 0 (el del WiFi) y con 12 KB de
   pila: `xTaskCreatePinnedToCore(tareaRed, "red", 12288, NULL, 1, NULL, 0)`.
3. Esa tarea cede el núcleo en cada vuelta con
   `vTaskDelay(pdMS_TO_TICKS(20))`: el vigilante (*watchdog*) del núcleo 0
   está activo, y sin esa pausa también se traba el WiFi. Mientras el
   portal está abierto no hace falta hacer nada: el lazo de WiFiManager no
   cede el núcleo por su cuenta, así que `TecnovaProvisioning::begin()` baja
   la tarea a la prioridad mínima (la de la tarea IDLE, que es la que el
   vigilante espera ver correr) y al cerrar el portal la devuelve a la suya.

```cpp
#include <TecnovaIoT.h>
#include <TecnovaProvisioning.h>

// Colas de un solo lugar ("buzones"): cada dato nuevo pisa al anterior, así
// nunca se llenan, siempre queda el último valor y quien escribe no espera.
QueueHandle_t buzonNivel;                   // pantalla -> red: valor del deslizador
QueueHandle_t buzonLed;                     // tarea del MQTT -> pantalla: último comando
volatile uint8_t estadoRed = TECNOVA_IDLE;  // red -> pantalla (1 byte: se lee entero)

// Todo lo lento (portal, WiFi, HTTPS, MQTT) pasa acá, nunca en loop()
void tareaRed(void *parametro) {
  // 1. Datos de conexión: los guardados, o el portal si faltan. No reinicia.
  TecnovaProvisioning::setAutoRestart(false);
  String wifiSsid, wifiPassword, deviceId, devicePassword;
  TecnovaProvisioning::begin(wifiSsid, wifiPassword, deviceId, devicePassword);

  // 2. La conexión con el panel, también sin reinicios: begin() vuelve
  //    enseguida. El objeto lo usa SOLO esta tarea (loop() le habla por colas).
  TecnovaIoT *tecnova = new TecnovaIoT(deviceId, devicePassword);
  tecnova->setAutoRestart(false);
  tecnova->onCommand("led", [](JsonVariant value) {
    int32_t v = value["value"].as<bool>() ? 1 : 0;
    xQueueOverwrite(buzonLed, &v);   // corre en la tarea del MQTT: solo anotar
  });
  tecnova->begin(wifiSsid.c_str(), wifiPassword.c_str());

  int32_t nivel = 0;
  bool nivelPendiente = false;
  for (;;) {
    if (xQueueReceive(buzonNivel, &nivel, 0) == pdTRUE) nivelPendiente = true;
    // setValue() da false hasta tener credenciales: se reintenta en la próxima
    // vuelta. (int): en ESP-IDF 5, int32_t es long y la llamada sería ambigua.
    if (nivelPendiente && tecnova->setValue("nivel", (int)nivel)) {
      tecnova->sendNow("nivel");     // sale ya; como mucho uno cada 250 ms
      nivelPendiente = false;
    }
    tecnova->loop();                 // a veces tarda unos segundos (HTTPS): por eso va acá
    estadoRed = tecnova->getState();
    TecnovaProvisioning::checkReconfigureButton();   // BOOT 3 s: reconfigurar
    vTaskDelay(pdMS_TO_TICKS(20));   // cede el núcleo: así corren el WiFi y el vigilante
  }
}

void setup() {
  Serial.begin(921600);
  buzonNivel = xQueueCreate(1, sizeof(int32_t));
  buzonLed = xQueueCreate(1, sizeof(int32_t));
  // ... iniciar la pantalla y la interfaz ...
  // Núcleo 0, el del WiFi: el 1 queda para loop() y la pantalla.
  xTaskCreatePinnedToCore(tareaRed, "red", 12288, NULL, 1, NULL, 0);
}
```

Sin portal (con los datos escritos en el código) es todavía más simple: el
objeto puede ser global, y tu `loop()` puede llamar directamente a
`setValue()`, `sendNow()` y `getState()`. Es lo que hace
[`examples/NetworkTask`](examples/NetworkTask/NetworkTask.ino). Con el
portal, en cambio, el objeto recién se puede crear adentro de la tarea
(los datos llegan después del portal), y por eso en el ejemplo de arriba
`loop()` no lo toca nunca.

### Qué corre en qué tarea

| Llamada | Tarea | Regla |
|---|---|---|
| `TecnovaProvisioning::begin()` y el portal | La tarea de red | Bloquea mientras el portal está abierto. |
| Callback de `onPortalOpen()` | Adentro del portal, en la tarea de red | Solo anotar o encolar. |
| `begin()` y `loop()` | La tarea de red | Solo esa tarea las llama. |
| Callback de `onCommand()` | La tarea interna del MQTT | Solo anotar o encolar; nada de `delay()` ni de pantalla. |
| `setValue()`, `sendNow()`, `getState()`, `stateName()`, `isConnected()` | Cualquiera | No esperan a la red. |
| `printStats()` | Cualquiera | Imprime con el candado tomado: no la llames desde la tarea de la pantalla. |
| `checkReconfigureButton()`, `requestReconfigure()`, `confirmSuccess()`, `forget()` | La misma tarea que `TecnovaProvisioning::begin()` | Comparten la memoria NVS. |

### LVGL (o cualquier librería gráfica) y las tareas

LVGL no es *thread-safe*: si dos tareas lo tocan a la vez, la memoria se
corrompe y el equipo se cuelga de forma aleatoria, días después. Las
reglas son dos:

- **Nunca** llames a una función `lv_*` desde un callback de `onCommand()`
  u `onPortalOpen()`, ni desde la tarea de red.
- Los datos van por una cola y se aplican desde un `lv_timer`, que corre
  adentro de `lv_timer_handler()` -- o sea, en `loop()`, la única tarea que
  toca la pantalla.

```cpp
lv_obj_t *interruptorLed;   // creados al armar la interfaz
lv_obj_t *etiquetaRed;

// Corre adentro de lv_timer_handler(), o sea en loop(): acá sí se toca LVGL
void aplicarRed(lv_timer_t *timer) {
  int32_t led;
  if (xQueueReceive(buzonLed, &led, 0) == pdTRUE) {
    if (led) lv_obj_add_state(interruptorLed, LV_STATE_CHECKED);
    else lv_obj_remove_state(interruptorLed, LV_STATE_CHECKED);
  }
  static uint8_t estadoMostrado = 255;
  if (estadoRed != estadoMostrado) {
    estadoMostrado = estadoRed;
    lv_label_set_text(etiquetaRed, TecnovaIoT::stateName((TecnovaState)estadoMostrado));
  }
}

// El evento del deslizador también corre en loop(): solo deja el valor
void alMoverNivel(lv_event_t *e) {
  int32_t v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  xQueueOverwrite(buzonNivel, &v);
}

// En setup(), después de crear la interfaz:
//   lv_timer_create(aplicarRed, 50, NULL);
//   lv_obj_add_event_cb(deslizador, alMoverNivel, LV_EVENT_VALUE_CHANGED, NULL);
```

### Estados

| `getState()` | Qué significa | Qué hacer |
|---|---|---|
| `TECNOVA_IDLE` | Todavía no se llamó a `begin()`. | -- |
| `TECNOVA_WIFI_CONNECTING` | Esperando el WiFi (al arrancar o porque se cortó). | Nada: reintenta solo. Si no conecta nunca, revisá la red y su clave (y reconfigurá). |
| `TECNOVA_FETCHING_CREDENTIALS` | Pidiendo las credenciales MQTT al panel (HTTPS). | Nada. |
| `TECNOVA_CREDENTIALS_REJECTED` | El panel contestó que el `dId` o el password no valen (HTTP 401, 403 o 404). | Revisar el dispositivo en el panel, o reconfigurar. Reintenta cada 5 min. |
| `TECNOVA_SERVER_UNAVAILABLE` | El pedido falló por otra causa: sin Internet, certificado, error del servidor o JSON inválido. | Nada: reintenta solo. El monitor serie dice la causa (por ejemplo, `Detalle TLS`). |
| `TECNOVA_MQTT_CONNECTING` | Con credenciales; esperando que el broker acepte la sesión. | Nada. |
| `TECNOVA_CONNECTED` | Sesión MQTT activa: se publica y llegan comandos. | -- |

`getState()` funciona también en el modo por defecto (ahí no se ven
nunca los dos estados de falla por mucho tiempo: la librería reinicia).
Con `setAutoRestart(false)`, cada cambio de estado sale además por el
monitor serie (`[TecnovaIoT] Estado: ...`).

### Qué hace ante cada problema

| Qué pasa | Qué hace la librería | Estado | Pedidos al webhook |
|---|---|---|---|
| No hay WiFi al arrancar | `begin()` vuelve al instante. El núcleo del ESP32 reintenta solo, y la librería le da un empujón a los 15, 45 y 105 s, y después cada 60 s. | `WIFI_CONNECTING` | 0 |
| Se corta el WiFi con el equipo andando | Pasa a `WIFI_CONNECTING` al instante. A los 10 s da por muerta la sesión MQTT vieja, sin bloquear. Cuando vuelve el WiFi, esp-mqtt reconecta solo en 10 s o menos. | `WIFI_CONNECTING` → `MQTT_CONNECTING` → `CONNECTED` | 0 (1 si en 30 s no reconecta) |
| Se corta Internet, pero el WiFi sigue andando | Nada avisa del corte: esp-mqtt lo nota cuando su ping (*keepalive*, de 30 s en este modo) queda sin respuesta, en menos de un minuto. Mientras tanto el estado sigue en `CONNECTED`. Después espera 30 s a que esp-mqtt reconecte y recién ahí pide credenciales, que fallan. En total, hasta un par de minutos hasta `SERVER_UNAVAILABLE`. | `CONNECTED` → `MQTT_CONNECTING` → `SERVER_UNAVAILABLE` | Como en la fila del webhook que falla |
| Cambiaron las credenciales MQTT en el servidor | El broker las rechaza (CONNACK 4 o 5) y se piden de nuevo enseguida, si la espera anti-tormenta lo permite. | `MQTT_CONNECTING` → `FETCHING_CREDENTIALS` → ... | 1 |
| El webhook falla (error 5xx, sin respuesta, certificado o JSON inválidos) | Reintenta a los 5, 10, 20, 40, 80 y 120 s, y después cada 120 s (más un 0-20 % al azar). Cada intento dura como mucho unos 35 s: DNS 15 s (fijo del núcleo), TCP 5, TLS 10 y lectura 5. | `SERVER_UNAVAILABLE` | Como mucho 1 cada 2 min, ya estabilizado |
| El panel responde 401, 403 o 404 | Reintenta cada 5 min (más el azar). | `CREDENTIALS_REJECTED` | 1 cada 5 min |
| El webhook anda, pero el MQTT nunca conecta | Pide credenciales cada 30 s o lo que diga la espera anti-tormenta, lo que sea mayor. | `MQTT_CONNECTING` | A los 30, 30, 30, 40, 80, 120... s |
| Cambió la clave del router | Se queda en `WIFI_CONNECTING`, con un empujón cada 60 s. **No abre el portal solo**: hay que reconfigurar a mano (botón BOOT o `requestReconfigure()`). | `WIFI_CONNECTING` | 0 |

La espera entre pedidos al webhook es una sola para todos los caminos, y
vuelve a cero cuando el MQTT conecta. El azar existe para que los equipos
de un aula que se recuperan del mismo corte no le pregunten al panel
todos en el mismo segundo.

En este modo la librería nunca llama a `delay()` ni a `ESP.restart()`. Lo
que sí puede demorar una vuelta de `loop()` -- siempre con un tope -- es
el pedido HTTPS (hasta unos 35 s; lo normal es de 1 a 3 s), cerrar el
cliente MQTT para pedir credenciales nuevas (hasta unos 11 s si estaba a
mitad de un intento) y una publicación con la red trabada (hasta 10 s).
Por eso va en su propia tarea.

### Tres cuidados más

- **El paquete de CA es global.** La librería lo carga para todo el ESP32
  (ver [Sobre el certificado TLS](#sobre-el-certificado-tls)). Si tu
  proyecto tiene otras conexiones HTTPS, no llames a `setCACertBundle()`
  (ni cargues otro paquete) mientras corre la librería.
- **Contraseñas en el monitor serie.** La librería nunca imprime
  contraseñas. Pero WiFiManager sí, si le subís el nivel de mensajes: con
  `WM_DEBUG_LEVEL` en `WM_DEBUG_VERBOSE` (o más) imprime la clave del
  dispositivo al guardar el portal (`device_pass:...`), y con
  `WM_DEBUG_DEV`, también la del WiFi. Lo mismo puede pasar con
  `CORE_DEBUG_LEVEL` en *verbose*. Usalos solo para depurar, y no
  compartas ese registro.
- **El portal es una red abierta**, con las páginas de WiFiManager para
  subir firmware y borrar la configuración habilitadas: ver
  [Seguridad del portal](#seguridad-del-portal). En una pantalla táctil,
  donde se abre con un par de toques, conviene que el botón pida
  confirmación.

## Sobre el certificado TLS

La librería trae embebido (`src/TecnovaRootCaBundle.h`) un bundle mínimo de
Autoridades Certificadoras raíz (GlobalSign Root CA + ISRG Root X1) --
necesario para validar el certificado que presenta el servidor. Desde la
1.5.0 se usa en las **dos** conexiones: la del broker (MQTT sobre WSS) y
el pedido de credenciales al webhook (HTTPS), que hasta la 1.4.0 no
validaba nada (`setInsecure()`). Validar ese pedido importa: lleva el
`dId` y el password del dispositivo, y la respuesta trae el usuario y la
clave MQTT. Sin validar, cualquiera en la misma red podía hacerse pasar
por el servidor y quedarse con todo.

Es el mismo para **cualquier** dispositivo de **cualquier** usuario que
hable con esta plataforma -- no hay que regenerarlo por dispositivo,
porque valida al *servidor*, no al dispositivo que se conecta.

Si el certificado no valida, el monitor serie muestra
`Error del webhook: HTTP -1` y, abajo, una línea `Detalle TLS: ...` con
la causa (por ejemplo, `X509 - Certificate verification failed`). La
librería nunca cae a una conexión sin validar. Si el `-1` es por un
problema de red (el servidor no contesta, no hay Internet, se agotó el
tiempo de la conexión o del saludo TLS), esa línea no sale: el núcleo usa
el mismo `-1` para todas esas fallas, y mostrarlo como detalle de TLS
("Generic error") despistaría.

Dos detalles técnicos:

- **El bundle es estado global del ESP32**, no de cada conexión. La
  librería lo carga en `begin()` y lo vuelve a cargar en cada pedido de
  credenciales, siempre con el cliente MQTT detenido (si se cambiara
  mientras el MQTT valida un certificado, leería memoria ya liberada).
- **Desde ESP-IDF 5.4** (arduino-esp32 3.2 en adelante) cambió el formato
  binario que espera ESP-IDF. El archivo queda siempre en el formato
  viejo y la librería lo traduce en memoria al arrancar.

Solo habría que regenerarlo si el servidor rotara a una Autoridad
Certificadora fuera de esas dos. Hoy la cadena valida gracias al
certificado de GTS Root R4 firmado de forma cruzada por GlobalSign Root
CA, que vence el 28 de enero de 2028; está previsto ampliar el paquete en
una 1.5.x. Para revisar la cadena real de un servidor (reemplazando
`<host>` por el que corresponda):

```bash
openssl s_client -connect <host>:443 -showcerts
```

## Compatibilidad de versiones del core ESP32

| Núcleo | Estado |
|---|---|
| arduino-esp32 2.0.x (ESP-IDF 4.4; en PlatformIO, `platform = espressif32` 6.x) | Probado en hardware. Es con el que corren los equipos en producción. |
| arduino-esp32 3.x (ESP-IDF 5.x; en PlatformIO, la plataforma de pioarduino) | Compila (se compiló con 3.1.3 y 3.3.12); **no probado en hardware** todavía. |

Entre un núcleo y otro hay tres diferencias, y la librería las resuelve
sola al compilar, según la versión de ESP-IDF (`ESP_IDF_VERSION_MAJOR`):

- El struct de configuración del cliente MQTT: plano en IDF 4.x y anidado
  en IDF 5.x.
- Las funciones del paquete de CA: en 2.x, las de la copia que trae
  Arduino (`arduino_esp_crt_bundle_*`); en 3.x esa copia no existe y se
  usan las de ESP-IDF (`esp_crt_bundle_*`).
- El formato binario del paquete, que cambió en ESP-IDF 5.4: se traduce
  en memoria (ver [Sobre el certificado TLS](#sobre-el-certificado-tls)).

Hasta la 1.4.0 este README decía que la librería compilaba con 3.x, y no
era cierto: usaba `arduino_esp_crt_bundle_*`, que en 3.x no existe. Se
corrigió en la 1.5.0.

`TecnovaProvisioning` depende además de WiFiManager: la 2.0.17 compila con
arduino-esp32 3.3.12, pero no se probó en hardware. Con 3.x el programa
crece: `examples/CaptivePortal` ocupa unos 1,3 MB y no entra en la
partición de programa por defecto de un ESP32 de 4 MB (1,25 MB). Se
arregla con otra tabla de particiones, por ejemplo agregando
`board_build.partitions = min_spiffs.csv` al `platformio.ini`.

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
- **`Error del webhook: HTTP -1` y, abajo, `Detalle TLS: X509 - Certificate
  verification failed`**: el certificado que presentó el servidor no
  valida contra el paquete de CA de la librería. Desde la 1.5.0 el pedido
  de credenciales se valida igual que el MQTT (ver [Sobre el certificado
  TLS](#sobre-el-certificado-tls)), así que puede ser que algo en la red
  esté interceptando la conexión, o que el servidor haya cambiado de
  Autoridad Certificadora. En ese caso el MQTT tampoco conectaría: hay que
  regenerar el paquete. Un `HTTP -1` **sin** línea `Detalle TLS` es un
  problema de red: el DNS no resolvió, no hay Internet, un firewall
  bloquea el puerto 443 o el servidor no contestó a tiempo.
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
  variables de las que tu código espera**: es que el nombre no coincide
  con ninguna variable configurada para ese dispositivo en el panel. Los
  dos métodos avisan distinto: `setValue()` **devuelve `false`** (revisá
  el valor de retorno), mientras que `onCommand()` **no devuelve nada**
  -- ahí el síntoma es que el callback nunca se dispara. En los dos
  casos, `printStats()` te lista los nombres que el panel devolvió de
  verdad, que es la forma rápida de comparar. La comparación **no
  distingue mayúsculas de minúsculas** (`"Temperatura"` y
  `"temperatura"` matchean igual), pero el resto del texto sí tiene que
  ser idéntico -- typos, espacios de más, tildes, etc. sí importan.
- **`onCommand()` se dispara pero el actuador nunca "prende"**: casi
  siempre es que estás comparando el valor contra el **texto** `"true"`.
  El panel manda el valor tipado -- un interruptor publica el booleano
  JSON `{"value":true}` -- y para ArduinoJson un booleano y el texto
  `"true"` son tipos distintos, que **nunca** son "iguales" entre sí
  aunque representen lo mismo. Así que `value["value"] == "true"` da
  `false` siempre. Se lee con `value["value"].as<bool>()`. El síntoma es
  engañoso: `printStats()`/`Last incoming msg` muestran que el comando
  llegó bien y el `Count` de la variable sube -- el problema está
  puntualmente en la comparación de tipos, no en la conexión.
- **El actuador funciona, pero el panel "no muestra" en qué estado
  quedó**: es esperable. Una variable de salida no se publica nunca, y el
  widget del interruptor refleja el comando que él mismo mandó, no un
  reporte del equipo. Si llamás a `setValue()` sobre esa variable te va a
  devolver `true` y no va a salir nada al aire -- desde la 1.4.0 la
  librería te lo avisa una vez por el monitor serie. Si querés que el
  equipo informe su estado real (por ejemplo, para confirmar que el relé
  cerró), creá en el panel una **segunda variable de entrada**, del tipo
  "El equipo la mide", y publicá esa.
- **Aprieto el botón, el LED prende, pero nunca apaga**: un **Botón de
  pulso** manda siempre `true`, no tiene estado; sirve para abrir una
  cerradura o dar un riego, no para encender y apagar. Cambiá el widget
  por un **Interruptor**.
- **Antes del 25 de septiembre de 2026 el botón SOLO apagaba**: era otro
  problema, el opuesto, y ya está resuelto del lado del panel. El botón
  publicaba el texto libre de un campo "Mensaje a enviar" que casi
  siempre quedaba vacío, así que salía `{"value":""}` y cualquier
  firmware lo leía como "apagar". Ese campo ya no existe. **Es un cambio
  del panel, no de la librería**: vale para cualquier versión que tengas
  instalada, y actualizar la librería no cambia lo que publica el botón.
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
