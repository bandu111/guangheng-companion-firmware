#include "gh_hall.h"

#include "sdkconfig.h"

bool gh_hall_is_enabled(void)
{
#ifdef CONFIG_GH_HALL_ENABLED
    return true;
#else
    return false;
#endif
}

esp_err_t gh_hall_init(void)
{
    if (!gh_hall_is_enabled()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#if CONFIG_GH_HALL_I2C_SDA < 0 || CONFIG_GH_HALL_I2C_SCL < 0
    return ESP_ERR_INVALID_ARG;
#else
    /* Pin ownership is intentionally unresolved until board revision/BSP scan is known. */
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t gh_hall_read(gh_hall_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
