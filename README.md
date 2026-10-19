# VANT-IA-MK1: control por gestos

La computadora procesa la cámara con **OpenCV + YOLO** y envía intenciones al **ESP32-S3** por TCP/Wi-Fi. El ESP32-S3 ejecuta el lazo local de control. El repositorio separa el control del código particular de pines y drivers para que el integrante que ya definió el hardware conecte su implementación mediante `board_port.hpp`.

```text
PC: cámara → YOLO → filtro de gestos → enlace TCP
ESP32-S3: enlace TCP → máquina de estados → control de actitud → 4 salidas normalizadas
                                                       ↗ adaptador de placa
```

## Qué incluye

| Módulo | Función |
| --- | --- |
| `ctrl_dron/vision.py` | Captura con OpenCV, infiere con YOLO, filtra gestos y envía órdenes. |
| `ctrl_dron/train.py` | Entrena un modelo propio con imágenes etiquetadas. |
| `ctrl_dron/simulator.py` | Receptor local para probar el enlace sin placa. |
| `esp32/main.py` | Receptor MicroPython de diagnóstico; registra órdenes, sin salidas. |
| `firmware/core/flight_core.hpp` | Estados, límites, mezcla de cuatro motores y parada por fallo; C++ portátil. |
| `firmware/main/main.cpp` | Proyecto ESP-IDF: Wi-Fi, TCP, cola de órdenes y lazo local de 200 Hz. |
| `firmware/main/board_port.hpp` | Interfaz que une el núcleo con lectura de actitud, habilitación y cuatro salidas PWM. |
| `firmware/main/board_stub.cpp` | Adaptador de compilación que mantiene las salidas desactivadas. |

El firmware ESP-IDF **no activa motores mientras se use `board_stub.cpp`**. El núcleo exige parámetros válidos, sensor válido, enlace reciente y habilitación antes de aceptar `ARM`. El equipo de integración sustituye el stub por su implementación de `board_port.hpp`. Véase [contrato del control](docs/CONTROL.md).

## Computadora: instalar y probar

Se necesita Python 3.10+.

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python -m ctrl_dron.simulator
```

En otra terminal, con una cámara y pesos YOLO de gestos:

```bash
python -m ctrl_dron.vision --model /ruta/gestos.pt --camera 0 --host 127.0.0.1
```

Para usar el ESP32-S3, cambiar `--host` por su IP. Sin `--host`, se muestran las detecciones sin transmitir. En la ventana: `a` envía `ARM`, `t` envía `TAKEOFF`, `d` envía `DISARM`, `e` envía `ESTOP` y `q` termina. **Los gestos nunca generan `ARM`, `TAKEOFF` ni `DISARM`**; esas órdenes son manuales. Si llegan cuadros pero no hay una detección única y estable, se envía `NEUTRAL`. Si dejan de llegar cuadros, el emisor deja de renovar el enlace y actúa el watchdog del ESP32-S3.

El archivo [config/gestures.json](config/gestures.json) asigna etiquetas del modelo a órdenes. Un YOLO genérico no contiene esas clases. Se necesitan imágenes etiquetadas y pesos entrenados para el proyecto. El [ejemplo de dataset](training/data.example.yaml) define las clases. Para entrenar:

```bash
python -m ctrl_dron.train --data /ruta/data.yaml --base-model /ruta/modelo_base.pt
```

## ESP32-S3: firmware de control

El proyecto está en `firmware/` y está orientado a ESP-IDF 5.5. Una vez implementado `board_port.hpp` para la placa del equipo y sustituido `board_stub.cpp` en `firmware/main/CMakeLists.txt`:

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig  # VANT-IA-MK1: SSID, clave y puerto
idf.py build
idf.py flash monitor
```

`board_flight_config()` aporta los parámetros medidos del controlador. Con valores vacíos el núcleo no arma. `board_read_attitude()` entrega orientación y velocidad angular ya calibradas; `board_write_motors()` convierte cuatro valores normalizados de 0 a 1 a las salidas de los motores. El orden es delantero izquierdo, delantero derecho, trasero derecho, trasero izquierdo. El adaptador debe dejar las cuatro salidas en cero al iniciar y ante cualquier fallo. El diseño de estados y la semántica de cada orden están en [docs/CONTROL.md](docs/CONTROL.md).

Para probar únicamente comunicación con MicroPython, copiar `esp32/config.example.py` a `esp32/config.py`, completar red, copiar `esp32/main.py` y usar su IP. `esp32/diagnostics.py` identifica por I²C el MPU-6500 y el barómetro; es independiente del firmware ESP-IDF.

## Protocolo

Una línea JSON por orden: `{"v":1,"seq":1,"command":"NEUTRAL"}`. El receptor responde con `ok`, `seq`, `state` y `actuation_enabled`. Órdenes: `NEUTRAL`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `FORWARD`, `BACK`, `ARM`, `TAKEOFF`, `LAND`, `DISARM`, `ESTOP`. Las secuencias repetidas se rechazan. Sin una orden válida durante 500 ms, el núcleo entra en fallo si estaba armado; `ESTOP` permanece enclavado hasta reiniciar el ESP32-S3. El protocolo de esta versión está pensado para una red local controlada; no incluye autenticación.

## Verificación local

```bash
python -m unittest discover -s tests -v
g++ -std=c++17 -Wall -Wextra -Werror tests/test_flight_core.cpp -o /tmp/test_flight_core
/tmp/test_flight_core
```

El código Python y el núcleo C++ se verifican en la computadora. La compilación ESP-IDF y la ejecución con cámara/modelo requieren el entorno y los archivos del equipo.

Documentación de las APIs usadas: [Ultralytics YOLO](https://docs.ultralytics.com/modes/predict/), [ESP-IDF LEDC](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/ledc.html), [ESP-IDF I²C](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/i2c.html).
