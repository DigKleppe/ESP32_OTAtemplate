# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)

set(COMPONENT_SRCDIRS ".")                                       
set(COMPONENT_ADD_INCLUDEDIRS "include")
#set(COMPONENT_PRIV_REQUIRES  "main spiffs" "nvs_flash esp_event app_update esp_http_client esp_https_ota driver mbedtls esp_wifi wifiConnect spiffsOTA")
set(COMPONENT_EMBED_TXTFILES "${project_dir}/server_certs/ca_cert.pem")
register_component()
#spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)
                    
