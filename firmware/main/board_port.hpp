#pragma once

#include <array>
#include "flight_core.hpp"

// Contrato entre el software de control y la integración física de la placa.
// Sustituir board_stub.cpp por la implementación del equipo.
// board_write_motors debe llevar a cero las cuatro salidas ante cualquier error.
bool board_init();
vant::Config board_flight_config();
bool board_read_attitude(vant::Attitude& out);
// Devolver false ante corte manual o cualquier condición local que impida volar.
bool board_enable_switch();
// Devolver false ante error; la implementación debe forzar cero en ese caso.
bool board_write_motors(const std::array<float, 4>& normalized);
