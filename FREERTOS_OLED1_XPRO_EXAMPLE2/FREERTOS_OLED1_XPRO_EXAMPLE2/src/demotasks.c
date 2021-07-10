/**
 * \file
 *
 * \brief FreeRTOS demo task implementations.
 *
 * Copyright (c) 2013-2018 Microchip Technology Inc. and its subsidiaries.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Subject to your compliance with these terms, you may use Microchip
 * software and any derivatives exclusively with Microchip products.
 * It is your responsibility to comply with third party license terms applicable
 * to your use of third party software (including open source software) that
 * may accompany Microchip software.
 *
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
 * INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
 * AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
 * LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
 * LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
 * SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
 * POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
 * ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
 * RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 * THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * \asf_license_stop
 *
 */

#include <asf.h>
#include <conf_demo.h>
#include "demotasks.h"
#include <string.h>

/**
 * \addtogroup freertos_sam0_demo_tasks_group
 *
 * @{
 */

//! \name Task configuration
//@{

#define UART_TASK_PRIORITY      (tskIDLE_PRIORITY + 3)
#define UART_TASK_DELAY         (10 / portTICK_RATE_MS)

#define MAIN_TASK_PRIORITY      (tskIDLE_PRIORITY + 2)
#define MAIN_TASK_DELAY         (100 / portTICK_RATE_MS)

#define ABOUT_TASK_PRIORITY     (tskIDLE_PRIORITY + 1)
#define ABOUT_TASK_DELAY        (33 / portTICK_RATE_MS)

//@}


//! \name Menu and display configuration
//@{

//! Available selections in menu
enum menu_items {
	MENU_ITEM_TERMINAL,
	MENU_ITEM_ABOUT,
	MENU_NUM_ITEMS,
};

//! Height of menu bar
#define MENU_HEIGHT      8

//! Width per menu item
#define MENU_ITEM_WIDTH  \
	(((GFX_MONO_LCD_WIDTH - (MENU_NUM_ITEMS - 1))) / MENU_NUM_ITEMS)

//! Height of area in which to draw content
#define CANVAS_HEIGHT  \
	((GFX_MONO_LCD_HEIGHT / 2) - (MENU_HEIGHT + 1))

//! Width of area in which to draw content
#define CANVAS_WIDTH           (GFX_MONO_LCD_WIDTH)

//! Offset of Y-coordinate for display buffer of graph
#define CANVAS_GRAPH_Y_OFFSET  (GFX_MONO_LCD_HEIGHT / 2)

//! Character lines on display
#define TERMINAL_LINES  \
	(1 + ((CANVAS_HEIGHT - SYSFONT_HEIGHT) / (SYSFONT_HEIGHT + 1)))

//! Character columns on display
#define TERMINAL_COLUMNS         (CANVAS_WIDTH / SYSFONT_WIDTH)

//! Character lines in terminal buffer
#define TERMINAL_BUFFER_LINES    (1 + TERMINAL_LINES)

//! Character columns in terminal buffer
#define TERMINAL_BUFFER_COLUMNS  (1 + TERMINAL_COLUMNS)

//@}


//! \name Global constants and variables
//@{

//! Labels for menu items
static const char menu_items_text[MENU_NUM_ITEMS][6] = {
	"ELC",
	"1048",
	"FINAL",
};

//! Text to display on about screen
static const char about_text[] =
	"   SEJA BEM VINDO!"
	"                     "
	"      DIGITE A SENHA:";
	

/**
 * \brief Instance for \ref oled1_xpro_io_group
 *
 * The extension header to use is configured with \ref OLED1_EXT_HEADER.
 */
static OLED1_CREATE_INSTANCE(oled1, OLED1_EXT_HEADER);

//! Instance for \ref edbg_cdc_rx_group
static struct usart_module cdc_usart;

//! Buffer for terminal text
static uint8_t terminal_buffer[TERMINAL_BUFFER_LINES][TERMINAL_BUFFER_COLUMNS];

//! Index of latest terminal line (first to be printed)
static uint8_t terminal_line_offset;

//! Queue for incoming terminal characters
static xQueueHandle terminal_in_queue;

//! Semaphore to signal busy display
static xSemaphoreHandle display_mutex;

//! Semaphore to signal busy terminal buffer
static xSemaphoreHandle terminal_mutex;

//! Handle for terminal output task
static xTaskHandle terminal_task_handle;

//! Handle for about screen task
static xTaskHandle about_task_handle;

//@}


//! \name Tasks for demo
//@{

static void main_task(void *params); 
static void about_task(void *params);
static void uart_task(void *params);

//@}

