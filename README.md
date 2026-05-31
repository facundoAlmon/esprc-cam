<div align="right">
<span>Español | <a href="README.en.md">English</a></span>
</div>

# 📷 ESP-RC Cam: Visión FPV para tu Coche RC

¡Bienvenido al repositorio de ESP-RC Cam! Este proyecto es el módulo de cámara del sistema ESP-RC Car, construido sobre un módulo AI-Thinker ESP32-CAM con sensor OV2640. Transmite video en vivo por Wi-Fi en formato MJPEG directamente a la webapp del cerebro del coche, dándole a tu RC una verdadera visión en primera persona.

Este módulo está diseñado para trabajar en conjunto con **[esprc-brain](../esprc-brain/)**, aunque también puede usarse de forma independiente desde cualquier navegador.

## ✨ Características Principales

-   **Streaming MJPEG en vivo:** Transmisión de video fluida a través del protocolo MJPEG estándar en el **puerto 81**. Compatible con cualquier navegador o reproductor de video. La webapp del brain lo muestra con un simple `<img>`, sin JavaScript adicional.

-   **Streaming WebSocket:** Canal alternativo de frames JPEG binarios por WebSocket (`/ws`), utilizado por la webapp del brain para integración avanzada.

-   **Integración automática con esprc-brain:** El brain descubre la cámara automáticamente vía **mDNS** (hostname `esprc-cam.local`) cada 30 segundos, sin necesidad de configuración manual de IP.

-   **Interfaz Web Integrada:** Aplicación web embebida en el firmware con 4 pestañas: Stream en vivo, Configuración de imagen, Conexión Wi-Fi y Actualización OTA.

-   **Configuración de Imagen Completa:** Ajuste en tiempo real de resolución (QQVGA a UXGA), calidad JPEG, brillo, contraste, saturación, exposición, AWB, AGC, corrección de lente, espejo y volteo, entre otros.

-   **Sincronización desde el Brain:** Botón "Sync from Brain" en la pestaña de Conexión que recupera automáticamente las credenciales Wi-Fi del brain, facilitando la configuración inicial en redes locales.

-   **Actualización OTA:** Actualizá el firmware de la cámara sin cables desde la pestaña "Actualizar" de la webapp, con barra de progreso y confirmación.

-   **Firmware Puro ESP-IDF:** Escrito en C++ sobre ESP-IDF 6.0 sin dependencias de Arduino. Doble partición OTA para actualizaciones seguras.

-   **Modos de Conectividad:**
    -   **Access Point (AP):** La cámara crea su propia red Wi-Fi `ESPRC-CAM` para acceso directo.
    -   **Modo Cliente (STA):** La cámara se conecta a tu red local, ideal para integración con el brain.

## 📂 Estructura del Proyecto

```
esprc-cam/
└── Firmware/
    ├── main/
    │   ├── main.c                  # Punto de entrada → app_task_start()
    │   ├── index.html              # Webapp compilada (generada por gulp)
    │   └── src/
    │       ├── main.cpp            # Init WiFi (AP/STA), NVS, cámara, servidor web
    │       ├── camera_driver.cpp   # Init OV2640 con PSRAM, camera_apply_settings()
    │       ├── mjpeg_server.cpp    # Servidor TCP raw MJPEG (puerto 81, core 0)
    │       ├── streamer.cpp        # Tarea de streaming WebSocket (core 1)
    │       ├── webserver.cpp       # API REST, OTA, WebSocket, redirects
    │       ├── nvs_prefs.cpp       # Wrapper NVS clave-valor
    │       └── dns_server.c        # DNS cautivo para modo AP
    ├── webapp/
    │   ├── src/                    # Fuentes HTML/CSS/JS
    │   ├── gulpfile.js             # Pipeline: Rollup + inline-source
    │   └── package.json
    ├── partitions.csv              # Tabla de particiones OTA dual (4 MB)
    ├── sdkconfig.defaults
    └── CMakeLists.txt
```

## 🚀 Primeros Pasos

### Requisitos Previos

1.  **Hardware:**
    -   Módulo **AI-Thinker ESP32-CAM** (ESP32, sensor OV2640, 4 MB flash, 4 MB PSRAM).
    -   Adaptador **FTDI** (USB-serial) para flashear: conectar `TX→RX`, `RX→TX`, `GND→GND`, `3.3V→3.3V` (o 5V según tu módulo). Para entrar en modo flash, conectar **GPIO0 → GND** antes de encender y presionar RST.

