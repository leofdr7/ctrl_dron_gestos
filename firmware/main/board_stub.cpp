#include "board_port.hpp"

// Implementación segura por defecto: permite verificar red y protocolo.
// Ninguna salida de motor se configura ni se activa.
bool board_init() { return false; }
vant::Config board_flight_config() { return {}; }
bool board_read_attitude(vant::Attitude& out) {
    out = {};
    return false;
}
bool board_enable_switch() { return false; }
bool board_write_motors(const std::array<float, 4>&) { return true; }
