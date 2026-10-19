"""Identificación I²C de sensores; ejecutar manualmente sin etapa de potencia."""

from machine import I2C, Pin

from config import SDA_PIN, SCL_PIN, MUX_ADDRESS


def inspect_bus(i2c):
    found = i2c.scan()
    print("Direcciones:", [hex(address) for address in found])
    for address in found:
        if address in (0x68, 0x69):
            chip_id = i2c.readfrom_mem(address, 0x75, 1)[0]
            print("IMU", hex(address), "WHO_AM_I", hex(chip_id),
                  "MPU-6500" if chip_id == 0x70 else "modelo no confirmado")
        elif address in (0x76, 0x77):
            chip_id = i2c.readfrom_mem(address, 0xD0, 1)[0]
            kind = {0x58: "BMP280", 0x60: "BME280"}.get(chip_id, "modelo no confirmado")
            print("Barómetro", hex(address), "ID", hex(chip_id), kind)
    return found


def run():
    if SDA_PIN is None or SCL_PIN is None:
        raise ValueError("Definir SDA_PIN y SCL_PIN según la placa en config.py")
    i2c = I2C(0, sda=Pin(SDA_PIN), scl=Pin(SCL_PIN), freq=100000)
    print("Bus principal")
    found = inspect_bus(i2c)
    if MUX_ADDRESS is not None:
        if MUX_ADDRESS not in found or not 0x70 <= MUX_ADDRESS <= 0x77:
            raise ValueError("MUX_ADDRESS no detectada o fuera de rango")
        print("PCA9548A confirmado en", hex(MUX_ADDRESS))
        try:
            for channel in range(8):
                i2c.writeto(MUX_ADDRESS, bytes([1 << channel]))
                print("Canal", channel)
                inspect_bus(i2c)
        finally:
            i2c.writeto(MUX_ADDRESS, b"\x00")


if __name__ == "__main__":
    run()
