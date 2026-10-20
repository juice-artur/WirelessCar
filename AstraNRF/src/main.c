#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>

int main(void)
{
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