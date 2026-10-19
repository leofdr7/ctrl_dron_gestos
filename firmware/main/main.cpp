#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "board_port.hpp"
#include "flight_core.hpp"

namespace {

constexpr uint32_t kLinkTimeoutMs = 500;
constexpr EventBits_t kWifiConnected = BIT0;
const char* kTag = "vant";
static_assert(CONFIG_FREERTOS_HZ >= 200, "El lazo requiere un tick de 5 ms o menor");

struct CommandMessage { vant::Intent intent; };

EventGroupHandle_t g_wifi_events;
QueueHandle_t g_commands;
std::atomic<uint32_t> g_last_rx_ms{0};
std::atomic<bool> g_estop{false};
std::atomic<int> g_mode{static_cast<int>(vant::Mode::Disarmed)};
bool g_board_ready = false;
vant::Config g_flight_config;

const char* mode_name(vant::Mode mode) {
    switch (mode) {
        case vant::Mode::Disarmed: return "DISARMED";
        case vant::Mode::Armed: return "ARMED";
        case vant::Mode::Flying: return "FLYING";
        case vant::Mode::Landing: return "LANDING";
        case vant::Mode::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

bool parse_intent(const char* text, vant::Intent& out) {
    struct Entry { const char* name; vant::Intent value; };
    static constexpr Entry entries[] = {
        {"NEUTRAL", vant::Intent::Neutral}, {"UP", vant::Intent::Up},
        {"DOWN", vant::Intent::Down}, {"LEFT", vant::Intent::Left},
        {"RIGHT", vant::Intent::Right}, {"FORWARD", vant::Intent::Forward},
        {"BACK", vant::Intent::Back}, {"ARM", vant::Intent::Arm},
        {"TAKEOFF", vant::Intent::Takeoff}, {"LAND", vant::Intent::Land},
        {"DISARM", vant::Intent::Disarm}, {"ESTOP", vant::Intent::Estop},
    };
    for (const auto& entry : entries) {
        if (std::strcmp(text, entry.name) == 0) {
            out = entry.value;
            return true;
        }
    }
    return false;
}

bool parse_packet(const std::string& line, int& seq, vant::Intent& intent) {
    if (line.empty() || line.size() > 256 || line.find('\0') != std::string::npos) return false;
    const char* parse_end = nullptr;
    cJSON* root = cJSON_ParseWithOpts(line.c_str(), &parse_end, true);
    if (!root) return false;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* sequence = cJSON_GetObjectItemCaseSensitive(root, "seq");
    const cJSON* command = cJSON_GetObjectItemCaseSensitive(root, "command");
    const bool ok = cJSON_IsObject(root) && cJSON_IsNumber(version) &&
                    version->valuedouble == 1.0 && cJSON_IsNumber(sequence) &&
                    sequence->valuedouble >= 0 && sequence->valuedouble <= 2147483647 &&
                    sequence->valuedouble == sequence->valueint &&
                    cJSON_IsString(command) && command->valuestring &&
                    parse_intent(command->valuestring, intent);
    if (ok) seq = sequence->valueint;
    cJSON_Delete(root);
    return ok;
}

bool send_all(int fd, const char* data, size_t length) {
    while (length > 0) {
        const ssize_t n = send(fd, data, length, 0);
        if (n <= 0) return false;
        data += n;
        length -= static_cast<size_t>(n);
    }
    return true;
}

void reply(int fd, bool ok, int seq) {
    char response[180];
    const auto mode = static_cast<vant::Mode>(g_mode.load());
    const int n = std::snprintf(response, sizeof(response),
        "{\"ok\":%s,\"seq\":%d,\"state\":\"%s\",\"actuation_enabled\":%s}\n",
        ok ? "true" : "false", ok ? seq : -1, mode_name(mode),
        g_board_ready ? "true" : "false");
    if (n > 0 && static_cast<size_t>(n) < sizeof(response)) {
        send_all(fd, response, static_cast<size_t>(n));
    }
}

void wifi_event(void*, esp_event_base_t base, int32_t event_id, void*) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(g_wifi_events, kWifiConnected);
        g_last_rx_ms.store(0);
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(g_wifi_events, kWifiConnected);
    }
}

void start_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    g_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr));
    wifi_config_t config = {};
    std::snprintf(reinterpret_cast<char*>(config.sta.ssid), sizeof(config.sta.ssid),
                  "%s", CONFIG_VANT_WIFI_SSID);
    std::snprintf(reinterpret_cast<char*>(config.sta.password), sizeof(config.sta.password),
                  "%s", CONFIG_VANT_WIFI_PASSWORD);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(g_wifi_events, kWifiConnected, pdFALSE, pdFALSE, portMAX_DELAY);
}

