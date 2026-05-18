/*
    Implementation file for interacting with networking peripherals
    Made by: Hannah Owen
*/

#include "networking.h"
#include "hardware_config.h"

#include <stdio.h>
#include <string.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "LORA";

static spi_device_handle_t lora_spi;

//==================================================
//LoRa command definitions
//==================================================

#define CMD_SET_STANDBY              0x80
#define CMD_SET_TX                   0x83
#define CMD_SET_PACKET_TYPE          0x8A
#define CMD_SET_RF_FREQUENCY         0x86
#define CMD_SET_BUFFER_BASE_ADDRESS  0x8F
#define CMD_WRITE_BUFFER             0x0E
#define CMD_SET_MODULATION_PARAMS    0x8B
#define CMD_SET_PACKET_PARAMS        0x8C
#define CMD_CLEAR_IRQ_STATUS         0x02
#define CMD_SET_DIO_IRQ_PARAMS       0x08
#define CMD_GET_IRQ_STATUS           0x12
#define CMD_SET_PA_CONFIG            0x95
#define CMD_SET_TX_PARAMS            0x8E
#define CMD_SET_REGULATOR_MODE       0x96
#define CMD_SET_DIO3_AS_TCXO_CTRL    0x97
#define CMD_SET_DIO2_RF_SWITCH_CTRL  0x9D

#define IRQ_TX_DONE                  0x0001

#define CMD_SET_RX                   0x82
#define CMD_GET_RX_BUFFER_STATUS     0x13
#define CMD_READ_BUFFER              0x1E

#define IRQ_RX_DONE                  0x0002
#define IRQ_CRC_ERR                  0x0040
#define IRQ_TIMEOUT                  0x0200

//------------------------HELPER FUNCTIONS------------------------

//==================================================
//SPI WRITE/READ HELPER
//==================================================

static uint8_t lora_read_register(uint16_t address)
{
    uint8_t tx_data[4];
    uint8_t rx_data[4];

    tx_data[0] = 0x1D; // READ REGISTER command
    tx_data[1] = (address >> 8) & 0xFF;
    tx_data[2] = address & 0xFF;
    tx_data[3] = 0x00;

    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    gpio_set_level(LORA_NSS, 0);
    spi_device_transmit(lora_spi, &t);
    gpio_set_level(LORA_NSS, 1);

    return rx_data[3];
}

static bool lora_wait_while_busy(uint32_t timeout_ms)
{
    uint32_t waited = 0;

    while(gpio_get_level(LORA_BUSY))
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        waited++;

        if(waited >= timeout_ms)
        {
            printf("BUSY timeout!\n");
            return false;
        }
    }

    return true;
}

static bool lora_send_command(uint8_t command, uint8_t *data, uint8_t length)
{
    if(!lora_wait_while_busy(1000))
    {
        return false;
    }

    uint8_t tx_data[16] = {0};

    tx_data[0] = command;

    for(int i = 0; i < length; i++)
    {
        tx_data[i + 1] = data[i];
    }

    spi_transaction_t t = {
        .length = (length + 1) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = NULL
    };

    gpio_set_level(LORA_NSS, 0);
    esp_err_t ret = spi_device_transmit(lora_spi, &t);
    gpio_set_level(LORA_NSS, 1);

    if(ret != ESP_OK)
    {
        printf("SPI command failed!\n");
        return false;
    }

    return lora_wait_while_busy(1000);
}

static bool lora_set_rf_frequency_433mhz(void)
{
    uint32_t freq = 433000000;
    uint32_t rf_freq = (uint32_t)((double)freq / 32.0e6 * 33554432.0);

    uint8_t data[4] = {
        (rf_freq >> 24) & 0xFF,
        (rf_freq >> 16) & 0xFF,
        (rf_freq >> 8) & 0xFF,
        rf_freq & 0xFF
    };

    return lora_send_command(0x86, data, 4);
}

