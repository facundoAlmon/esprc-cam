set(srcs
    "main.c"
    "src/main.cpp"
    "src/camera_driver.cpp"
    "src/streamer.cpp"
    "src/webserver.cpp"
    "src/mjpeg_server.cpp"
    "src/nvs_prefs.cpp"
    "src/dns_server.c"
    "src/espnow_cam.cpp")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "." "include"
                    REQUIRES "esp_http_server" "nvs_flash" "esp_netif" "esp_wifi"
                             "app_update" "esp_partition" "mdns" "esp_timer"
                             "esp_http_client" "driver" "esp_driver_ledc"
                    EMBED_FILES "index.html")

# IDF v6.0 added fields to httpd_uri_t; omitting them is valid C++ (zero-init)
# but triggers -Wmissing-field-initializers promoted to error.
set_source_files_properties("src/webserver.cpp"
    PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers")
