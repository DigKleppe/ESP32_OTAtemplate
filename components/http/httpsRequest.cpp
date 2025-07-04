/*
 * HTTPS GET Example using plain Mbed TLS sockets
 *
 * Contacts the howsmyssl.com API via TLS v1.2 and reads a JSON
 * response.
 *
 * Adapted from the ssl_client1 example in Mbed TLS.
 *
 * SPDX-FileCopyrightText: The Mbed TLS Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPDX-FileContributor: 2015-2022 Espressif Systems (Shanghai) CO LTD
 */

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
#include "httpsRequest.h"

static const char *TAG = "httpsRequest";

extern const uint8_t server_root_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[] asm("_binary_ca_cert_pem_end");

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




static void https_get_request(esp_tls_cfg_t cfg, const httpsRegParams_t *httpsRegParams, const char *REQUEST) 
{

    esp_http_client_config_t config = {
       // .url = "https://postman-echo.com/post",
      	.url = "https://digkleppe.nl/OTA",
        .cert_pem = (char *) server_root_cert_pem_start,
	    .timeout_ms = 5000,
        .is_async = true,
    };

     esp_http_client_handle_t client = esp_http_client_init(&config);
     esp_err_t err;
//     esp_http_client_set_method(client, HTTP_METHOD_POST);
//   //  esp_http_client_set_post_field(client, post_data, strlen(post_data));
//     while (1) {
//         err = esp_http_client_perform(client);
//         if (err != ESP_ERR_HTTP_EAGAIN) {
//             break;
//         }
//     }
//     if (err == ESP_OK) {
//         ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %"PRId64,
//                 esp_http_client_get_status_code(client),
//                 esp_http_client_get_content_length(client));
//     } else {
//         ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
//     }
//     esp_http_client_cleanup(client);

    // Test HTTP_METHOD_HEAD with is_async enabled
    config.url = "https://digkleppe.nl/OTA/get";
    config.event_handler = _http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.is_async = true;
    config.timeout_ms = 5000;

    client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_HEAD);

    while (1) {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN) {
            break;
        }
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

}


// static void https_get_requestxx(esp_tls_cfg_t cfg, const httpsRegParams_t *httpsRegParams, const char *REQUEST) {
// 	char buf[512];
// 	int ret, len;
// 	bool stop = false;
// 	int contentLength = -1;
// 	httpsMssg_t mssg;
// 	httpsMssg_t dummy;
// 	bool err = false;
// 	mssg.len = -1; // default

// 	esp_tls_t *tls = esp_tls_init();
// 	if (!tls) {
// 		ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
// 		err = true;
// 	}
// 	if (!err) {
// 		if (esp_tls_conn_http_new_sync(httpsRegParams->httpsURL, &cfg, tls) == 1) {
// 			ESP_LOGI(TAG, "Connection established...");
// 		} else {
// 			ESP_LOGE(TAG, "Connection failed...");
// 			int esp_tls_code = 0, esp_tls_flags = 0;
// 			esp_tls_error_handle_t tls_e = NULL;
// 			esp_tls_get_error_handle(tls, &tls_e);
// 			/* Try to get TLS stack level error and certificate failure flags, if any */
// 			ret = esp_tls_get_and_clear_last_error(tls_e, &esp_tls_code, &esp_tls_flags);
// 			if (ret == ESP_OK) {
// 				ESP_LOGE(TAG, "TLS error = -0x%x, TLS flags = -0x%x", esp_tls_code, esp_tls_flags);
// 			}
// 			esp_tls_conn_destroy(tls);
// 			err = true;
// 		}
// 	}

// 	if (!err) {

// 		ESP_LOGI(TAG, "htts request: %s", REQUEST);

// #ifdef CONFIG_EXAMPLE_CLIENT_SESSION_TICKETS
// 		/* The TLS session is successfully established, now saving the session ctx for reuse */
// 		if (save_client_session) {
// 			esp_tls_free_client_session(tls_client_session);
// 			tls_client_session = esp_tls_get_client_session(tls);
// 		}
// #endif
// 		size_t written_bytes = 0;
// 		do {
// 			ret = esp_tls_conn_write(tls, REQUEST + written_bytes, strlen(REQUEST) - written_bytes);
// 			if (ret >= 0) {
// 				ESP_LOGI(TAG, "%d bytes written", ret);
// 				written_bytes += ret;
// 			} else if (ret != ESP_TLS_ERR_SSL_WANT_READ && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
// 				ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
// 				esp_tls_conn_destroy(tls);
// 				return;
// 			}
// 		} while (written_bytes < strlen(REQUEST));
// 	}