static bool lora_write_buffer(uint8_t offset, uint8_t *data, uint8_t length)
{
    if(!lora_wait_while_busy(100))
    {
        return false;
    }

    uint8_t tx_data[32] = {0};

    tx_data[0] = 0x0E;
    tx_data[1] = offset;

    for(int i = 0; i < length; i++)
    {
        tx_data[i + 2] = data[i];
    }

    spi_transaction_t t = {
        .length = (length + 2) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = NULL
    };

    gpio_set_level(LORA_NSS, 0);
    esp_err_t ret = spi_device_transmit(lora_spi, &t);
    gpio_set_level(LORA_NSS, 1);

    if(ret != ESP_OK)
    {
        printf("WriteBuffer failed!\n");
        return false;
    }

    return lora_wait_while_busy(1000);
}

static uint8_t lora_get_status(void)
{
    uint8_t tx_data[2] = {0xC0, 0x00};
    uint8_t rx_data[2] = {0};

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    gpio_set_level(LORA_NSS, 0);
    spi_device_transmit(lora_spi, &t);
    gpio_set_level(LORA_NSS, 1);

    return rx_data[1];
}

static bool lora_get_irq_status(uint16_t *irq_status)
{
    uint8_t tx_data[4] = {0x12, 0x00, 0x00, 0x00};
    uint8_t rx_data[4] = {0};

    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    gpio_set_level(LORA_NSS, 0);
    esp_err_t ret = spi_device_transmit(lora_spi, &t);
    gpio_set_level(LORA_NSS, 1);

    if(ret != ESP_OK)
    {
        return false;
    }

    *irq_status = ((uint16_t)rx_data[2] << 8) | rx_data[3];

    return true;
}

static bool lora_read_buffer(uint8_t offset, uint8_t *data, uint8_t length)
{
    if(!lora_wait_while_busy(1000))
    {
        return false;
    }

    uint8_t tx_data[80] = {0};
    uint8_t rx_data[80] = {0};

    tx_data[0] = CMD_READ_BUFFER;
    tx_data[1] = offset;
    tx_data[2] = 0x00;   // dummy byte

    spi_transaction_t t = {
        .length = (length + 3) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    gpio_set_level(LORA_NSS, 0);
    esp_err_t ret = spi_device_transmit(lora_spi, &t);
    gpio_set_level(LORA_NSS, 1);

    if(ret != ESP_OK)
    {
        printf("ReadBuffer failed!\n");
        return false;
    }

    for(int i = 0; i < length; i++)
    {
        data[i] = rx_data[i + 3];
    }

    return true;
}

//------------------------PUBLIC FUNCTIONS------------------------

//==================================================
//INITIALIZATION
//==================================================

bool lora_init(void)
{
    printf("Starting LoRa hardware test...\n");

    /*
    -------------------------
    Configure GPIO
    -------------------------
    */

    gpio_config_t output_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LORA_NSS) |
                        (1ULL << LORA_NRESET)
    };

    gpio_config(&output_conf);

    gpio_config_t input_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << LORA_BUSY) |
                        (1ULL << LORA_DIO1)
    };

    gpio_config(&input_conf);

    /*
    -------------------------
    Reset LoRa chip
    -------------------------
    */

    printf("Resetting LoRa chip...\n");

    gpio_set_level(LORA_NRESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(LORA_NRESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    /*
    -------------------------
    Read BUSY pin
    -------------------------
    */

    int busy = gpio_get_level(LORA_BUSY);

    printf("BUSY pin state: %d\n", busy);

    /*
    -------------------------
    SPI BUS CONFIG
    -------------------------
    */

    spi_bus_config_t buscfg = {
        .mosi_io_num = LORA_MOSI,
        .miso_io_num = LORA_MISO,
        .sclk_io_num = LORA_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);

    if(ret != ESP_OK)
    {
        printf("SPI bus init failed!\n");
        return false;
    }

    /*
    -------------------------
    ADD DEVICE
    -------------------------
    */

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &lora_spi);

    if(ret != ESP_OK)
    {
        printf("SPI device add failed!\n");
        return false;
    }

    printf("SPI initialized!\n");

    /*
    -------------------------
    Try reading register
    -------------------------
    */

    uint8_t reg = lora_read_register(0x08);

    printf("Register read result: 0x%02X\n", reg);

    return true;
}

