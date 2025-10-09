# main/CMakeLists.txt — ESP-IDF component
# Include model blobs only if present (lets you generate them or keep them committed)
set(MODEL_SRCS "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/model_data.cc")
  list(APPEND MODEL_SRCS "model_data.cc")
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/model_data_occ.cc")
  list(APPEND MODEL_SRCS "model_data_occ.cc")
endif()

idf_component_register(
  SRCS
    "main.c"
    "ml.cpp"              # TinyML temperature forecaster glue
    "ml_occ.cpp"          # TinyML occupancy classifier glue
    ${MODEL_SRCS}         # generated TFLite blobs if present
  INCLUDE_DIRS
    "."
    "../include"          # drop this if your headers live only in main/
  REQUIRES
    esp_wifi
    esp_event
    esp_netif
    nvs_flash
    mqtt
    driver
    esp_timer
    lwip
    esp-tflite-micro      # install via: idf.py add-dependency "espressif/esp-tflite-micro"
)

# Language levels (safe defaults)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11 cxx_std_17)

# Optional: warnings (comment out if noisy)
# target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
