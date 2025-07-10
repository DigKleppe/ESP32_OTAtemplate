#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/err.h"

#include "esp_tls.h"
#include "sdkconfig.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_event.h"
#include <sys/param.h>
#include <ctype.h>
#include "esp_system.h"

#include "esp_http_client.h"
#include "httpsReadFile.h"


static const char *TAG = "httpsReadFile";

extern const char server_root_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const char server_root_cert_pem_end[] asm("_binary_ca_cert_pem_end");

QueueHandle_t httpsReqMssgBox;
QueueHandle_t httpsReqRdyMssgBox;
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

uint8_t buf[HTTPSBUFSIZE];

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                        output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
#if CONFIG_EXAMPLE_ENABLE_RESPONSE_BUFFER_DUMP
                ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
#endif
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
		{
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
		}
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

int httpsReadFile( char * url,  char * dest, int maxChars) {

    esp_http_client_config_t config = {
        .url = url,
        . cert_pem = server_root_cert_pem_start,
      //  .crt_bundle_attach = esp_crt_bundle_attach,
        
        // .is_async = true,
        // .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        return -1;
    }
    int content_length =  esp_http_client_fetch_headers(client);
    int read_len;

    read_len = esp_http_client_read(client, dest, maxChars);
    
    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %"PRId64,
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return read_len;
}


int httpsReadFile(const httpsRegParams_t *httpsRegParams) 
{
 	httpsMssg_t mssg;
    if (httpsRegParams->destbuffer == NULL) {
        ESP_LOGE(TAG, "buffer not available");
        return -1;
    }

    esp_http_client_config_t config = {
        .url = httpsRegParams->httpsURL,          
       // .crt_bundle_attach = esp_crt_bundle_attach,
        . cert_pem = server_root_cert_pem_start,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        return -1;
    }
    int content_length =  esp_http_client_fetch_headers(client);
    int total_read_len = 0, read_len;

    do { 
        if ( xQueueReceive (httpsReqRdyMssgBox, &mssg,MSSGBOX_TIMEOUT)) {    
            read_len = esp_http_client_read(client, (char *) httpsRegParams->destbuffer, httpsRegParams->maxChars);
             mssg.len = read_len;
            xQueueSend(httpsReqMssgBox, &mssg, MSSGBOX_TIMEOUT); 
        }
        else {
            ESP_LOGE(TAG, "Timeout httpsReqRdyMssgBox");
            read_len = -1;
         }    
        total_read_len += read_len;
     } while ( read_len > 0 );

    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %"PRId64,
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return total_read_len;
}


void httpsGetRequestTask(void *pvparameters) {
	ESP_LOGI(TAG, "Start https_task");
	httpsRegParams_t *httpsRegParams = (httpsRegParams_t *)pvparameters;
	// char request[256];
	// snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: esp-idf/1.0 esp32\r\n\r\n", httpsRegParams->httpsURL,
	// 		 httpsRegParams->httpsServer);

	if (httpsReqMssgBox == NULL) { // once
		httpsReqMssgBox = xQueueCreate(1, sizeof(httpsMssg_t));
		httpsReqRdyMssgBox = xQueueCreate(1, sizeof(httpsMssg_t));
	} else {
		xQueueReset(httpsReqMssgBox);
		xQueueReset(httpsReqRdyMssgBox);
	}
    httpsReadFile(httpsRegParams);

	ESP_LOGI(TAG, "Finish https_request task" );
	vTaskDelete(NULL);
}