//==================================================
//TEST LOOP
//==================================================

/*
void lora_test_loop(void)
{
    int busy = gpio_get_level(LORA_BUSY);
    int dio1 = gpio_get_level(LORA_DIO1);

    printf("BUSY=%d | DIO1=%d\n", busy, dio1);
}

bool lora_run_diagnostics(void)
{
    printf("\nRunning LoRa diagnostics...\n");

    printf("Test 1: BUSY before command = %d\n", gpio_get_level(LORA_BUSY));

    uint8_t standby_data[] = {0x00}; 
    if(!lora_send_command(0x80, standby_data, 1))
    {
        printf("SetStandby failed!\n");
        return false;
    }

    printf("SetStandby command sent.\n");
    printf("BUSY after standby = %d\n", gpio_get_level(LORA_BUSY));

    uint8_t packet_type_data[] = {0x01}; 
    if(!lora_send_command(0x8A, packet_type_data, 1))
    {
        printf("SetPacketType LoRa failed!\n");
        return false;
    }

    printf("SetPacketType LoRa command sent.\n");

    uint8_t clear_irq_data[] = {0xFF, 0xFF};
    if(!lora_send_command(0x02, clear_irq_data, 2))
    {
        printf("ClearIrqStatus failed!\n");
        return false;
    }

    printf("ClearIrqStatus command sent.\n");

    printf("DIO1 state = %d\n", gpio_get_level(LORA_DIO1));
    printf("BUSY final state = %d\n", gpio_get_level(LORA_BUSY));

    return true;
}

bool lora_send_test_packet(void)
{
    printf("\nSending LoRa test packet...\n");

    uint8_t standby[] = {0x00};
    if(!lora_send_command(0x80, standby, 1))
    {
        printf("SetStandby failed.\n");
        return false;
    }

    uint8_t packet_type[] = {0x01};
    if(!lora_send_command(0x8A, packet_type, 1))
    {
        printf("SetPacketType failed.\n");
        return false;
    }

    //==================================================
    //Enable DC-DC regulator
    //==================================================

    uint8_t regulator_mode[] = {0x01};

    if(!lora_send_command(0x96, regulator_mode, 1))
    {
        printf("SetRegulatorMode failed.\n");
        return false;
    }

    printf("DC-DC regulator enabled.\n");

    //==================================================
    //Enable TCXO on DIO3
    //==================================================

    uint8_t tcxo_config[] = {
        0x07,             // 3.3V TCXO
        0x00, 0x01, 0x40  // startup delay
    };

    if(!lora_send_command(0x97, tcxo_config, 4))
    {
        printf("SetDio3AsTcxoCtrl failed.\n");
        return false;
    }

    printf("TCXO enabled.\n");

    vTaskDelay(pdMS_TO_TICKS(10));

    //==================================================
    //Enable RF switch control on DIO2
    //==================================================

    uint8_t rf_switch[] = {0x01};

    if(!lora_send_command(0x9D, rf_switch, 1))
    {
        printf("SetDio2AsRfSwitchCtrl failed.\n");
        return false;
    }

    printf("RF switch control enabled.\n");

    if(!lora_set_rf_frequency_433mhz())
    {
        printf("SetRfFrequency failed.\n");
        return false;
    }

    uint8_t pa_config[] = {
        0x04,   // paDutyCycle
        0x07,   // hpMax
        0x00,   // deviceSel = SX1262/SX1268 style
        0x01    // paLut
    };

    if(!lora_send_command(0x95, pa_config, 4))
    {
        printf("SetPaConfig failed.\n");
        return false;
    }

    uint8_t tx_params[] = {
        0x0E,   // power = 14 dBm
        0x04    // ramp time = 200 us
    };

    if(!lora_send_command(0x8E, tx_params, 2))
    {
        printf("SetTxParams failed.\n");
        return false;
    }

    uint8_t buffer_base[] = {0x00, 0x00};
    if(!lora_send_command(0x8F, buffer_base, 2))
    {
        printf("SetBufferBaseAddress failed.\n");
        return false;
    }

    uint8_t irq_params[] = {
        0x00, 0x01,   // IRQ mask: TX_DONE
        0x00, 0x01,   // DIO1 mask: TX_DONE
        0x00, 0x00,   // DIO2 mask
        0x00, 0x00    // DIO3 mask
    };

    if(!lora_send_command(0x08, irq_params, 8))
    {
        printf("SetDioIrqParams failed.\n");
        return false;
    }

    uint8_t clear_irq[] = {0xFF, 0xFF};
    if(!lora_send_command(0x02, clear_irq, 2))
    {
        printf("ClearIrqStatus failed.\n");
        return false;
    }

    uint8_t modulation_params[] = {
        0x07,   // SF7
        0x04,   // bandwidth 125 kHz
        0x01,   // coding rate 4/5
        0x00    // low data rate optimization off
    };

    if(!lora_send_command(0x8B, modulation_params, 4))
    {
        printf("SetModulationParams failed.\n");
        return false;
    }

    uint8_t packet_params[] = {
        0x00, 0x08,   // preamble length = 8
        0x00,         // explicit header
        0x05,         // payload length = 5
        0x01,         // CRC on
        0x00          // standard IQ
    };

    if(!lora_send_command(0x8C, packet_params, 6))
    {
        printf("SetPacketParams failed.\n");
        return false;
    }

    uint8_t message[] = {'H', 'E', 'L', 'L', 'O'};

    if(!lora_write_buffer(0x00, message, 5))
    {
        printf("WriteBuffer failed.\n");
        return false;
    }

    uint8_t tx_timeout[] = {0x00, 0x00, 0x00};

    if(!lora_send_command(0x83, tx_timeout, 3))
    {
        printf("SetTx failed.\n");
        return false;
    }

    printf("Status after SetTx: 0x%02X\n", lora_get_status());

    printf("TX started. Waiting for TX_DONE IRQ...\n");

    for(int i = 0; i < 3000; i++)
    {
        uint16_t irq = 0;

        if(lora_get_irq_status(&irq))
        {
            if(irq != 0)
            {
                printf("IRQ status: 0x%04X | DIO1=%d\n", irq, gpio_get_level(LORA_DIO1));
            }

            if(irq & 0x0001)
            {
                printf("TX_DONE detected in IRQ register!\n");

                lora_send_command(0x02, clear_irq, 2);

                return true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    printf("TX_DONE timeout. No TX_DONE IRQ detected.\n");
    return false;

}
*/

