#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>



static const struct gpio_dt_spec stm32_pin =
	GPIO_DT_SPEC_GET(DT_ALIAS(trigger_stm32_bootloader), gpios);

	
static void stm32_request_ota(void)
{
	gpio_pin_set_dt(&stm32_pin, 1);  
	k_msleep(200);
	gpio_pin_set_dt(&stm32_pin, 0); 
}

int main(void)
{
	
	if (!gpio_is_ready_dt(&stm32_pin)) {
		return 0; 
    }
	
	gpio_pin_configure_dt(&stm32_pin, GPIO_OUTPUT_INACTIVE);

	k_msleep(6000);   

	stm32_request_ota(); 
	
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
    int i = 0;
    if (!device_is_ready(uart)) {
        i = 1000;
        k_msleep(1000);
        return 0;
    }

    const char msg[] = "Hello nRF52840\r\n";

    while (1) {
        for (size_t i = 0; i < sizeof(msg) - 1; i++) {
            uart_poll_out(uart, msg[i]);
        }

        k_msleep(1000);
    }

    return 0;
}