/*
 * nRF52840 BLE <-> UART bridge for the Astra OTA.
 *
 * The GATT service exposes Nordic-UART-like characteristics (so it can be
 * tested with nRF Connect), one control characteristic that pulses the STM32
 * OTA request pin, and one read-only diagnostics block:
 *
 *   6e400001-...  service
 *   6e400002-...  RX, write     -> forwarded to UART
 *   6e400003-...  TX, notify    <- everything received from UART
 *   6e400004-...  control, write 0x01 -> trigger the STM32 bootloader
 *   6e400005-...  diagnostics, read-only counter block
 *
 * Both directions are byte streams; the frame protocol itself is handled by
 * the Qt host and the STM32 bootloader.
 *
 * The UART uses the asynchronous API: reception is a long EasyDMA transfer
 * over two alternating buffers, so nothing is lost while the CPU services the
 * radio.  The interrupt-driven API reads a single byte per interrupt and the
 * nRF UARTE receiver stays stopped between the ENDRX event and the STARTRX
 * restart; at 115200 with BLE active that restart is late often enough to
 * swallow most of a frame, and an overrun there stops the receiver for good.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/bluetooth/services/nus.h>

#include <errno.h>

#define ASTRA_NUS_SERVICE_UUID_VAL \
    BT_UUID_NUS_SRV_VAL

#define ASTRA_NUS_RX_UUID_VAL \
    BT_UUID_NUS_RX_CHAR_VAL 

#define ASTRA_NUS_TX_UUID_VAL \
   	BT_UUID_NUS_TX_CHAR_VAL

/* Astra custom characteristic UUIDs. */
#define ASTRA_CTRL_UUID_VAL \
    BT_UUID_128_ENCODE(0x6e400004, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

#define ASTRA_DBG_UUID_VAL \
    BT_UUID_128_ENCODE(0x6e400005, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

/* BLE UUID definitions. */
static const struct bt_uuid_128 nus_service_uuid =
    BT_UUID_INIT_128(ASTRA_NUS_SERVICE_UUID_VAL);

static const struct bt_uuid_128 nus_rx_uuid =
    BT_UUID_INIT_128(ASTRA_NUS_RX_UUID_VAL);

static const struct bt_uuid_128 nus_tx_uuid =
    BT_UUID_INIT_128(ASTRA_NUS_TX_UUID_VAL);

static const struct bt_uuid_128 astra_ctrl_uuid =
    BT_UUID_INIT_128(ASTRA_CTRL_UUID_VAL);

static const struct bt_uuid_128 astra_dbg_uuid =
    BT_UUID_INIT_128(ASTRA_DBG_UUID_VAL);

/* UART RX configuration. */
#define ASTRA_UART_RX_BUFFER_SIZE  512U
#define ASTRA_UART_RX_TIMEOUT_US   5000U

static struct bt_conn *current_conn;
static bool notify_enabled;

RING_BUF_DECLARE(uart_tx_ring, 4096);
RING_BUF_DECLARE(uart_rx_ring, 8192);

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct gpio_dt_spec stm32_pin =
	GPIO_DT_SPEC_GET(DT_ALIAS(trigger_stm32_bootloader), gpios);

K_SEM_DEFINE(trigger_sem, 0, 1);
K_SEM_DEFINE(tx_request_sem, 0, 1);
K_SEM_DEFINE(tx_done_sem, 0, 1);
K_SEM_DEFINE(rx_wake_sem, 0, 1);

/*
 * Diagnostics counters, readable over BLE as a little-endian struct.  They are
 * updated from the UART callback (interrupt context) and read from the BLE
 * host thread; a torn snapshot is harmless for bring-up.
 */
struct bridge_stats {
	uint32_t uart_rx_bytes;
	uint32_t uart_rx_events;
	uint32_t uart_rx_overflow;
	uint32_t rx_stopped;
	uint32_t err_overrun;
	uint32_t err_framing;
	uint32_t err_parity;
	uint32_t err_noise;
	uint32_t rx_restarts;
	uint32_t rx_restart_fail;
	uint32_t notify_sent;
	uint32_t notify_err;
	uint32_t notify_skipped;
	uint32_t uart_tx_bytes;
	uint32_t adv_restarts;
	uint32_t adv_errors;
	uint32_t connections;
	uint32_t disconnects;
	uint32_t rx_buf_requests;
};

static struct bridge_stats stats;
static uint8_t rx_buf[2][ASTRA_UART_RX_BUFFER_SIZE];
static uint8_t rx_buf_active;
static atomic_t rx_restart_pending;

/* ---------------------------------------------------------------------- BLE */

static void tx_thread(void *a, void *b, void *c);
static void rx_thread(void *a, void *b, void *c);
K_THREAD_DEFINE(tx_tid, 1024, tx_thread, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(rx_tid, 1024, rx_thread, NULL, NULL, NULL, 7, 0, 0);

static ssize_t on_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset,
                           uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0u) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	/* Either the whole write fits or the host has to retry it: dropping a
	 * byte in the middle of a frame would corrupt the whole transfer. */
	if (ring_buf_space_get(&uart_tx_ring) < len) {
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	if (ring_buf_put(&uart_tx_ring, buf, len) != len) {
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	k_sem_give(&tx_request_sem);

	return len;
}

static ssize_t on_ctrl_write(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *value = buf;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset == 0u && len >= 1u && value[0] == 0x01u) {
		k_sem_give(&trigger_sem);
	}

	return len;
}

static ssize_t on_stats_read(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset)
{
	struct bridge_stats snapshot = stats;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &snapshot,
	                         sizeof(snapshot));
}

static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* attrs: [0] service, [1] RX decl, [2] RX value, [3] TX decl, [4] TX value,
 *        [5] CCC, [6] control decl, [7] control value, [8] diag decl,
 *        [9] diag value. */
BT_GATT_SERVICE_DEFINE(nus_service,
	BT_GATT_PRIMARY_SERVICE(&nus_service_uuid),
	BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
		BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
		BT_GATT_PERM_WRITE, NULL, on_rx_write, NULL),
	BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
		BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(on_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&astra_ctrl_uuid.uuid,
		BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
		BT_GATT_PERM_WRITE, NULL, on_ctrl_write, NULL),
	BT_GATT_CHARACTERISTIC(&astra_dbg_uuid.uuid,
		BT_GATT_CHRC_READ, BT_GATT_PERM_READ, on_stats_read, NULL, NULL),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
	        sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, ASTRA_NUS_SERVICE_UUID_VAL),
};