//==================================================
//LORA COMMUNICATION FUNCTIONs
//==================================================

bool sendLoRa(uint8_t *data, uint8_t length)
{
    if(data == NULL || length == 0 || length > 62)
    {
        printf("Invalid LoRa payload.\n");
        return false;
    }

    printf("Sending LoRa packet, length = %d...\n", length);

    uint8_t standby[] = {0x00};

    if(!lora_send_command(CMD_SET_STANDBY, standby, 1))
    {
        printf("SetStandby failed.\n");
        return false;
    }

    uint8_t packet_type[] = {0x01};

    if(!lora_send_command(CMD_SET_PACKET_TYPE, packet_type, 1))
    {
        printf("SetPacketType failed.\n");
        return false;
    }

    uint8_t regulator_mode[] = {0x01};

    if(!lora_send_command(CMD_SET_REGULATOR_MODE, regulator_mode, 1))
    {
        printf("SetRegulatorMode failed.\n");
        return false;
    }

    uint8_t tcxo_config[] = {
        0x07,
        0x00, 0x01, 0x40
    };

    if(!lora_send_command(CMD_SET_DIO3_AS_TCXO_CTRL, tcxo_config, 4))
    {
        printf("SetDio3AsTcxoCtrl failed.\n");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t rf_switch[] = {0x01};

    if(!lora_send_command(CMD_SET_DIO2_RF_SWITCH_CTRL, rf_switch, 1))
    {
        printf("SetDio2AsRfSwitchCtrl failed.\n");
        return false;
    }

    if(!lora_set_rf_frequency_433mhz())
    {
        printf("SetRfFrequency failed.\n");
        return false;
    }

    uint8_t pa_config[] = {
        0x04,
        0x07,
        0x00,
        0x01
    };

    if(!lora_send_command(CMD_SET_PA_CONFIG, pa_config, 4))
    {
        printf("SetPaConfig failed.\n");
        return false;
    }

    uint8_t tx_params[] = {
        0x0E,
        0x04
    };

    if(!lora_send_command(CMD_SET_TX_PARAMS, tx_params, 2))
    {
        printf("SetTxParams failed.\n");
        return false;
    }

    uint8_t buffer_base[] = {
        0x00,
        0x00
    };

    if(!lora_send_command(CMD_SET_BUFFER_BASE_ADDRESS, buffer_base, 2))
    {
        printf("SetBufferBaseAddress failed.\n");
        return false;
    }

    uint8_t irq_params[] = {
        0x00, 0x01,
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x00
    };

    if(!lora_send_command(CMD_SET_DIO_IRQ_PARAMS, irq_params, 8))
    {
        printf("SetDioIrqParams failed.\n");
        return false;
    }

    uint8_t clear_irq[] = {
        0xFF,
        0xFF
    };

    if(!lora_send_command(CMD_CLEAR_IRQ_STATUS, clear_irq, 2))
    {
        printf("ClearIrqStatus failed.\n");
        return false;
    }

    uint8_t modulation_params[] = {
        0x07,
        0x04,
        0x01,
        0x00
    };

    if(!lora_send_command(CMD_SET_MODULATION_PARAMS, modulation_params, 4))
    {
        printf("SetModulationParams failed.\n");
        return false;
    }

    uint8_t packet_params[] = {
        0x00, 0x08,
        0x00,
        length,
        0x01,
        0x00
    };

    if(!lora_send_command(CMD_SET_PACKET_PARAMS, packet_params, 6))
    {
        printf("SetPacketParams failed.\n");
        return false;
    }

    if(!lora_write_buffer(0x00, data, length))
    {
        printf("WriteBuffer failed.\n");
        return false;
    }

    uint8_t tx_timeout[] = {
        0x00,
        0x00,
        0x00
    };

    if(!lora_send_command(CMD_SET_TX, tx_timeout, 3))
    {
        printf("SetTx failed.\n");
        return false;
    }

    printf("TX started. Waiting for TX_DONE...\n");

    for(int i = 0; i < 5000; i++)
    {
        uint16_t irq = 0;

        if(lora_get_irq_status(&irq))
        {
            if(irq & IRQ_TX_DONE)
            {
                printf("TX_DONE detected.\n");

                lora_send_command(CMD_CLEAR_IRQ_STATUS, clear_irq, 2);

                return true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    printf("TX timeout. No TX_DONE detected.\n");
    return false;
}

bool receiveLoRa(uint8_t *data, uint8_t *length)
{
    if(data == NULL || length == NULL)
    {
        return false;
    }

    printf("Entering LoRa RX mode...\n");

    uint8_t standby[] = {0x00};
    if(!lora_send_command(CMD_SET_STANDBY, standby, 1))
    {
        printf("SetStandby failed.\n");
        return false;
    }

    uint8_t packet_type[] = {0x01};
    if(!lora_send_command(CMD_SET_PACKET_TYPE, packet_type, 1))
    {
        printf("SetPacketType failed.\n");
        return false;
    }

    uint8_t regulator_mode[] = {0x01};
    if(!lora_send_command(CMD_SET_REGULATOR_MODE, regulator_mode, 1))
    {
        printf("SetRegulatorMode failed.\n");
        return false;
    }

    uint8_t tcxo_config[] = {
        0x07,
        0x00, 0x01, 0x40
    };

    if(!lora_send_command(CMD_SET_DIO3_AS_TCXO_CTRL, tcxo_config, 4))
    {
        printf("SetDio3AsTcxoCtrl failed.\n");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t rf_switch[] = {0x01};
    if(!lora_send_command(CMD_SET_DIO2_RF_SWITCH_CTRL, rf_switch, 1))
    {
        printf("SetDio2AsRfSwitchCtrl failed.\n");
        return false;
    }

    if(!lora_set_rf_frequency_433mhz())
    {
        printf("SetRfFrequency failed.\n");
        return false;
    }

    uint8_t buffer_base[] = {0x00, 0x00};
    if(!lora_send_command(CMD_SET_BUFFER_BASE_ADDRESS, buffer_base, 2))
    {
        printf("SetBufferBaseAddress failed.\n");
        return false;
    }

    uint8_t irq_params[] = {
        0x00, 0x42,   // IRQ mask: RX_DONE + CRC_ERR
        0x00, 0x42,   // DIO1 mask: RX_DONE + CRC_ERR
        0x00, 0x00,
        0x00, 0x00
    };

    if(!lora_send_command(CMD_SET_DIO_IRQ_PARAMS, irq_params, 8))
    {
        printf("SetDioIrqParams failed.\n");
        return false;
    }

    uint8_t clear_irq[] = {0xFF, 0xFF};
    lora_send_command(CMD_CLEAR_IRQ_STATUS, clear_irq, 2);

    uint8_t modulation_params[] = {
        0x07,   // SF7
        0x04,   // BW 125 kHz
        0x01,   // CR 4/5
        0x00
    };

    if(!lora_send_command(CMD_SET_MODULATION_PARAMS, modulation_params, 4))
    {
        printf("SetModulationParams failed.\n");
        return false;
    }

    uint8_t packet_params[] = {
        0x00, 0x08,   // preamble 8
        0x00,         // explicit header
        0xFF,         // max payload length
        0x01,         // CRC on
        0x00          // standard IQ
    };

    if(!lora_send_command(CMD_SET_PACKET_PARAMS, packet_params, 6))
    {
        printf("SetPacketParams failed.\n");
        return false;
    }

    uint8_t rx_timeout[] = {
        0x00, 0x00, 0x00   // continuous RX
    };

    if(!lora_send_command(CMD_SET_RX, rx_timeout, 3))
    {
        printf("SetRx failed.\n");
        return false;
    }

    printf("Waiting for LoRa packet...\n");

    while(1)
    {
        uint16_t irq = 0;

        if(lora_get_irq_status(&irq))
        {
            if(irq & IRQ_RX_DONE)
            {
                printf("RX_DONE detected.\n");

                uint8_t tx_data[4] = {CMD_GET_RX_BUFFER_STATUS, 0x00, 0x00, 0x00};
                uint8_t rx_data[4] = {0};

                spi_transaction_t t = {
                    .length = 32,
                    .tx_buffer = tx_data,
                    .rx_buffer = rx_data
                };

                gpio_set_level(LORA_NSS, 0);
                spi_device_transmit(lora_spi, &t);
                gpio_set_level(LORA_NSS, 1);

                uint8_t payload_length = rx_data[2];
                uint8_t start_pointer = rx_data[3];

                printf("Payload length = %d\n", payload_length);

                if(payload_length > 63)
                {
                    payload_length = 63;
                }

                if(!lora_read_buffer(start_pointer, data, payload_length))
                {
                    return false;
                }

                data[payload_length] = '\0';
                *length = payload_length;

                lora_send_command(CMD_CLEAR_IRQ_STATUS, clear_irq, 2);

                return true;
            }

            if(irq & IRQ_CRC_ERR)
            {
                printf("CRC error.\n");
                lora_send_command(CMD_CLEAR_IRQ_STATUS, clear_irq, 2);
                return false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

//==================================================
//ANALYSING COMMANDS
//==================================================

bool isWifiOnRequest(uint8_t *data)
{
    char *message = (char *)data;

    if(strstr(message, "TO=B") != NULL &&
       strstr(message, "CMD=WIFI_ON_REQUEST") != NULL)
    {
        return true;
    }

    return false;
}

bool isGpsUpdateRequest(uint8_t *data)
{
    char *message = (char *)data;

    if(strstr(message, "TO=B") != NULL &&
       strstr(message, "CMD=GPS_UPDATE_REQUEST") != NULL)
    {
        return true;
    }

    return false;
}
