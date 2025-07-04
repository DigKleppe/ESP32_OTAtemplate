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

#include "httpsRequest.h"

static const char *TAG = "httpsRequest";

extern const uint8_t server_root_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[] asm("_binary_ca_cert_pem_end");

QueueHandle_t httpsReqMssgBox;
QueueHandle_t httpsReqRdyMssgBox;

uint8_t buf[HTTPSBUFSIZE];

static void https_get_requestxx(esp_tls_cfg_t cfg, const httpsRegParams_t *httpsRegParams, const char *REQUEST) {
	char buf[512];
	int ret, len;
	bool stop = false;
	int contentLength = -1;
	httpsMssg_t mssg;
	httpsMssg_t dummy;
	bool err = false;
	mssg.len = -1; // default

	esp_tls_t *tls = esp_tls_init();
	if (!tls) {
		ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
		err = true;
	}
	if (!err) {
		if (esp_tls_conn_http_new_sync(httpsRegParams->httpsURL, &cfg, tls) == 1) {
			ESP_LOGI(TAG, "Connection established...");
		} else {
			ESP_LOGE(TAG, "Connection failed...");
			int esp_tls_code = 0, esp_tls_flags = 0;
			esp_tls_error_handle_t tls_e = NULL;
			esp_tls_get_error_handle(tls, &tls_e);
			/* Try to get TLS stack level error and certificate failure flags, if any */
			ret = esp_tls_get_and_clear_last_error(tls_e, &esp_tls_code, &esp_tls_flags);
			if (ret == ESP_OK) {
				ESP_LOGE(TAG, "TLS error = -0x%x, TLS flags = -0x%x", esp_tls_code, esp_tls_flags);
			}
			esp_tls_conn_destroy(tls);
			err = true;
		}
	}

	if (!err) {

		ESP_LOGI(TAG, "htts request: %s", REQUEST);

#ifdef CONFIG_EXAMPLE_CLIENT_SESSION_TICKETS
		/* The TLS session is successfully established, now saving the session ctx for reuse */
		if (save_client_session) {
			esp_tls_free_client_session(tls_client_session);
			tls_client_session = esp_tls_get_client_session(tls);
		}
#endif
		size_t written_bytes = 0;
		do {
			ret = esp_tls_conn_write(tls, REQUEST + written_bytes, strlen(REQUEST) - written_bytes);
			if (ret >= 0) {
				ESP_LOGI(TAG, "%d bytes written", ret);
				written_bytes += ret;
			} else if (ret != ESP_TLS_ERR_SSL_WANT_READ && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
				ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
				esp_tls_conn_destroy(tls);
				return;
			}
		} while (written_bytes < strlen(REQUEST));
	}

	ESP_LOGI(TAG, "Reading HTTP response...");
	do {
		len = sizeof(buf) - 1;
		memset(buf, 0x00, sizeof(buf));
		ret = esp_tls_conn_read(tls, (char *)buf, len);

		if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) {
			err = true;
			stop = true;
		} else if (ret < 0) {
			ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
			stop = true;
		} else if (ret == 0) {
			ESP_LOGI(TAG, "connection closed");
			stop = true;
		}

		len = ret;
		ESP_LOGD(TAG, "%d bytes read", len);
		/* Print response directly to stdout as it is read */
		// for (int i = 0; i < len; i++) {
		// 	putchar(buf[i]);
		// }
		// putchar('\n'); // JSON output doesn't have a newline at end
	} while (!stop);

	if (err)
		mssg.len = -1;

	xQueueSend(httpsReqMssgBox, &mssg, MSSGBOX_TIMEOUT); // last message

	// ESP_LOGI(TAG, "End, send %d mssgs, %d bytes", blocks - 1, totalLen);
	if (tls)
		esp_tls_conn_destroy(tls);
}

