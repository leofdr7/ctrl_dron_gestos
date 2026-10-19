# Copiar como config.py y editar. No conectar drivers de motor a este prototipo.
WIFI_SSID = "nombre_de_red"
WIFI_PASSWORD = "contrasena"
PORT = 8765

# Elegir GPIO libres según el esquema de la placa. Son obligatorios para diagnostics.py.
SDA_PIN = None
SCL_PIN = None
# Solo configurarlo cuando el PCA9548A esté confirmado físicamente.
MUX_ADDRESS = None