static void adv_start_work_handler(struct k_work *work);

/* Delayed so a failure can be retried without blocking a BT callback. */
static K_WORK_DELAYABLE_DEFINE(adv_work, adv_start_work_handler);

static void adv_start_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int const err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
	                                sd, ARRAY_SIZE(sd));

	if (err == 0) {
		stats.adv_restarts++;
		return;
	}

	if (err == -EALREADY) {
		return;
	}

	stats.adv_errors++;
	k_work_schedule(&adv_work, K_MSEC(500));
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0u) {
		return;
	}

	current_conn = bt_conn_ref(conn);
	stats.connections++;
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(reason);

	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	notify_enabled = false;
	stats.disconnects++;

	/* Advertising must come back, otherwise the bridge is invisible until a
	 * power cycle.  Done from a work item so the BT host has finished
	 * tearing the connection down. */
	k_work_schedule(&adv_work, K_MSEC(100));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

/* --------------------------------------------------------------------- UART */

static void uart_cb(const struct device *dev, struct uart_event *evt,
                    void *user_data)
{
	ARG_UNUSED(user_data);

	switch (evt->type) {
	case UART_RX_RDY: {
		uint8_t const *const data = evt->data.rx.buf + evt->data.rx.offset;
		uint32_t const len = evt->data.rx.len;
		uint32_t const put = ring_buf_put(&uart_rx_ring, data, len);

		stats.uart_rx_bytes += put;
		stats.uart_rx_events++;
		if (put < len) {
			stats.uart_rx_overflow += len - put;
		}

		k_sem_give(&rx_wake_sem);
		break;
	}
	case UART_RX_BUF_REQUEST: {
		uint8_t const next = (uint8_t)(rx_buf_active ^ 1u);

		stats.rx_buf_requests++;
		if (uart_rx_buf_rsp(dev, rx_buf[next], sizeof(rx_buf[next])) == 0) {
			rx_buf_active = next;
		}
		break;
	}
	case UART_RX_BUF_RELEASED:
		break;
	case UART_RX_STOPPED: {
		uint32_t const reason = evt->data.rx_stop.reason;

		stats.rx_stopped++;
		if ((reason & UART_ERROR_OVERRUN) != 0u) {
			stats.err_overrun++;
		}
		if ((reason & UART_ERROR_FRAMING) != 0u) {
			stats.err_framing++;
		}
		if ((reason & UART_ERROR_PARITY) != 0u) {
			stats.err_parity++;
		}
		if ((reason & UART_ERROR_NOISE) != 0u) {
			stats.err_noise++;
		}
		break;
	}
	case UART_RX_DISABLED:
		/* Reception ended (error or explicit stop).  Without a restart the
		 * bridge would go deaf for good, which is exactly how the previous
		 * interrupt-driven implementation failed. */
		atomic_set(&rx_restart_pending, 1);
		k_sem_give(&rx_wake_sem);
		break;
	case UART_TX_DONE:
	case UART_TX_ABORTED:
		k_sem_give(&tx_done_sem);
		break;
	default:
		break;
	}
}

