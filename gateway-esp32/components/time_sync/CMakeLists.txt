idf_component_register(
    SRCS
        "time_sync.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_netif
)
