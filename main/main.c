#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_eth.h"

#include "ledstrip_manager.h"
#include "portmacro.h"
#include "tcp_server.h"
#include "ethernet_init.h"

static const char EXAMPLE_ESP_WIFI_SSID[] = "Suziass\0\0\0";
static const char EXAMPLE_ESP_WIFI_PASS[] = "Assuzie789\0\0";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
static const EventBits_t WIFI_CONNECTED_BIT = BIT0;
static const EventBits_t WIFI_FAIL_BIT = BIT1;

static int s_retry_num = 0;


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    static const int max_connection_attempt = 5;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < max_connection_attempt) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err;
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wifi.");
        return err;
    }

    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set wifi power saving mode.");
        return err;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure wifi event handler instance register for any ids.");
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure wifi event handler for GOT_IP.");
        return err;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "\0",
        },
    };
    memcpy(wifi_config.sta.ssid, EXAMPLE_ESP_WIFI_SSID, sizeof(EXAMPLE_ESP_WIFI_SSID));
    memcpy(wifi_config.sta.password, EXAMPLE_ESP_WIFI_PASS, sizeof(EXAMPLE_ESP_WIFI_PASS));

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set wifi mode.");
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set wifi config.");
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start wifi.");
        return err;
    }

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    return ESP_OK;
}


void eth_event_handler_pipi(void* args, esp_event_base_t even_base, int32_t event_id, void* event_data) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    const esp_netif_ip_info_t* ip_info = &event->ip_info;
    ESP_LOGI("ETHERNET", "IP: " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI("ETHERNET", "NETMASK: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI("ETHERNET", "GW: " IPSTR, IP2STR(&ip_info->gw));
}

int init_eth() {
    esp_err_t err;
    uint8_t eth_port_count;
    esp_eth_handle_t* eth_handles;
    err = ethernet_init_all(&eth_handles, &eth_port_count);
    if (err != ESP_OK) {
        ESP_LOGI("ETH INIT", "Ethernet initi all failed");
        return err;
    }

    if (eth_port_count != 1) {
        ESP_LOGI("ETH INIT", "Ethernet count %u but expected 1", eth_port_count);
        return ESP_FAIL;
    }

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t* netif = esp_netif_new(&cfg);
    err = esp_netif_attach(netif, esp_eth_new_netif_glue(eth_handles[0]));
    if (err != ESP_OK) {
        ESP_LOGI("ETH INIT", "Attach glue failed");
        return err;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler_pipi, NULL);
    if (err != ESP_OK) {
        ESP_LOGI("ETH INIT", "Failed to attach event handler.");
        return err;
    }

    err = esp_eth_start(eth_handles[0]);
    if (err != ESP_OK) {
        ESP_LOGI("ETH INIT", "Failed to start eth handle");
        return err;
    }

    return ESP_OK;
}


static const int producer_cpu = 0;
static const int consumer_cpu = 1;
static QueueHandle_t message_queue;
void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    if (init_eth() != ESP_OK) {
        ESP_ERROR_CHECK(wifi_init_sta());
    }

    const int queue_size = 600;
    message_queue = xQueueCreate(queue_size, sizeof(uint8_t) * 3);
    xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", 4096 *3, (void*)&message_queue, 5, NULL, producer_cpu);
    xTaskCreatePinnedToCore(ledstrip_task, "ledstrip", 4096 *3, (void*)&message_queue, 5, NULL, consumer_cpu);
}