//! Interrupt handler for reception from EDBG Virtual COM Port
static void cdc_rx_handler(uint8_t instance);


/**
 * \brief Initialize tasks and resources for demo
 *
 * This function initializes the \ref oled1_xpro_io_group instance and the
 * \ref edbg_cdc_rx_group instance for reception, then creates all
 * the objects for FreeRTOS to run the demo.
 */

void demotasks_init(void)
{
	// Initialize hardware for the OLED1 Xplained Pro driver instance
	oled1_init(&oled1);

	// Configure SERCOM USART for reception from EDBG Virtual COM Port
	cdc_rx_init(&cdc_usart, &cdc_rx_handler);

	display_mutex  = xSemaphoreCreateMutex();
	terminal_mutex = xSemaphoreCreateMutex();
	terminal_in_queue = xQueueCreate(64, sizeof(uint8_t));

	//utilizado About Como tela principal
	xTaskCreate(about_task,
			(const char *)"About",
			configMINIMAL_STACK_SIZE,
			NULL,
			ABOUT_TASK_PRIORITY,
			&about_task_handle);

	xTaskCreate(main_task,
			(const char *) "Main",
			configMINIMAL_STACK_SIZE,
			NULL,
			MAIN_TASK_PRIORITY,
			NULL);
			
	xTaskCreate(uart_task,
			(const char *) "UART",
			configMINIMAL_STACK_SIZE,
			NULL,
			UART_TASK_PRIORITY,
			NULL);

	// Suspend these since the main task will control their execution
	vTaskSuspend(about_task_handle);
	vTaskSuspend(terminal_task_handle);
}


/**
 * \brief Main demo task
 *
 * This task keeps track of which screen the user has selected, which tasks
 * to resume/suspend to draw the selected screen, and also draws the menu bar.
 *
 * The menu bar shows which screens the user can select by clicking the
 * corresponding buttons on the OLED1 Xplained Pro:
 * - \ref graph_task() "graph" (selected at start-up)
 * - \ref terminal_task() "term."
 * - \ref about_task() "about"
 *
 * \param params Parameters for the task. (Not used.)
 */

static void apaga_display(void)
{
	gfx_mono_draw_string("                    ", 0 , 0 , &sysfont);
	gfx_mono_draw_string("                    ", 0 , 11 , &sysfont);
	gfx_mono_draw_string("                    ", 0 , 12 , &sysfont);	
	gfx_mono_draw_string("                    ", 0 , 13 , &sysfont);
	gfx_mono_draw_string("                    ", 0 , 14 , &sysfont);
	gfx_mono_draw_string("                    ", 0 , 15 , &sysfont);
	gfx_mono_draw_string("                    ", 0 , 16 , &sysfont);
}


