#include <cassert>
#include <cmath>
#include "../firmware/core/flight_core.hpp"

using namespace vant;

int main() {
    Config c;
    c.hover_throttle = 0.45f;
    c.idle_throttle = 0.10f;
    c.max_throttle = 0.80f;
    c.throttle_rate_per_s = 0.10f;
    c.takeoff_rate_per_s = 0.20f;
    c.landing_rate_per_s = 0.20f;
    c.max_tilt_deg = 5.0f;
    c.angle_kp = 0.01f;
    c.rate_kd = 0.001f;
    c.yaw_rate_kd = 0.001f;
    c.yaw_sign = {1, -1, 1, -1};
    assert(c.valid());

    FlightCore flight(c);
    Attitude sensor;
    sensor.valid = true;
    auto out = flight.step(sensor, Intent::Takeoff, true, true, true, 0.005f);
    assert(out.mode == Mode::Disarmed);
    out = flight.step(sensor, Intent::Arm, true, true, true, 0.005f);
    assert(out.mode == Mode::Armed && out.motor[0] == 0.0f);
    out = flight.step(sensor, Intent::Takeoff, true, true, true, 0.005f);
    assert(out.mode == Mode::Flying && out.motor[0] > 0.0f);
    for (int i = 0; i < 400; ++i) {
        out = flight.step(sensor, Intent::Neutral, false, true, true, 0.005f);
    }
    assert(out.motor[0] > 0.4f);
    out = flight.step(sensor, Intent::Right, true, true, true, 0.005f);
    assert(out.motor[0] > out.motor[1]);
    out = flight.step(sensor, Intent::Neutral, false, false, true, 0.005f);
    assert(out.mode == Mode::Fault);
    for (float value : out.motor) assert(value == 0.0f);
    out = flight.step(sensor, Intent::Arm, true, true, true, 0.005f);
    assert(out.mode == Mode::Fault);

    Config incomplete;
    FlightCore locked(incomplete);
    out = locked.step(sensor, Intent::Arm, true, true, true, 0.005f);
    assert(out.mode == Mode::Disarmed);

    FlightCore landing(c);
    out = landing.step(sensor, Intent::Arm, true, true, true, 0.005f);
    out = landing.step(sensor, Intent::Takeoff, true, true, true, 0.005f);
    for (int i = 0; i < 400; ++i) {
        out = landing.step(sensor, Intent::Neutral, false, true, true, 0.005f);
    }
    out = landing.step(sensor, Intent::Land, true, true, true, 0.005f);
    assert(out.mode == Mode::Landing);
    for (int i = 0; i < 500; ++i) {
        out = landing.step(sensor, Intent::Neutral, false, true, true, 0.005f);
    }
    assert(out.mode == Mode::Disarmed);
    for (float value : out.motor) assert(value == 0.0f);

    FlightCore emergency(c);
    out = emergency.step(sensor, Intent::Arm, true, true, true, 0.005f);
    out = emergency.step(sensor, Intent::Estop, true, true, true, 0.005f);
    assert(out.mode == Mode::Fault);
    for (float value : out.motor) assert(value == 0.0f);
    return 0;
}