static void https_get_request(esp_tls_cfg_t cfg, const httpsRegParams_t *httpsRegParams, const char *REQUEST) {
	int ret, len;
	int totalLen = 0;
	bool doRead = true;
	int blocks = 0;
	int contentLength = -1;
	httpsMssg_t mssg;
	httpsMssg_t dummy;
	mssg.len = -1; // default
	bool err = false;
	uint8_t *pContent;

	ESP_LOGI(TAG, "htts request: %s", REQUEST);

	esp_tls_t *tls = esp_tls_init();
	if (!tls) {
		ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
		err = true;
	}
	if (!err) {
		if (esp_tls_conn_http_new_sync(httpsRegParams->httpsURL, &cfg, tls) == 1) {
			ESP_LOGI(TAG, "Connection established...");
		} else {
			ESP_LOGE(TAG, "Connection failed...");
			err = true;
		}
	}
	if (!err) {
		size_t written_bytes = 0;
		do {
			ret = esp_tls_conn_write(tls, REQUEST + written_bytes, strlen(REQUEST) - written_bytes);
			if (ret >= 0) {
				ESP_LOGI(TAG, "%d bytes written", ret);
				written_bytes += ret;
			} else if (ret != ESP_TLS_ERR_SSL_WANT_READ && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
				ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
				err = true;
			}
		} while (written_bytes < strlen(REQUEST) && !err);
	}
	if (!err) {
		ESP_LOGI(TAG, "Reading HTTP response...");
		do {
			len = sizeof(buf);
			memset(buf, 0x00, sizeof(buf));
			ret = esp_tls_conn_read(tls, (char *)buf, len);

			blocks++;

			if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) {
				ESP_LOGI(TAG, "ret2: %d %s", ret, esp_err_to_name(ret));
				doRead = false;
				err = true;
			} else if (ret < 0) {
				ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
				break;
			} else if (ret == 0) {
				ESP_LOGI(TAG, "connection closed");
				mssg.len = 0; // no more chars availble
				break;
			}
			len = ret;
			switch (blocks) {
			case 1: {
				char *p = strstr((char *)buf, " 404 ");
				if (p) { // file not found ?
					ESP_LOGE(TAG, "File not found: %s", httpsRegParams->httpsURL);
					len = 0;
					err = true;
				} else {
					p = strcasestr((char *)buf, "Content-Length:"); // search content  length, on github last read returns error! ??
					if (p) {
						sscanf((p + 15), "%d", &contentLength); // = file lenght
						ESP_LOGI(TAG, "file length: %d", contentLength);
					} else {
						ESP_LOGE(TAG, "No file-length!");
					}
				}
				totalLen = 0; // start counting from block 2
				ESP_LOGI(TAG, "Start receiving: %d", len);
				break;

			} break;
			// case 2:
			//	totalLen = 0; // start counting from block 2
			//	ESP_LOGI(TAG, "Start receiving: %d", len);
			//	break;
			default:
				break;
			}

			if (len > 0) {

				//				for (int i = 0; i < len; i++) {
				//					putchar(buf[i]);
				//				}
				//				putchar('\n');
				//		if (blocks >= 2) { // do not export header ( BUFSIZE 1024 )
				//		if (blocks >= 3) {  // do not export header
				if (xQueueReceive(httpsReqRdyMssgBox, (void *)&dummy, MSSGBOX_TIMEOUT) == pdFALSE) {
					ESP_LOGE(TAG, "Rdy timeout");
					err = true;
				} else {
					if (blocks == 1) {
						pContent = (uint8_t *)strstr((char *)buf, "\r\n\r\n"); // find start of data
						if (pContent) {
							pContent += 4;					   // point to de data in received mssg
							mssg.len = len - (pContent - buf); // length of data in mssg
							totalLen += mssg.len;

							if (mssg.len > httpsRegParams->maxChars)
								memcpy(httpsRegParams->destbuffer, pContent, httpsRegParams->maxChars);
							else
								memcpy(httpsRegParams->destbuffer, pContent, mssg.len);

						} else {
							mssg.len = -1;
							err = true;
							ESP_LOGE(TAG, "No content found");
						}
					} else {
						mssg.len = len; // length of data in mssg
						totalLen += mssg.len;
						if (mssg.len > httpsRegParams->maxChars)
							memcpy(httpsRegParams->destbuffer, buf, httpsRegParams->maxChars);
						else
							memcpy(httpsRegParams->destbuffer, buf, mssg.len);
					}

					if (xQueueSend(httpsReqMssgBox, &mssg, MSSGBOX_TIMEOUT) == errQUEUE_FULL) {
						ESP_LOGE(TAG, " send mssg failed %d ", blocks);
						err = true;
					}
				}
				//			}
			}
			if (contentLength > 0) {
				if (totalLen >= contentLength) { // stop github pages??
					doRead = false;
					mssg.len = 0;
				}
			}
		} while (doRead && !err);
	}

	if (tls)
		esp_tls_conn_destroy(tls);

	if (err)
		mssg.len = -1;

	xQueueSend(httpsReqMssgBox, &mssg, MSSGBOX_TIMEOUT); // last message

	ESP_LOGI(TAG, "End, send %d mssgs, %d bytes", blocks - 1, totalLen);
}

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