static void main_task(void *params)
{ 	
	int vari = 0;
	int senha[4];
	int testea[4];
	testea[0] = 3; //Por inicar no about ele pega o primeiro valor como 3, usado somente para igualar 
	testea[1] = 1;
	testea[2] = 1;
	testea[3] = 1;
	
	bool selection_changed = true;
	bool select_graph_buffer;
	enum menu_items current_selection;
	gfx_coord_t x, y, display_y_offset;
	xTaskHandle temp_task_handle = NULL;

	for(;;) {
		// Show that task is executing
		oled1_set_led_state(&oled1, OLED1_LED3_ID, true);
		current_selection = MENU_ITEM_ABOUT;
				
			
		// Check buttons to see if user changed the selection
		if (oled1_get_button_state(&oled1, OLED1_BUTTON1_ID)) 
		{				
			senha[vari] = 1;
			vari++;									

		} else if (oled1_get_button_state(&oled1, OLED1_BUTTON2_ID)) 
		{			
			senha[vari] = 2;	
			vari++;		

		} else if (oled1_get_button_state(&oled1, OLED1_BUTTON3_ID)) 
		{			
			senha[vari] = 3;
			vari++;						
		}
		
		if (vari == 2)
		{
			apaga_display();
			gfx_mono_draw_string("         * ", 0 , 10 , &sysfont);
		}else if (vari == 3)
		{
			apaga_display();
			gfx_mono_draw_string("         * * ", 0 , 10 , &sysfont);
		}

		if (vari == 4)
		{	
			//Calcula os numeros para ver se as senhas batem 
			int verdade = ((senha[3] - testea[3]) + (senha[2] - testea[2]) + (senha[1] - testea[1]) + (senha[0] - testea[0]));
			
			if(verdade == 0)
			{
				gfx_mono_draw_string("         * * *", 0 , 10 , &sysfont);
				delay_ms(500);
				apaga_display();
				gfx_mono_draw_string("    SENHA CORRETA!", 0 , 5 , &sysfont);	
				delay_ms(1000);
				Reset_Handler();
				
			}
			else
			{
				gfx_mono_draw_string("         * * *", 0 , 10 , &sysfont);
				delay_ms(500);
				apaga_display();
				gfx_mono_draw_string("   SENHA INCORRETA! ", 0 , 5 , &sysfont);	
				delay_ms(1000);
				Reset_Handler();
			}
		}	
		
		// If selection changed, handle the selection
		if (selection_changed) {
			// Wait for and take the display semaphore before doing any changes.
			xSemaphoreTake(display_mutex, portMAX_DELAY);

			temp_task_handle = about_task_handle;
			select_graph_buffer = false;

			// Select and initialize display buffer to use.
			display_y_offset = select_graph_buffer ? CANVAS_GRAPH_Y_OFFSET : 0;

			// Draw the menu bar (only needs to be done once for graph)
			
			// Clear the selected display buffer first
			gfx_mono_draw_filled_rect(0, display_y_offset,
					GFX_MONO_LCD_WIDTH, GFX_MONO_LCD_HEIGHT / 2,
					GFX_PIXEL_CLR);

			// Draw menu lines, each item with height MENU_HEIGHT pixels
			y = display_y_offset + CANVAS_HEIGHT;
			gfx_mono_draw_horizontal_line(0, y, GFX_MONO_LCD_WIDTH,
					GFX_PIXEL_SET);

			x = MENU_ITEM_WIDTH;
			y++;

			for (uint8_t i = 0; i < (MENU_NUM_ITEMS - 1); i++) {
				gfx_mono_draw_vertical_line(x, y, MENU_HEIGHT,
						GFX_PIXEL_SET);
				x += 1 + MENU_ITEM_WIDTH;
			}

			// Highlight the current selection
			gfx_mono_draw_rect(current_selection * (1 + MENU_ITEM_WIDTH), y,
					MENU_ITEM_WIDTH, MENU_HEIGHT, GFX_PIXEL_SET);

			// Draw the menu item text
			x = (MENU_ITEM_WIDTH / 2) - ((5 * SYSFONT_WIDTH) / 2);
			y += (MENU_HEIGHT / 2) - (SYSFONT_HEIGHT / 2);

			for (uint8_t i = 0; i < MENU_NUM_ITEMS; i++) {
				gfx_mono_draw_string(menu_items_text[i], x, y, &sysfont);
				x += 1 + MENU_ITEM_WIDTH;
			}
	

			// Set display controller to output the new buffer
			ssd1306_set_display_start_line_address(display_y_offset);

			// We are done modifying the display, so give back the mutex
			xSemaphoreGive(display_mutex);

			selection_changed = false;

			// If a task handle was specified, resume it now
			if (temp_task_handle) {
				vTaskResume(temp_task_handle);
			}
		}

		// Show that task is done
		oled1_set_led_state(&oled1, OLED1_LED3_ID, false);

		vTaskDelay(MAIN_TASK_DELAY);
	}
}

/**
 * \brief About task
 *
 * This task prints a short text about the demo, with a simple zooming
 * animation.
 *
 * \param params Parameters for the task. (Not used.)
 */
static void about_task(void *params) // TELA INCIAL DO SISTEMA 
{
	char c;
	gfx_coord_t x, y;
	uint8_t i, shift;

	const uint8_t max_shift = 8;
	shift = 1;

	for (;;) {
		oled1_set_led_state(&oled1, OLED1_LED2_ID, true);

		xSemaphoreTake(display_mutex, portMAX_DELAY);

		// Print the about text in an expanding area
		for (i = 0; i < (sizeof(about_text) - 1); i++) {
			c = about_text[i];
			x = (((i % TERMINAL_COLUMNS) * SYSFONT_WIDTH) * shift
					+ (CANVAS_WIDTH / 2) * (max_shift - shift))
					/ max_shift;
			y = (((i / TERMINAL_COLUMNS) * SYSFONT_HEIGHT) * shift
					+ (CANVAS_HEIGHT / 2) * (max_shift - shift))
					/ max_shift;
			gfx_mono_draw_char(c, x, y, &sysfont);
		}

		xSemaphoreGive(display_mutex);

		oled1_set_led_state(&oled1, OLED1_LED2_ID, false);

		// Repeat task until we're displaying the text in full size
		if (shift < max_shift) {
			shift++;
			vTaskDelay(ABOUT_TASK_DELAY);
		} else {
			shift = 0;
			vTaskSuspend(NULL);
		}
	}
}