void control_task(void*) {
    vant::FlightCore controller(g_flight_config);
    TickType_t wake = xTaskGetTickCount();
    int64_t previous = esp_timer_get_time();
    vant::Intent current_intent = vant::Intent::Neutral;
    while (true) {
        const int64_t now = esp_timer_get_time();
        const float dt = static_cast<float>(now - previous) / 1000000.0f;
        previous = now;
        vant::Attitude attitude;
        attitude.valid = g_board_ready && board_read_attitude(attitude) && attitude.valid;
        const uint32_t now_ms = static_cast<uint32_t>(now / 1000);
        const uint32_t last_ms = g_last_rx_ms.load();
        const bool healthy = last_ms != 0 && now_ms - last_ms <= kLinkTimeoutMs;
        CommandMessage message;
        const bool new_packet = xQueueReceive(g_commands, &message, 0) == pdTRUE;
        if (new_packet) current_intent = message.intent;
        vant::Intent intent = current_intent;
        bool event = new_packet;
        if (g_estop.load()) {
            intent = vant::Intent::Estop;
            event = true;
        }
        const auto output = controller.step(attitude, intent, event, healthy,
                                            g_board_ready && board_enable_switch(), dt);
        g_mode.store(static_cast<int>(output.mode));
        if (!board_write_motors(output.motor)) g_estop.store(true);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
    }
}

void serve_client(int fd) {
    timeval timeout{0, 100000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::string buffer;
    int last_seq = -1;
    int64_t last_activity_us = esp_timer_get_time();
    char chunk[256];
    while (true) {
        if ((xEventGroupGetBits(g_wifi_events) & kWifiConnected) == 0 ||
            esp_timer_get_time() - last_activity_us > 2000000) break;
        const ssize_t count = recv(fd, chunk, sizeof(chunk), 0);
        if (count == 0) break;
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            break;
        }
        buffer.append(chunk, static_cast<size_t>(count));
        if (buffer.size() > 1024) break;
        size_t newline;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            const std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            int seq = -1;
            vant::Intent intent;
            bool ok = parse_packet(line, seq, intent) && seq > last_seq;
            if (ok) {
                if (intent == vant::Intent::Estop) {
                    g_estop.store(true);
                } else {
                    const CommandMessage message{intent};
                    ok = xQueueSend(g_commands, &message, 0) == pdTRUE;
                }
                if (ok) {
                    last_seq = seq;
                    last_activity_us = esp_timer_get_time();
                    g_last_rx_ms.store(static_cast<uint32_t>(esp_timer_get_time() / 1000));
                }
            }
            reply(fd, ok, seq);
        }
    }
    g_last_rx_ms.store(0);
    xQueueReset(g_commands);
    close(fd);
}

void server_task(void*) {
    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        ESP_LOGE(kTag, "No se pudo abrir socket");
        vTaskDelete(nullptr);
        return;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(CONFIG_VANT_TCP_PORT);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, 1) != 0) {
        ESP_LOGE(kTag, "No se pudo escuchar en TCP");
        close(server);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag, "Esperando órdenes TCP en puerto %d", CONFIG_VANT_TCP_PORT);
    while (true) {
        const int client = accept(server, nullptr, nullptr);
        if (client >= 0) serve_client(client);
        else vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace

extern "C" void app_main() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    g_board_ready = board_init();
    g_flight_config = board_flight_config();
    g_board_ready = g_board_ready && g_flight_config.valid();
    if (!board_write_motors({0.0f, 0.0f, 0.0f, 0.0f})) g_board_ready = false;
    if (!g_board_ready) ESP_LOGW(kTag, "Adaptador de placa desactivado; motores en cero");
    g_commands = xQueueCreate(16, sizeof(CommandMessage));
    if (!g_commands) ESP_LOGE(kTag, "Sin memoria para cola de órdenes");
    if (!g_commands) return;
    if (xTaskCreatePinnedToCore(control_task, "flight", 6144, nullptr, 18, nullptr, 1) != pdPASS) {
        ESP_LOGE(kTag, "No se pudo crear la tarea de control");
        g_board_ready = false;
        return;
    }
    start_wifi();
    if (xTaskCreatePinnedToCore(server_task, "orders", 6144, nullptr, 5, nullptr, 0) != pdPASS) {
        ESP_LOGE(kTag, "No se pudo crear la tarea de red");
        g_estop.store(true);
    }
}
