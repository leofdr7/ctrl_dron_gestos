# Contrato del control local

## Intenciones y estados

La computadora solo decide la intención a partir de imágenes. El ESP32-S3 conserva el estado de vuelo y calcula las cuatro salidas en un lazo de 200 Hz. Así, una imagen tardía no retrasa la corrección de actitud.

| Estado | Entrada que lo inicia | Salidas |
| --- | --- | --- |
| `DISARMED` | Arranque o `DISARM` | Cuatro ceros. |
| `ARMED` | `ARM` manual con configuración, sensor, enlace y habilitación válidos | Cuatro ceros, esperando `TAKEOFF`. |
| `FLYING` | `TAKEOFF` manual | Rampa hacia el colectivo configurado y mezcla de actitud. |
| `LANDING` | `LAND` | Disminución del colectivo configurada; termina en `DISARMED`. |
| `FAULT` | `ESTOP`, pérdida de enlace/habilitación, sensor inválido o tiempo de lazo fuera de rango | Cuatro ceros; queda enclavado. |

`UP` y `DOWN` modifican gradualmente el colectivo. `LEFT`, `RIGHT`, `FORWARD` y `BACK` fijan consignas de inclinación limitadas. `NEUTRAL` vuelve a inclinación objetivo cero y conserva el colectivo actual. `LAND` es una rampa de reducción; el núcleo no implementa mantenimiento automático de altura. Una clase visual nunca arma ni inicia el despegue.

## Interfaces de software

`firmware/main/board_port.hpp` define cuatro funciones que debe implementar el puerto de placa:

- `board_init()` deja salidas en cero y devuelve si el adaptador quedó listo.
- `board_flight_config()` entrega ganancias, límites, tasas de rampa y signos de yaw. Los valores predeterminados inválidos impiden armar.
- `board_read_attitude()` proporciona roll, pitch y velocidades angulares en grados/segundo, con `valid=true` solo cuando la estimación es confiable.
- `board_enable_switch()` y `board_write_motors()` aplican la habilitación y las cuatro salidas normalizadas.

El orden de motores del mezclador X es **delantero izquierdo, delantero derecho, trasero derecho, trasero izquierdo**. El signo de yaw depende del sentido real de los rotores y se configura en `yaw_sign`. El adaptador debe ser el único módulo que conoce GPIO, I²C y PWM. No se deben poner valores físicos por defecto en el núcleo.

## Limitaciones de software actuales

El núcleo usa control proporcional de ángulo y amortiguación por velocidad angular; no hay control de posición, navegación ni mantenimiento de altura. El aterrizaje se modela como rampa de potencia y requiere validación con la dinámica real del dron. El servidor TCP no autentica paquetes. Se debe verificar el comportamiento temporal del lazo, la estimación de actitud y las respuestas ante fallos antes de permitir un despegue real.