/**
 * \brief UART task
 *
 * This task runs in the background to handle the queued, incoming terminal
 * characters and write them to the terminal text buffer. It does not print
 * anything to the display -- that is done by \ref terminal_task().
 *
 * \param params Parameters for the task. (Not used.)
 */
static void uart_task(void *params)
{
	uint8_t *current_line_ptr;
	uint8_t *current_char_ptr;
	uint8_t current_column = 0;

	for (;;) {
		// Show that task is executing
		oled1_set_led_state(&oled1, OLED1_LED1_ID, true);

		// Grab terminal mutex
		xSemaphoreTake(terminal_mutex, portMAX_DELAY);

		current_line_ptr = terminal_buffer[terminal_line_offset];
		current_char_ptr = current_line_ptr + current_column;

		// Any characters queued? Handle them!
		while (xQueueReceive(terminal_in_queue, current_char_ptr, 0)) {
			/* Newline-handling is difficult because all terminal emulators
			 * seem to do it their own way. The method below seems to work
			 * with Putty and Realterm out of the box.
			 */
			switch (*current_char_ptr) {
			case '\r':
				// Replace \r with \0 and move head to next line
				*current_char_ptr = '\0';

				current_column = 0;
				terminal_line_offset = (terminal_line_offset + 1)
						% TERMINAL_BUFFER_LINES;
				current_line_ptr = terminal_buffer[terminal_line_offset];
				current_char_ptr = current_line_ptr + current_column;
				break;

			case '\n':
				// For \n, do nothing -- it is replaced with \0 later
				break;

			default:
				// For all other characters, just move head to next char
				current_column++;
				if (current_column >= TERMINAL_COLUMNS) {
					current_column = 0;
					terminal_line_offset = (terminal_line_offset + 1)
							% TERMINAL_BUFFER_LINES;
					current_line_ptr = terminal_buffer[terminal_line_offset];
				}
				current_char_ptr = current_line_ptr + current_column;
			}

			// Set zero-terminator at head
			*current_char_ptr = '\0';
		}

		xSemaphoreGive(terminal_mutex);

		oled1_set_led_state(&oled1, OLED1_LED1_ID, false);

		vTaskDelay(UART_TASK_DELAY);
	}
}

/**
 * \internal
 * \brief UART interrupt handler for reception on EDBG CDC UART
 *
 * This function is based on the interrupt handler of the SERCOM USART callback
 * driver (\ref _usart_interrupt_handler()). It has been modified to only handle
 * the receive interrupt and to push the received data directly into the queue
 * for terminal characters (\ref terminal_in_queue), and echo the character back
 * to the sender.
 *
 * \param instance Instance number of SERCOM that generated interrupt.
 */
static void cdc_rx_handler(uint8_t instance)
{
	SercomUsart *const usart_hw = (SercomUsart *)EDBG_CDC_MODULE;
	uint16_t interrupt_status;
	uint16_t data;
	uint8_t error_code;

	// Wait for synch to complete
#if defined(FEATURE_SERCOM_SYNCBUSY_SCHEME_VERSION_1)
	while (usart_hw->STATUS.reg & SERCOM_USART_STATUS_SYNCBUSY) {
	}
#elif defined(FEATURE_SERCOM_SYNCBUSY_SCHEME_VERSION_2)
	while (usart_hw->SYNCBUSY.reg) {
	}
#endif

	// Read and mask interrupt flag register
	interrupt_status = usart_hw->INTFLAG.reg;

	if (interrupt_status & SERCOM_USART_INTFLAG_RXC) {
		// Check for errors
		error_code = (uint8_t)(usart_hw->STATUS.reg & SERCOM_USART_STATUS_MASK);
		if (error_code) {
			// Only frame error and buffer overflow should be possible
			if (error_code &
					(SERCOM_USART_STATUS_FERR | SERCOM_USART_STATUS_BUFOVF)) {
				usart_hw->STATUS.reg =
						SERCOM_USART_STATUS_FERR | SERCOM_USART_STATUS_BUFOVF;
			} else {
				// Error: unknown failure
			}
		// All is fine, so push the received character into our queue
		} else {
			data = (usart_hw->DATA.reg & SERCOM_USART_DATA_MASK);

			if (!xQueueSendFromISR(terminal_in_queue, (uint8_t *)&data,
						NULL)) {
				// Error: could not enqueue character
			} else {
				// Echo back! Data reg. should empty fast since this is the
				// only place anything is sent.
				while (!(interrupt_status & SERCOM_USART_INTFLAG_DRE)) {
					interrupt_status = usart_hw->INTFLAG.reg;
				}
				usart_hw->DATA.reg = (uint8_t)data;
			}
		}
	} else {
		// Error: only RX interrupt should be enabled
	}
}

/** @} */