// 	ESP_LOGI(TAG, "Reading HTTP response...");
// 	do {
// 		len = sizeof(buf) - 1;
// 		memset(buf, 0x00, sizeof(buf));
// 		ret = esp_tls_conn_read(tls, (char *)buf, len);

// 		if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) {
// 			err = true;
// 			stop = true;
// 		} else if (ret < 0) {
// 			ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
// 			stop = true;
// 		} else if (ret == 0) {
// 			ESP_LOGI(TAG, "connection closed");
// 			stop = true;
// 		}

// 		len = ret;
// 		ESP_LOGD(TAG, "%d bytes read", len);
// 		/* Print response directly to stdout as it is read */
// 		// for (int i = 0; i < len; i++) {
// 		// 	putchar(buf[i]);
// 		// }
// 		// putchar('\n'); // JSON output doesn't have a newline at end
// 	} while (!stop);

// 	if (err)
// 		mssg.len = -1;

// 	xQueueSend(httpsReqMssgBox, &mssg, MSSGBOX_TIMEOUT); // last message

// 	// ESP_LOGI(TAG, "End, send %d mssgs, %d bytes", blocks - 1, totalLen);
// 	if (tls)
// 		esp_tls_conn_destroy(tls);
// }

// static void https_get_request(esp_tls_cfg_t cfg, const httpsRegParams_t *httpsRegParams, const char *REQUEST) {
// 	int ret, len;
// 	int totalLen = 0;
// 	bool doRead = true;
// 	int blocks = 0;
// 	int contentLength = -1;
// 	httpsMssg_t mssg;
// 	httpsMssg_t dummy;
// 	mssg.len = -1; // default
// 	bool err = false;
// 	uint8_t *pContent;

// 	ESP_LOGI(TAG, "htts request: %s", REQUEST);

// 	esp_tls_t *tls = esp_tls_init();
// 	if (!tls) {
// 		ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
// 		err = true;
// 	}
// 	if (!err) {
// 		if (esp_tls_conn_http_new_sync(httpsRegParams->httpsURL, &cfg, tls) == 1) {
// 			ESP_LOGI(TAG, "Connection established...");
// 		} else {
// 			ESP_LOGE(TAG, "Connection failed...");
// 			err = true;
// 		}
// 	}
// 	if (!err) {
// 		size_t written_bytes = 0;
// 		do {
// 			ret = esp_tls_conn_write(tls, REQUEST + written_bytes, strlen(REQUEST) - written_bytes);
// 			if (ret >= 0) {
// 				ESP_LOGI(TAG, "%d bytes written", ret);
// 				written_bytes += ret;
// 			} else if (ret != ESP_TLS_ERR_SSL_WANT_READ && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
// 				ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
// 				err = true;
// 			}
// 		} while (written_bytes < strlen(REQUEST) && !err);
// 	}
// 	if (!err) {
// 		ESP_LOGI(TAG, "Reading HTTP response...");
// 		do {
// 			len = sizeof(buf);
// 			memset(buf, 0x00, sizeof(buf));
// 			ret = esp_tls_conn_read(tls, (char *)buf, len);

// 			blocks++;

// 			if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) {
// 				ESP_LOGI(TAG, "ret2: %d %s", ret, esp_err_to_name(ret));
// 				doRead = false;
// 				err = true;
// 			} else if (ret < 0) {
// 				ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
// 				break;
// 			} else if (ret == 0) {
// 				ESP_LOGI(TAG, "connection closed");
// 				mssg.len = 0; // no more chars availble
// 				break;
// 			}
// 			len = ret;
// 			switch (blocks) {
// 			case 1: {
// 				char *p = strstr((char *)buf, " 404 ");
// 				if (p) { // file not found ?
// 					ESP_LOGE(TAG, "File not found: %s", httpsRegParams->httpsURL);
// 					len = 0;
// 					err = true;
// 				} else {
// 					p = strcasestr((char *)buf, "Content-Length:"); // search content  length, on github last read returns error! ??
// 					if (p) {
// 						sscanf((p + 15), "%d", &contentLength); // = file lenght
// 						ESP_LOGI(TAG, "file length: %d", contentLength);
// 					} else {
// 						ESP_LOGE(TAG, "No file-length!");
// 					}
// 				}
// 				totalLen = 0; // start counting from block 2
// 				ESP_LOGI(TAG, "Start receiving: %d", len);
// 				break;

