#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lora_bridge, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("weather-station lora_bridge v0.1.0");
	k_sleep(K_FOREVER);
	return 0;
}
