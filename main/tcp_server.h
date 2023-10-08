#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"


static const char *TAG_SERVER = "tcp_server";

static void process_data(const int sock, QueueHandle_t* queue_handle)
{
    char rx_buffer[3000];
    uint8_t tx_buffer[3];
    uint8_t tx_index = 0;
    while (true) {
        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG_SERVER, "Error occurred during receiving: errno %d", errno);
            return;
        } else if (len == 0) {
            ESP_LOGW(TAG_SERVER, "Connection closed");
            return;
        } else {
            for (int i = 0; i < len; ++i) {
                tx_buffer[tx_index] = rx_buffer[i];
                ++tx_index;
                if (tx_index == 3) {
                    // Send
                    while (!xQueueSend(*queue_handle, (void*)&tx_buffer, 5));
                    tx_index = 0;
                }
            }
        }
    }
}


static void tcp_server_task(void *pvParameters)
{
    static const uint32_t port = 1234;
    QueueHandle_t* queue_handle = (QueueHandle_t*) pvParameters;
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = 1000;
    int keepInterval = 1000;
    int keepCount = 3;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(port);
        ip_protocol = IPPROTO_IP;
    }


    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG_SERVER, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_LOGI(TAG_SERVER, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG_SERVER, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG_SERVER, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG_SERVER, "Socket bound, port %" PRIu32, port);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG_SERVER, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    int sock = -1;
    while (1) {

        ESP_LOGI(TAG_SERVER, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG_SERVER, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG_SERVER, "Socket accepted ip address: %s", addr_str);

        process_data(sock, queue_handle);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

