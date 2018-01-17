/*
The MIT License (MIT)

Copyright (c) 2017 Sebastian Pauka

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "project.h"
#include "usb_utils.h"

const char* parity[] = {"None", "Odd", "Even", "Mark", "Space"};
const char* stop[]   = {"1", "1.5", "2"};

// Buffer used to store USB commands
usb_buf_t usb_input_buffer;

/**
 * Check for a USB configuration change from the host.
 */
usb_status_t check_usb_uart_config_change(void) {
    // Configure the USB UART interface if the host sends a change configuration request
    if (USBUART_IsConfigurationChanged())
    {
        /* Initialize IN endpoints when device is configured. */
        if (USBUART_GetConfiguration())
        {
            /* Enumeration is done, enable OUT endpoint to receive data 
             * from host. */
            USBUART_CDC_Init();
        }
    }
    return USB_SUCCESS;
}

/**
 * Send data over USB CDC. Wait until the USB device is ready before putting data.
 */
usb_status_t write_usb(uint8_t *buffer, size_t len) {
	// Wait until the usb device is ready
	while (USBUART_CDCIsReady() == USB_NOT_READY);

	// Check if we want to write a zero-length packet
	if (len == 0) {
		USBUART_PutData(NULL, 0);
		return USB_SUCCESS;
	}

	// Check that the buffer is not NULL, and we won't overflow
	if (buffer == NULL)
		return USB_INVALID_BUF;
	if (len > 64)
		return USB_BUF_OVERFLOW;

	// Otherwise write data
	USBUART_PutData(buffer, len);
	/* If the packet is exactly the length of the buffer, we put a zero
	 * length packet to ensure that the end of segment is properly 
	 * identified by the host */
	if (len == USBUART_BUFFER_SIZE)
		return write_usb(NULL, 0);

	// Succcessful write :)
	return USB_SUCCESS;
}

/**
 * Read data in from the USB device.
 */
usb_status_t read_usb_data(void) {
    uint8_t buffer[USBUART_BUFFER_SIZE];
    /* Check for input data from host. */
    if (USBUART_DataIsReady())
    {
    	size_t count = 0;
        /* Read received data and re-enable OUT endpoint. */
        count = USBUART_GetAll(buffer);
        if (count > 0)
        {
            // Store the received data in the UART buffer, unless there is an overflow
            // In the case of an overflow, we'll just reset the buffer
            if ((count + usb_input_buffer.buf_size) > USBUART_BUFFER_SIZE) {
                usb_input_buffer.buf_size = 0;
                return USB_BUF_OVERFLOW;
            }
            memcpy(usb_input_buffer.buf + usb_input_buffer.buf_size, buffer, count);
            usb_input_buffer.buf_size += count;

            // Check whether there is a command terminator in the buffer
            parse_usb_buffer();
        }
    }
    return USB_SUCCESS;
}

/**
 * Parse input buffer looking for commands, and handle commands
 */
usb_status_t parse_usb_buffer(void) {
    const size_t term_len = sizeof(term)/sizeof(char) - 1;

    // Look for terminators
    if (usb_input_buffer.buf_size < term_len)
        return USB_SUCCESS;
    for (size_t i = 0; i < usb_input_buffer.buf_size - (term_len-1); i += 1) {
        if (memcmp(usb_input_buffer.buf + i, term, term_len) == 0) {
            // We've found a terminator, handle the command
            usb_input_buffer.buf[i] = '\0';
            write_usb((uint8_t *)"Command: \"", 11);
            write_usb((uint8_t *)usb_input_buffer.buf, i - (term_len-1));
            write_usb((uint8_t *)"\"\r\n", 3);

            // Shuffle the buffer down
            const char *eoc = usb_input_buffer.buf + i + (term_len-1);
            const size_t len_remain = usb_input_buffer.buf_size - i - (term_len);
            memmove(usb_input_buffer.buf, eoc, len_remain);
            usb_input_buffer.buf_size = len_remain;

            // Reset the loop to the beginning of the list
            i = -1;
        }
    }
    return USB_SUCCESS;
}