/* Sends everything queued by the host.  uart_tx() is asynchronous, so the
 * staging buffer must stay untouched until UART_TX_DONE. */
static void tx_thread(void *a, void *b, void *c)
{
	static uint8_t stage[512];
	uint32_t len = 0u;
	uint32_t off = 0u;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		if (off >= len) {
			/* Nothing pending: wait for the host and refill. */
			k_sem_take(&tx_request_sem, K_FOREVER);
			len = ring_buf_get(&uart_tx_ring, stage, sizeof(stage));
			off = 0u;
			if (len == 0u) {
				continue;
			}
		}

		int const err = uart_tx(uart, stage + off, len - off, SYS_FOREVER_US);

		if (err != 0) {
			/* The port is still busy with the previous transfer: retry
			 * the same bytes instead of dropping them, a hole in the
			 * middle of a frame would cost the whole block. */
			k_msleep(1);
			continue;
		}

		stats.uart_tx_bytes += len - off;

		/* The driver owns the buffer until it reports completion. */
		k_sem_take(&tx_done_sem, K_FOREVER);
		off = len;
	}
}

static void notify_bytes(const uint8_t *data, uint32_t len)
{
	struct bt_conn *conn = current_conn;

	if (conn == NULL || !notify_enabled) {
		stats.notify_skipped += len;
		return;
	}

	/* Keep the connection object alive while notifying even if it drops
	 * underneath us. */
	bt_conn_ref(conn);

	uint16_t const mtu = bt_gatt_get_mtu(conn);
	uint16_t const chunk = (mtu > 3u) ? (uint16_t)(mtu - 3u) : 20u;

	for (uint32_t off = 0u; off < len; off += chunk) {
		uint16_t const n = (uint16_t)MIN((uint32_t)chunk, len - off);
		int retries = 0;
		int err;

		/* Waiting for a free TX buffer beats dropping an ACK. */
		do {
			if (current_conn == NULL || !notify_enabled) {
				bt_conn_unref(conn);
				return;
			}

			err = bt_gatt_notify(conn, &nus_service.attrs[4], data + off, n);
			if (err != -ENOMEM) {
				break;
			}

			k_msleep(2);
		} while (retries++ < 100);

		if (err != 0) {
			stats.notify_err++;
			break;
		}

		stats.notify_sent++;
	}

	bt_conn_unref(conn);
}

/* Drains received bytes to BLE and re-arms the receiver after an error. */
static void rx_thread(void *a, void *b, void *c)
{
	static uint8_t stage[256];

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		if (atomic_cas(&rx_restart_pending, 1, 0)) {
			rx_buf_active = 0u;

			if (uart_rx_enable(uart, rx_buf[0], sizeof(rx_buf[0]),
			                   ASTRA_UART_RX_TIMEOUT_US) == 0) {
				stats.rx_restarts++;
			} else {
				stats.rx_restart_fail++;
				atomic_set(&rx_restart_pending, 1);
				k_msleep(10);
				continue;
			}
		}

		if (k_sem_take(&rx_wake_sem, K_MSEC(20)) != 0) {
			continue;
		}

		uint32_t len;
		while ((len = ring_buf_get(&uart_rx_ring, stage, sizeof(stage))) > 0u) {
			notify_bytes(stage, len);
		}
	}
}

/* ------------------------------------------------- STM32 bootloader trigger */

static void trigger_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_sem_take(&trigger_sem, K_FOREVER);

		gpio_pin_set_dt(&stm32_pin, 1);
		k_msleep(200);
		gpio_pin_set_dt(&stm32_pin, 0);
	}
}

K_THREAD_DEFINE(trigger_tid, 1024, trigger_thread, NULL, NULL, NULL, 7, 0, 0);

/* --------------------------------------------------------------------- main */

int main(void)
{
	if (!gpio_is_ready_dt(&stm32_pin) || !device_is_ready(uart)) {
		return 0;
	}

	gpio_pin_configure_dt(&stm32_pin, GPIO_OUTPUT_INACTIVE);

	uart_callback_set(uart, uart_cb, NULL);

	if (uart_rx_enable(uart, rx_buf[0], sizeof(rx_buf[0]), ASTRA_UART_RX_TIMEOUT_US) != 0) {
		return 0;
	}

	if (bt_enable(NULL) != 0) {
		return 0;
	}

	k_work_schedule(&adv_work, K_NO_WAIT);

	return 0;
}
