# Auto-build webapp when sources change (guard: not available in IDF script mode)
if(NOT CMAKE_SCRIPT_MODE_FILE)
    file(GLOB_RECURSE WEBAPP_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/../webapp/src/*.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/../webapp/src/*.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/../webapp/src/*.css")

    set(INDEX_HTML "${CMAKE_CURRENT_SOURCE_DIR}/index.html")

    add_custom_command(
        OUTPUT ${INDEX_HTML}
        COMMAND npm run build
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../webapp"
        DEPENDS ${WEBAPP_SRCS}
                "${CMAKE_CURRENT_SOURCE_DIR}/../webapp/gulpfile.js"
        COMMENT "Building webapp..."
        VERBATIM
    )

    add_custom_target(webapp_build DEPENDS ${INDEX_HTML})
endif()

set(srcs
    "main.c"
    "src/main.cpp"
    "src/camera_driver.cpp"
    "src/led_status.cpp"
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
                             "esp_http_client" "driver" "esp_driver_gpio" "esp_driver_ledc"
                    EMBED_FILES "index.html")

# IDF v6.0 added fields to httpd_uri_t; omitting them is valid C++ (zero-init)
# but triggers -Wmissing-field-initializers promoted to error.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} webapp_build)
endif()

set_source_files_properties("src/webserver.cpp"
    PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers")
