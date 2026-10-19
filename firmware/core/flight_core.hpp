#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace vant {

enum class Intent {
    Neutral, Up, Down, Left, Right, Forward, Back,
    Arm, Takeoff, Land, Disarm, Estop
};

enum class Mode { Disarmed, Armed, Flying, Landing, Fault };

struct Attitude {
    float roll_deg = 0.0f;
    float pitch_deg = 0.0f;
    float roll_rate_dps = 0.0f;
    float pitch_rate_dps = 0.0f;
    float yaw_rate_dps = 0.0f;
    bool valid = false;
};

// Todos los valores son normalizados o en grados/segundo. Deben ajustarse en banco.
// Motor: 0=delantero izquierdo, 1=delantero derecho,
//        2=trasero derecho, 3=trasero izquierdo.
struct Config {
    float hover_throttle = 0.0f;  // obligatorio: medir antes de habilitar
    float idle_throttle = 0.0f;
    float max_throttle = 0.0f;    // obligatorio: definir antes de habilitar
    float throttle_rate_per_s = 0.0f;
    float takeoff_rate_per_s = 0.0f;
    float landing_rate_per_s = 0.0f;
    float max_tilt_deg = 0.0f;
    float angle_kp = 0.0f;
    float rate_kd = 0.0f;
    float yaw_rate_kd = 0.0f;
    std::array<int8_t, 4> yaw_sign = {0, 0, 0, 0};

    bool valid() const {
        return std::isfinite(hover_throttle) && std::isfinite(idle_throttle) &&
               std::isfinite(max_throttle) && std::isfinite(throttle_rate_per_s) &&
               std::isfinite(takeoff_rate_per_s) && std::isfinite(landing_rate_per_s) &&
               std::isfinite(max_tilt_deg) && std::isfinite(angle_kp) &&
               std::isfinite(rate_kd) && std::isfinite(yaw_rate_kd) &&
               idle_throttle >= 0.0f && hover_throttle > idle_throttle &&
               max_throttle > hover_throttle && max_throttle <= 1.0f &&
               throttle_rate_per_s > 0.0f && takeoff_rate_per_s > 0.0f &&
               landing_rate_per_s > 0.0f && max_tilt_deg > 0.0f &&
               angle_kp > 0.0f && rate_kd >= 0.0f && yaw_rate_kd > 0.0f &&
               (yaw_sign[0] == 1 || yaw_sign[0] == -1) &&
               yaw_sign[0] == yaw_sign[2] &&
               yaw_sign[1] == yaw_sign[3] && yaw_sign[0] == -yaw_sign[1];
    }
};

struct Output {
    Mode mode = Mode::Disarmed;
    std::array<float, 4> motor = {0.0f, 0.0f, 0.0f, 0.0f};
};

class FlightCore {
public:
    explicit FlightCore(const Config& config) : config_(config) {}

    // Llamar a una frecuencia fija desde el controlador local, no desde YOLO.
    Output step(const Attitude& imu, Intent intent, bool new_packet,
                bool link_healthy, bool enable_switch, float dt_s) {
        if (mode_ == Mode::Fault) return zero();
        if (new_packet && intent == Intent::Estop) {
            mode_ = Mode::Fault;
            return zero();
        }
        if (new_packet && intent == Intent::Disarm) {
            mode_ = Mode::Disarmed;
            collective_ = 0.0f;
            return zero();
        }
        if (mode_ == Mode::Disarmed) {
            if (new_packet && intent == Intent::Arm && config_.valid() &&
                sensors_ok(imu) && link_healthy && enable_switch &&
                std::isfinite(dt_s) && dt_s > 0.0f && dt_s <= 0.05f) {
                mode_ = Mode::Armed;
                collective_ = config_.idle_throttle;
            }
            return zero();
        }
        if (!config_.valid() || !sensors_ok(imu) || !link_healthy ||
            !enable_switch || !std::isfinite(dt_s) || dt_s <= 0.0f || dt_s > 0.05f) {
            mode_ = Mode::Fault;
            return zero();
        }
        if (mode_ == Mode::Armed) {
            if (new_packet && intent == Intent::Takeoff) mode_ = Mode::Flying;
            else return zero();
        }
        if (new_packet && intent == Intent::Land && mode_ == Mode::Flying) {
            mode_ = Mode::Landing;
        }
        if (mode_ == Mode::Landing) {
            collective_ = max(0.0f, collective_ - config_.landing_rate_per_s * dt_s);
            if (collective_ <= 0.0f) {
                mode_ = Mode::Disarmed;
                return zero();
            }
        } else if (collective_ < config_.hover_throttle) {
            collective_ = min(config_.hover_throttle,
                              collective_ + config_.takeoff_rate_per_s * dt_s);
        } else if (intent == Intent::Up) {
            collective_ = min(config_.max_throttle,
                              collective_ + config_.throttle_rate_per_s * dt_s);
        } else if (intent == Intent::Down) {
            collective_ = max(config_.idle_throttle,
                              collective_ - config_.throttle_rate_per_s * dt_s);
        }

        float roll_target = 0.0f;
        float pitch_target = 0.0f;
        if (mode_ == Mode::Flying) {
            if (intent == Intent::Right) roll_target = config_.max_tilt_deg;
            if (intent == Intent::Left) roll_target = -config_.max_tilt_deg;
            if (intent == Intent::Forward) pitch_target = -config_.max_tilt_deg;
            if (intent == Intent::Back) pitch_target = config_.max_tilt_deg;
        }

        const float roll = config_.angle_kp * (roll_target - imu.roll_deg) -
                           config_.rate_kd * imu.roll_rate_dps;
        const float pitch = config_.angle_kp * (pitch_target - imu.pitch_deg) -
                            config_.rate_kd * imu.pitch_rate_dps;
        const float yaw = -config_.yaw_rate_kd * imu.yaw_rate_dps;
        const float raw[4] = {
            collective_ + roll + pitch + config_.yaw_sign[0] * yaw,
            collective_ - roll + pitch + config_.yaw_sign[1] * yaw,
            collective_ - roll - pitch + config_.yaw_sign[2] * yaw,
            collective_ + roll - pitch + config_.yaw_sign[3] * yaw,
        };
        Output out;
        out.mode = mode_;
        for (int i = 0; i < 4; ++i) {
            out.motor[i] = clamp(raw[i], 0.0f, config_.max_throttle);
        }
        return out;
    }

    Mode mode() const { return mode_; }

private:
    static float min(float a, float b) { return a < b ? a : b; }
    static float max(float a, float b) { return a > b ? a : b; }
    static float clamp(float x, float lo, float hi) { return max(lo, min(x, hi)); }
    static bool sensors_ok(const Attitude& imu) {
        return imu.valid && std::isfinite(imu.roll_deg) &&
               std::isfinite(imu.pitch_deg) && std::isfinite(imu.roll_rate_dps) &&
               std::isfinite(imu.pitch_rate_dps) && std::isfinite(imu.yaw_rate_dps) &&
               std::fabs(imu.roll_deg) < 70.0f && std::fabs(imu.pitch_deg) < 70.0f;
    }
    Output zero() const { return Output{mode_, {0.0f, 0.0f, 0.0f, 0.0f}}; }

    Config config_;
    Mode mode_ = Mode::Disarmed;
    float collective_ = 0.0f;
};

}  // namespace vant