2.  **Software:**
    -   [ESP-IDF v6.0.1](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/get-started/index.html): entorno de desarrollo de Espressif (versión requerida: **v6.0.1**).
    -   [Node.js y npm](https://nodejs.org/): para compilar la webapp (solo si querés modificarla).
    -   [Git](https://git-scm.com/): para clonar el repositorio.

### Preparar el Firmware

1.  **Cloná el repositorio:**
    ```bash
    git clone https://gitlab.com/falmon/esprc-cam.git
    cd esprc-cam/Firmware
    ```

2.  **Activá el entorno ESP-IDF v6.0.1** (necesario en cada nueva sesión de terminal):
    ```bash
    . /ruta/a/esp-idf-v6.0.1/export.sh
    ```
    > Si instalaste IDF con el instalador oficial, usá el script `export.sh` de tu instalación local.

3.  **Seleccioná el target** (una sola vez por clon):
    ```bash
    idf.py set-target esp32
    ```

4.  **Compilá el firmware:**
    ```bash
    idf.py build
    ```

5.  **Flasheá el ESP32-CAM:**
    Conectá GPIO0 a GND, presioná RST para entrar en modo flash, y ejecutá:
    ```bash
    idf.py -p /dev/ttyUSB0 flash monitor
    ```
    Soltá GPIO0 después de que comience la transferencia. El monitor serie mostrará los logs de arranque.

> **Nota:** Si es la primera vez que flasheás o cambiaste el target, eliminá el `sdkconfig` para que se regenere limpio: `rm -f sdkconfig`

### Desarrollo de la WebApp (Opcional)

La webapp usa **Gulp.js** para empaquetar todo (HTML, CSS, JS) en un único `index.html` que se incrusta en el firmware.

1.  **Instalá las dependencias:**
    ```bash
    cd Firmware/webapp
    npm install
    ```

2.  **Comandos disponibles:**
    -   `npm run build`: Compila y copia `index.html` a `Firmware/main/`. Luego re-compilar y flashear el firmware para aplicar los cambios.
    -   `npm run serve`: Servidor local en `http://localhost:8080` para desarrollar sin flashear.
    -   `npm run clean`: Elimina los artefactos de compilación.

> **Nota:** El bundler usa `gulp-inline-source` con `compress:true` que **no soporta optional chaining (`?.`) ni nullish coalescing (`??`)**. Usá siempre guardas explícitas con `&&`.

## 🔧 Guía de Uso

### Primera Conexión

Por defecto, el ESP32-CAM arranca en **Modo Access Point (AP)**.

1.  **Conectate a la red Wi-Fi:** Buscá la red **`ESPRC-CAM`** (sin contraseña) desde tu teléfono o PC.
2.  **Abrí la interfaz web:** Navegá a [http://esprc-cam.local](http://esprc-cam.local) o [http://192.168.4.1](http://192.168.4.1).
3.  **¡A ver la imagen!** Ya estás en la pestaña de Stream. Presioná ▶ para iniciar la transmisión.

### Guía Detallada de la Interfaz Web

La webapp tiene 4 pestañas accesibles desde la barra lateral (escritorio) o la barra inferior (móvil).

---

#### 📹 Stream

La pestaña principal. Muestra el video en vivo de la cámara.

-   **▶ Iniciar / ■ Detener:** Conecta o corta el stream MJPEG.
-   **Pantalla completa:** Botón en la esquina de la imagen para ver el video en pantalla completa. En pantalla completa se muestra el badge de FPS en tiempo real.
-   **Badge de estado:** Indica si el stream está activo u offline.

> El stream usa el elemento `<img>` del navegador apuntando a `http://{ip}:81/mjpeg`. No se necesita JavaScript para reproducirlo — el navegador lo maneja nativamente.

---

#### ⚙️ Config

Configuración completa del sensor OV2640. Todos los cambios se guardan en la memoria NVS del ESP32 y se aplican al reiniciar.

**Stream:**
| Parámetro | Descripción |
|-----------|-------------|
| Resolución | De QQVGA (160×120) hasta UXGA (1600×1200). QVGA (320×240) por defecto. |
| Calidad | Compresión JPEG: 4 (mejor calidad) a 63 (mayor compresión). |
| Límite FPS | Limita los cuadros por segundo (0 = sin límite). |
| Estadísticas FPS | Activa el conteo de FPS en tiempo real visible en el stream. |

**Imagen:**
| Parámetro | Descripción |
|-----------|-------------|
| Brillo | -2 a +2 |
| Contraste | -2 a +2 |
| Saturación | -2 a +2 |
| Espejo H | Voltea la imagen horizontalmente. |
| Voltear V | Voltea la imagen verticalmente. |

**Configuración Avanzada** (desplegable):
Exposición manual (AEC), ganancia (AGC), balance de blancos (AWB), corrección de lente, efecto especial, modo WB, y más — controles finos para condiciones de iluminación específicas.

---

#### 📡 Conexión

Configuración de red del dispositivo.

**Wi-Fi:**
-   **Modo:** Elige entre **Punto de Acceso** (la cámara crea su propia red) o **Cliente Wi-Fi** (la cámara se conecta a una red existente).
-   **SSID / Contraseña / Hostname:** Credenciales de la red y nombre mDNS del dispositivo (`esprc-cam` por defecto).
-   **Guardar y Reiniciar:** Aplica los cambios de red y reinicia automáticamente.
-   **Sincronizar desde Brain:** Consulta el brain vía mDNS y descarga automáticamente las credenciales Wi-Fi guardadas en él. Ideal para configurar la cámara en la misma red que el brain con un solo clic.

**Dispositivo:**
-   Muestra la IP actual del ESP32-CAM.
-   Botones para **Reiniciar** o hacer **Reset de Fábrica** (borra toda la configuración guardada).

---

#### 🔄 Actualizar

Actualización de firmware por OTA (Over The Air) sin necesidad de cables.

1.  Consultá la información del firmware actual (partición activa, versión, fecha de compilación).
2.  Seleccioná el archivo binario `esprc_cam.bin` generado por `idf.py build` en `Firmware/build/`.
3.  Presioná **Subir y Reiniciar**. La barra de progreso indica el avance.
4.  Al completarse, el dispositivo reinicia automáticamente con el nuevo firmware.

> **Advertencia:** Subir un firmware inválido puede inutilizar el dispositivo hasta que se reflashee por USB.

## 🔗 Integración con esprc-brain

Este módulo está diseñado para funcionar como complemento de **[esprc-brain](../esprc-brain/)**. La integración es automática:

1.  **Descubrimiento automático:** El brain ejecuta `discover_camera_task` (FreeRTOS, cada 30 s) que consulta mDNS por `esprc-cam`. Al encontrar la IP, la guarda en NVS y la expone en `GET /api/camera`.

2.  **Auto-conexión de la webapp del brain:** Al abrir la webapp del brain, esta llama a `/api/camera` y conecta el stream MJPEG automáticamente si la cámara está disponible en la red.

3.  **Configuración de cámara desde el brain:** La pestaña "Cámara" de la webapp del brain envía requests directamente a `http://{camIP}/api/config`. El brain no actúa como proxy; el navegador se conecta directamente.

4.  **Estadísticas en tiempo real:** La webapp del brain sondea `http://{camIP}/api/stats` cada 2 s mientras la pestaña de cámara está abierta.

5.  **Orden de flasheo recomendado:** Flashear la cámara **primero** para que el brain la descubra al arrancar.

Para más detalles sobre la integración, ver [../CLAUDE.md](../CLAUDE.md).

## 🤝 ¿Querés Contribuir?

¡Las contribuciones son bienvenidas! Si tenés una mejora, encontraste un bug o querés agregar una funcionalidad:

1.  Hacé un **Fork** del repositorio.
2.  Creá una nueva rama (`git checkout -b feature/mi-mejora`).
3.  Realizá tus cambios y hacé commit (`git commit -m 'Agrego mi mejora'`).
4.  Subí tu rama (`git push origin feature/mi-mejora`).
5.  Abrí un **Pull Request**.

## 📝 Tareas Pendientes (ToDo)

-   [ ] Agregar soporte para streaming ESP-NOW (módulo `espnow_cam.cpp` en progreso).
-   [ ] Agregar un esquema del circuito de conexión FTDI → ESP32-CAM.

## 🙏 Agradecimientos

-   **[Espressif Systems](https://github.com/espressif):** Por el componente [esp32-camera](https://github.com/espressif/esp32-camera) y el framework ESP-IDF.
-   **[Benoît Blanchon](https://github.com/bblanchon):** Por la librería [ArduinoJson](https://github.com/bblanchon/ArduinoJson).

## 📜 Licencia

Este proyecto está distribuido bajo la **Licencia MIT**. Sos libre de usar, modificar y distribuir el código siempre que mantengas el aviso de copyright original.

> El firmware utiliza exclusivamente componentes nativos de ESP-IDF y librerías bajo licencias permisivas (MIT/Apache 2.0).

---
Hecho con ❤️, ☕ y muchos cables por [Facundo Almon](https://github.com/facundoAlmon).