// 			} break;
// 			// case 2:
// 			//	totalLen = 0; // start counting from block 2
// 			//	ESP_LOGI(TAG, "Start receiving: %d", len);
// 			//	break;
// 			default:
// 				break;
// 			}

// 			if (len > 0) {

// 				//				for (int i = 0; i < len; i++) {
// 				//					putchar(buf[i]);
// 				//				}
// 				//				putchar('\n');
// 				//		if (blocks >= 2) { // do not export header ( BUFSIZE 1024 )
// 				//		if (blocks >= 3) {  // do not export header
// 				if (xQueueReceive(httpsReqRdyMssgBox, (void *)&dummy, MSSGBOX_TIMEOUT) == pdFALSE) {
// 					ESP_LOGE(TAG, "Rdy timeout");
// 					err = true;
// 				} else {
// 					if (blocks == 1) {
// 						pContent = (uint8_t *)strstr((char *)buf, "\r\n\r\n"); // find start of data
// 						if (pContent) {
// 							pContent += 4;					   // point to de data in received mssg
// 							mssg.len = len - (pContent - buf); // length of data in mssg
// 							totalLen += mssg.len;

// 							if (mssg.len > httpsRegParams->maxChars)
// 								memcpy(httpsRegParams->destbuffer, pContent, httpsRegParams->maxChars);
// 							else
// 								memcpy(httpsRegParams->destbuffer, pContent, mssg.len);

// 						} else {
// 							mssg.len = -1;
// 							err = true;
// 							ESP_LOGE(TAG, "No content found");
// 						}
// 					} else {
// 						mssg.len = len; // length of data in mssg
// 						totalLen += mssg.len;
// 						if (mssg.len > httpsRegParams->maxChars)
// 							memcpy(httpsRegParams->destbuffer, buf, httpsRegParams->maxChars);
// 						else
// 							memcpy(httpsRegParams->destbuffer, buf, mssg.len);
// 					}

// 					if (xQueueSend(httpsReqMssgBox, &mssg, MSSGBOX_TIMEOUT) == errQUEUE_FULL) {
// 						ESP_LOGE(TAG, " send mssg failed %d ", blocks);
// 						err = true;
// 					}
// 				}
// 				//			}
// 			}
// 			if (contentLength > 0) {
// 				if (totalLen >= contentLength) { // stop github pages??
// 					doRead = false;
// 					mssg.len = 0;
// 				}
// 			}
// 		} while (doRead && !err);
// 	}

// 	if (tls)
// 		esp_tls_conn_destroy(tls);

// 	if (err)
// 		mssg.len = -1;

// 	xQueueSend(httpsReqMssgBox, &mssg, MSSGBOX_TIMEOUT); // last message

// 	ESP_LOGI(TAG, "End, send %d mssgs, %d bytes", blocks - 1, totalLen);
// }

void httpsGetRequestTask(void *pvparameters) {
	ESP_LOGI(TAG, "Start https_task");
	httpsRegParams_t *httpsRegParams = (httpsRegParams_t *)pvparameters;
	char request[256];
	snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: esp-idf/1.0 esp32\r\n\r\n", httpsRegParams->httpsURL,
			 httpsRegParams->httpsServer);

	if (httpsReqMssgBox == NULL) { // once
		httpsReqMssgBox = xQueueCreate(1, sizeof(httpsMssg_t));
		httpsReqRdyMssgBox = xQueueCreate(1, sizeof(httpsMssg_t));
	} else {
		xQueueReset(httpsReqMssgBox);
		xQueueReset(httpsReqRdyMssgBox);
	}

	esp_tls_cfg_t cfg = {
		.crt_bundle_attach = esp_crt_bundle_attach,
	};
	https_get_request(cfg, httpsRegParams, request);

	ESP_LOGI(TAG, "Finish https_request task");
	vTaskDelete(NULL);
}
