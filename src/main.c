/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <string.h>
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"

#include "logo.h"

#include "arm_math.h"

#define TASK_SIMULATOR_STACK_SIZE (4096 / sizeof(portSTACK_TYPE))
#define TASK_SIMULATOR_STACK_PRIORITY (tskIDLE_PRIORITY)

#define RAIO 0.508/2
#define VEL_MAX_KMH  5.0f
#define VEL_MIN_KMH  0.5f
#define RAMP

#define SENSOR_PIO PIOA
#define SENSOR_PIO_ID ID_PIOA
#define SENSOR_PIO_PIN 19
#define SENSOR_PIO_PIN_MASK (1 << SENSOR_PIO_PIN)

LV_FONT_DECLARE(dseg12);
LV_FONT_DECLARE(dseg25);
LV_FONT_DECLARE(dseg70);
LV_FONT_DECLARE(dseg50);

typedef struct  {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t second;
} calendar;

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type);
static void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource);

/************************************************************************/
/* LCD / LVGL                                                           */
/************************************************************************/

#define LV_HOR_RES_MAX          (240)
#define LV_VER_RES_MAX          (320)

/*A static or global variable to store the buffers*/
static lv_disp_draw_buf_t disp_buf;

/*Static or global buffer(s). The second buffer is optional*/
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];
static lv_disp_drv_t disp_drv;          /*A variable to hold the drivers. Must be static or global.*/
static lv_indev_drv_t indev_drv;

static lv_obj_t * scr1;  // screen 1
static lv_obj_t * scr2;  // screen 2
static lv_obj_t * scr3;  // screen 2

static lv_obj_t * labelBtnConfig;
static lv_obj_t * labelBtnTrajeto;
static lv_obj_t * labelBtnHome;
static lv_obj_t * labelBtnRestart;
static lv_obj_t * labelBtnPlay;
static lv_obj_t * labelBtnStop;
lv_obj_t * labelClock1;
lv_obj_t * labelClock2;
lv_obj_t * labelClock3;
lv_obj_t * labelVel1;
lv_obj_t * labelVel2;
lv_obj_t * labelVelStr1;
lv_obj_t * labelVelStr2;
lv_obj_t * labelLine1;
lv_obj_t * labelLine2;
lv_obj_t * labelLine3;
lv_obj_t * labelTempo;
lv_obj_t * labelDist;
lv_obj_t * labelVm;
lv_obj_t * labelTempoStr;
lv_obj_t * labelDistStr;
lv_obj_t * labelVmStr;
lv_obj_t * labelAcl1;
lv_obj_t * labelAcl2;
lv_obj_t * labelAro; 
lv_obj_t * roller1;

SemaphoreHandle_t xSemaphoreConfig;
SemaphoreHandle_t xSemaphoreHome;
SemaphoreHandle_t xSemaphoreTrajeto;
SemaphoreHandle_t xSemaphoreSec;
SemaphoreHandle_t xSemaphorePulso; 


/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY            (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

volatile int n_pulsos = 0;

/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/
void sensor_callback(void){
	n_pulsos++;
	BaseType_t xHigherPriorityTaskWoken = pdTRUE;
	xSemaphoreGiveFromISR(xSemaphorePulso, &xHigherPriorityTaskWoken);
}

static void event_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		LV_LOG_USER("Clicked");
	}
	else if(code == LV_EVENT_VALUE_CHANGED) {
		LV_LOG_USER("Toggled");
	}
}

static void config_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xSemaphoreGiveFromISR(xSemaphoreConfig, &xHigherPriorityTaskWoken);
	}
}

static void home_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xSemaphoreGiveFromISR(xSemaphoreHome, &xHigherPriorityTaskWoken);
	}
}

static void trajeto_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xSemaphoreGiveFromISR(xSemaphoreTrajeto, &xHigherPriorityTaskWoken);
	}
}

static void roller_handler(lv_event_t * e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t * obj = lv_event_get_target(e);
	if(code == LV_EVENT_VALUE_CHANGED) {
		char buf[32];
		lv_roller_get_selected_str(obj, buf, sizeof(buf));
		LV_LOG_USER("Selected: %s\n", buf);
	}
}

void RTC_Handler(void) {
	uint32_t ul_status = rtc_get_status(RTC);
	
	/* seccond tick */
	if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC) {
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xSemaphoreGiveFromISR(xSemaphoreSec, &xHigherPriorityTaskWoken);
	}
	
	/* Time or date alarm */
	if ((ul_status & RTC_SR_ALARM) == RTC_SR_ALARM) {
		// o código para irq de alame vem aqui
	}

	rtc_clear_status(RTC, RTC_SCCR_SECCLR);
	rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
	rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
	rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
	rtc_clear_status(RTC, RTC_SCCR_CALCLR);
	rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
}

void lv_ex_btn_1(void) {
	lv_obj_t * label;

	lv_obj_t * btn1 = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -40);

	label = lv_label_create(btn1);
	lv_label_set_text(label, "Corsi");
	lv_obj_center(label);

	lv_obj_t * btn2 = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btn2, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 40);
	lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_set_height(btn2, LV_SIZE_CONTENT);

	label = lv_label_create(btn2);
	lv_label_set_text(label, "Toggle");
	lv_obj_center(label);
}

void create_scr_1(lv_obj_t * screen) {
	
	lv_obj_t * screen_color = screen;
	lv_obj_set_style_bg_color(screen_color, lv_color_black(), LV_PART_MAIN );
	
	lv_obj_t * img = lv_img_create(screen);
	lv_img_set_src(img, &logo);
	lv_obj_align(img, LV_ALIGN_TOP_LEFT, 5, 0);
	
	labelClock1 = lv_label_create(screen);
	lv_obj_align(labelClock1, LV_ALIGN_TOP_RIGHT, -5 , 10);
	lv_obj_set_style_text_font(labelClock1, &dseg12, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelClock1, lv_color_white(), LV_STATE_DEFAULT);
	
	labelLine1 = lv_label_create(screen);
	lv_obj_align(labelLine1, LV_ALIGN_TOP_MID, 0 , 10);
	lv_obj_set_style_text_color(labelLine1, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelLine1, "________________________");
	
	labelVel1 = lv_label_create(screen);
	lv_obj_align(labelVel1, LV_ALIGN_CENTER, -40, 0);
	lv_obj_set_style_text_font(labelVel1, &dseg70, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelVel1, lv_color_white(), LV_STATE_DEFAULT);
	
	labelVelStr1 = lv_label_create(screen);
	lv_obj_align_to(labelVelStr1, labelVel1, LV_ALIGN_OUT_RIGHT_BOTTOM, 55, 0);
	lv_obj_set_style_text_color(labelVelStr1, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelVelStr1, "Km/h");
	
	labelAcl1 = lv_label_create(screen);
	lv_obj_align_to(labelAcl1, labelVelStr1, LV_ALIGN_OUT_TOP_MID, 0, 0);
	lv_obj_set_style_text_color(labelAcl1, lv_color_white(), LV_STATE_DEFAULT);
	
	labelLine2 = lv_label_create(screen);
	lv_obj_align(labelLine2, LV_ALIGN_BOTTOM_MID, 0 , -40);
	lv_obj_set_style_text_color(labelLine2, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelLine2, "________________________");
	
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	
	lv_obj_t * btnConfig = lv_btn_create(screen);
	lv_obj_add_event_cb(btnConfig, config_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btnConfig, LV_ALIGN_BOTTOM_MID, -20, 0);
	lv_obj_add_style(btnConfig, &style, 0);

	labelBtnConfig = lv_label_create(btnConfig);
	lv_label_set_text(labelBtnConfig, LV_SYMBOL_SETTINGS);
	lv_obj_center(labelBtnConfig);
	
	lv_obj_t * btnTrajeto = lv_btn_create(screen);
	lv_obj_add_event_cb(btnTrajeto, trajeto_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnTrajeto, btnConfig, LV_ALIGN_RIGHT_MID, 40, -10);
	lv_obj_add_style(btnTrajeto, &style, 0);

	labelBtnTrajeto = lv_label_create(btnTrajeto);
	lv_label_set_text(labelBtnTrajeto,  LV_SYMBOL_IMAGE);
	lv_obj_center(labelBtnTrajeto);
	
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	
}

void create_scr_2(lv_obj_t * screen) {
	
	lv_obj_t * screen_color = screen;
	lv_obj_set_style_bg_color(screen_color, lv_color_black(), LV_PART_MAIN );
	
	lv_obj_t * img = lv_img_create(screen);
	lv_img_set_src(img, &logo);
	lv_obj_align(img, LV_ALIGN_TOP_LEFT, 5, 0);
	
	labelClock2 = lv_label_create(screen);
	lv_obj_align(labelClock2, LV_ALIGN_TOP_RIGHT, -5 , 10);
	lv_obj_set_style_text_font(labelClock2, &dseg12, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelClock2, lv_color_white(), LV_STATE_DEFAULT);
	//lv_label_set_text_fmt(labelClock, "17:45:05");
	
	labelLine1 = lv_label_create(screen);
	lv_obj_align(labelLine1, LV_ALIGN_TOP_MID, 0 , 10);
	lv_obj_set_style_text_color(labelLine1, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelLine1, "________________________");
	
	labelAro = lv_label_create(screen);
	lv_obj_align(labelAro, LV_ALIGN_TOP_MID, 0 , 50);
	lv_obj_set_style_text_color(labelAro, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelAro, "ARO");
	
	roller1 = lv_roller_create(screen);
	lv_roller_set_options(roller1,
	"20\n"
	"21\n"
	"22\n"
	"23\n"
	"24\n"
	"25",
	LV_ROLLER_MODE_NORMAL);

	lv_roller_set_visible_row_count(roller1, 3);
	lv_obj_center(roller1);
	lv_obj_add_event_cb(roller1, roller_handler, LV_EVENT_ALL, NULL);
	lv_roller_set_selected(roller1, 5, LV_ANIM_OFF);
	
	labelLine2 = lv_label_create(screen);
	lv_obj_align(labelLine2, LV_ALIGN_BOTTOM_MID, 0 , -40);
	lv_obj_set_style_text_color(labelLine2, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelLine2, "________________________");
	
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	
	lv_obj_t * btnHome = lv_btn_create(screen);
	lv_obj_add_event_cb(btnHome, home_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btnHome, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_add_style(btnHome, &style, 0);
	
	labelBtnHome = lv_label_create(btnHome);
	lv_label_set_text(labelBtnHome, LV_SYMBOL_HOME);
	lv_obj_center(labelBtnHome);
	
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	
}

void create_scr_3(lv_obj_t * screen) {
	
	lv_obj_t * screen_color = screen;
	lv_obj_set_style_bg_color(screen_color, lv_color_black(), LV_PART_MAIN );
	
	lv_obj_t * img = lv_img_create(screen);
	lv_img_set_src(img, &logo);
	lv_obj_align(img, LV_ALIGN_TOP_LEFT, 5, 0);
	
	labelClock3 = lv_label_create(screen);
	lv_obj_align(labelClock3, LV_ALIGN_TOP_RIGHT, -5 , 10);
	lv_obj_set_style_text_font(labelClock3, &dseg12, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelClock3, lv_color_white(), LV_STATE_DEFAULT);
	//lv_label_set_text_fmt(labelClock, "17:45:05");
	
	labelLine1 = lv_label_create(screen);
	lv_obj_align(labelLine1, LV_ALIGN_TOP_MID, 0 , 10);
	lv_obj_set_style_text_color(labelLine1, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelLine1, "________________________");
	
	labelVel2 = lv_label_create(screen);
	lv_obj_align(labelVel2, LV_ALIGN_LEFT_MID, 40, -70);
	lv_obj_set_style_text_font(labelVel2, &dseg70, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelVel2, lv_color_white(), LV_STATE_DEFAULT);
	//lv_label_set_text_fmt(labelVel, "18");
	
	labelVelStr2 = lv_label_create(screen);
	lv_obj_align_to(labelVelStr2, labelVel2, LV_ALIGN_OUT_RIGHT_BOTTOM, 110, 0);
	lv_obj_set_style_text_color(labelVelStr2, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelVelStr2, "Km/h");
	
	labelAcl2 = lv_label_create(screen);
	lv_obj_align_to(labelAcl2, labelVelStr2, LV_ALIGN_OUT_TOP_MID, 0, 0);
	lv_obj_set_style_text_color(labelAcl2, lv_color_white(), LV_STATE_DEFAULT);
	//lv_label_set_text(labelAcl2,  LV_SYMBOL_UP);
	
	labelLine3 = lv_label_create(screen);
	lv_obj_align_to(labelLine3, labelVelStr2, LV_ALIGN_OUT_BOTTOM_LEFT, -140 , -15);
	lv_obj_set_style_text_color(labelLine3, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelLine3, "________________________");
	
	labelTempoStr = lv_label_create(screen);
	lv_obj_align(labelTempoStr, LV_ALIGN_LEFT_MID, 0, 10);
	lv_obj_set_style_text_font(labelTempoStr, &lv_font_montserrat_20, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelTempoStr, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelTempoStr, "Tempo (h)");
	
	labelTempo = lv_label_create(screen);
	lv_obj_align_to(labelTempo, labelTempoStr, LV_ALIGN_OUT_RIGHT_MID, 15, 0);
	lv_obj_set_style_text_font(labelTempo, &dseg25, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelTempo, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelTempo, "01:00");
		
	labelDistStr = lv_label_create(screen);
	lv_obj_align_to(labelDistStr, labelTempoStr, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);
	lv_obj_set_style_text_font(labelDistStr, &lv_font_montserrat_20, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelDistStr, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelDistStr, "Dist (Km)");
	
	labelDist = lv_label_create(screen);
	lv_obj_align_to(labelDist, labelTempo, LV_ALIGN_OUT_BOTTOM_MID, 20, 15);
	lv_obj_set_style_text_font(labelDist, &dseg25, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelDist, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelDist, "4.5");
	
	labelVmStr = lv_label_create(screen);
	lv_obj_align_to(labelVmStr, labelDistStr, LV_ALIGN_OUT_BOTTOM_MID, -30, 15);
	lv_obj_set_style_text_font(labelVmStr, &lv_font_montserrat_20, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelVmStr, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelVmStr, "Vel.M (Km/h)");
	
	labelVm = lv_label_create(screen);
	lv_obj_align_to(labelVm, labelDist, LV_ALIGN_OUT_BOTTOM_MID, 20, 15);
	lv_obj_set_style_text_font(labelVm, &dseg25, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelVm, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelVm, "5");
	
	labelLine2 = lv_label_create(screen);
	lv_obj_align(labelLine2, LV_ALIGN_BOTTOM_MID, 0 , -40);
	lv_obj_set_style_text_color(labelLine2, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelLine2, "________________________");
	
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	
	lv_obj_t * btnPlay = lv_btn_create(screen);
	lv_obj_add_event_cb(btnPlay, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btnPlay, LV_ALIGN_BOTTOM_LEFT, 60, 0);
	lv_obj_add_style(btnPlay, &style, 0);
	
	labelBtnPlay = lv_label_create(btnPlay);
	lv_label_set_text(labelBtnPlay, LV_SYMBOL_PLAY);
	lv_obj_center(labelBtnPlay);
	
	lv_obj_t * btnStop = lv_btn_create(screen);
	lv_obj_add_event_cb(btnStop, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnStop, btnPlay, LV_ALIGN_OUT_RIGHT_MID, 0, -10);
	lv_obj_add_style(btnStop, &style, 0);
	
	labelBtnStop = lv_label_create(btnStop);
	lv_label_set_text(labelBtnStop, LV_SYMBOL_STOP);
	lv_obj_center(labelBtnStop);
	
	lv_obj_t * btnRestart = lv_btn_create(screen);
	lv_obj_add_event_cb(btnRestart, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnRestart, btnStop, LV_ALIGN_OUT_RIGHT_MID, 0, -10);
	lv_obj_add_style(btnRestart, &style, 0);
	
	labelBtnRestart = lv_label_create(btnRestart);
	lv_label_set_text(labelBtnRestart, LV_SYMBOL_REFRESH);
	lv_obj_center(labelBtnRestart);
	
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	
}


/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_lcd(void *pvParameters) {
	
	RTT_init(280, 0, 0);
	int primeiro_pulso = 1;
	
	int pulso_anterior = 0; 
	int pulso_atual = 0;
	
	double vel_anterior = 0.0;
	double vel_atual = 0.0;
	
	calendar rtc_initial = {2018, 3, 19, 12, 15, 45 ,1};
	RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_SECEN);
	
	int px, py;
	
	scr1  = lv_obj_create(NULL);
	create_scr_1(scr1);
	
	scr3  = lv_obj_create(NULL);
	create_scr_3(scr3);
	
	scr2  = lv_obj_create(NULL);
	create_scr_2(scr2);
	
	lv_scr_load(scr1);


	for (;;)  {
		
		if (xSemaphoreTake(xSemaphoreSec, 1)){
			int current_hour, current_min, current_sec;
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			
			char hour[8];
			sprintf(hour,"%d",current_hour);
			
			char min[8];
			sprintf(min,"%d",current_min);
			
			char sec[8];
			sprintf(sec,"%d",current_sec);
			
			char tempo[128];
			sprintf(tempo,"%s:%s:%s",hour,min,sec);
			lv_label_set_text_fmt(labelClock1, tempo);
			lv_label_set_text_fmt(labelClock2, tempo);
			lv_label_set_text_fmt(labelClock3, tempo);
		}
		
		if (xSemaphoreTake(xSemaphorePulso, 400)){
			//printf("%d\n", n_pulsos);
			if(primeiro_pulso){
				pulso_anterior = rtt_read_timer_value(RTT);
				pulso_atual = pulso_anterior;
				primeiro_pulso = 0;
			} else {
				pulso_atual = rtt_read_timer_value(RTT);
				printf("%d\n", pulso_atual);
				int t = pulso_atual - pulso_anterior;
				//printf("%d\n", t);
				double vel = 0.258 * 2 * 3.14 * (1.0 / t) * 1000;  // Use 1.0 to ensure floating-point division
				
				//printf("%f\n", vel);
				char vel_str[8];
				sprintf(vel_str, "%.1f", vel);  // Use %f for floating-point conversion
				vel_atual = atof(vel_str);

				lv_label_set_text_fmt(labelVel1, "%s", vel_str);  // Set label text to the string representation
				lv_label_set_text_fmt(labelVel2, "%s", vel_str);  // Set label text to the string representation

				if (vel_atual > vel_anterior) {
					lv_label_set_text(labelAcl1, "+");
					lv_label_set_text(labelAcl2, "+");
				} else if (vel_atual < vel_anterior) {
					lv_label_set_text(labelAcl1, "-");
					lv_label_set_text(labelAcl2, "-");
				} else {
					lv_label_set_text(labelAcl1, "");
					lv_label_set_text(labelAcl2, "");
				}

				pulso_anterior = pulso_atual;
				vel_anterior = vel_atual;
			}
		}
		
		if (xSemaphoreTake(xSemaphoreConfig, 1)){
			lv_scr_load(scr2);
		}
		
		if (xSemaphoreTake(xSemaphoreHome, 1)){
			lv_scr_load(scr1);
		}
		
		if (xSemaphoreTake(xSemaphoreTrajeto, 1)){
			lv_scr_load(scr3);
		}
		
		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}

float kmh_to_hz(float vel, float raio) {
	float f = vel / (2*PI*raio*3.6);
	return(f);
}

static void task_simulador(void *pvParameters) {

	pmc_enable_periph_clk(ID_PIOC);
	pio_set_output(PIOC, PIO_PC31, 1, 0, 0);

	float vel = VEL_MAX_KMH;
	float f;
	int ramp_up = 1;

	while(1){
		pio_clear(PIOC, PIO_PC31);
		delay_ms(1);
		pio_set(PIOC, PIO_PC31);
		#ifdef RAMP
		if (ramp_up) {
			printf("[SIMU] ACELERANDO: %d \n", (int) (10*vel));
			vel += 0.5;
			} else {
			printf("[SIMU] DESACELERANDO: %d \n",  (int) (10*vel));
			vel -= 0.5;
		}

		if (vel >= VEL_MAX_KMH)
		ramp_up = 0;
		else if (vel <= VEL_MIN_KMH)
		ramp_up = 1;
		#endif // Add this line to close the #ifdef block
		#ifndef RAMP
		vel = 5;
		//printf("[SIMU] CONSTANTE: %d \n", (int) (10*vel));
		#endif
		f = kmh_to_hz(vel, RAIO);
		int t = 965*(1.0/f); //UTILIZADO 965 como multiplicador ao invés de 1000
		//para compensar o atraso gerado pelo Escalonador do freeRTOS
		delay_ms(t);
	}
}

static void sensor_init(void) {
	
	pmc_enable_periph_clk(SENSOR_PIO_ID);

	pio_configure(SENSOR_PIO, PIO_INPUT, SENSOR_PIO_PIN_MASK, PIO_DEFAULT);
	pio_set_debounce_filter(SENSOR_PIO, SENSOR_PIO_PIN_MASK, 60);
	
	pio_handler_set(SENSOR_PIO,
	SENSOR_PIO_ID,
	SENSOR_PIO_PIN_MASK,
	PIO_IT_FALL_EDGE,
	sensor_callback);

	pio_enable_interrupt(SENSOR_PIO, SENSOR_PIO_PIN_MASK);
	pio_get_interrupt_status(SENSOR_PIO);

	NVIC_EnableIRQ(SENSOR_PIO_ID);
	NVIC_SetPriority(SENSOR_PIO_ID, 4);
}

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type) {
	/* Configura o PMC */
	pmc_enable_periph_clk(ID_RTC);

	/* Default RTC configuration, 24-hour mode */
	rtc_set_hour_mode(rtc, 0);

	/* Configura data e hora manualmente */
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.second);

	/* Configure RTC interrupts */
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 4);
	NVIC_EnableIRQ(id_rtc);

	/* Ativa interrupcao via alarme */
	rtc_enable_interrupt(rtc,  irq_type);
}

static void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource) {

	uint16_t pllPreScale = (int) (((float) 32768) / freqPrescale);
	
	rtt_sel_source(RTT, false);
	rtt_init(RTT, pllPreScale);
	
	if (rttIRQSource & RTT_MR_ALMIEN) {
		uint32_t ul_previous_time;
		ul_previous_time = rtt_read_timer_value(RTT);
		while (ul_previous_time == rtt_read_timer_value(RTT));
		rtt_write_alarm_time(RTT, IrqNPulses+ul_previous_time);
	}

	/* config NVIC */
	NVIC_DisableIRQ(RTT_IRQn);
	NVIC_ClearPendingIRQ(RTT_IRQn);
	NVIC_SetPriority(RTT_IRQn, 4);
	NVIC_EnableIRQ(RTT_IRQn);

	/* Enable RTT interrupt */
	if (rttIRQSource & (RTT_MR_RTTINCIEN | RTT_MR_ALMIEN))
	rtt_enable_interrupt(RTT, rttIRQSource);
	else
	rtt_disable_interrupt(RTT, RTT_MR_RTTINCIEN | RTT_MR_ALMIEN);
	
}

/************************************************************************/
/* configs                                                              */
/************************************************************************/

static void configure_lcd(void) {
	/**LCD pin configure on SPI*/
	pio_configure_pin(LCD_SPI_MISO_PIO, LCD_SPI_MISO_FLAGS);  //
	pio_configure_pin(LCD_SPI_MOSI_PIO, LCD_SPI_MOSI_FLAGS);
	pio_configure_pin(LCD_SPI_SPCK_PIO, LCD_SPI_SPCK_FLAGS);
	pio_configure_pin(LCD_SPI_NPCS_PIO, LCD_SPI_NPCS_FLAGS);
	pio_configure_pin(LCD_SPI_RESET_PIO, LCD_SPI_RESET_FLAGS);
	pio_configure_pin(LCD_SPI_CDS_PIO, LCD_SPI_CDS_FLAGS);
	
	ili9341_init();
	ili9341_backlight_on();
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = USART_SERIAL_EXAMPLE_BAUDRATE,
		.charlength = USART_SERIAL_CHAR_LENGTH,
		.paritytype = USART_SERIAL_PARITY,
		.stopbits = USART_SERIAL_STOP_BIT,
	};

	/* Configure console UART. */
	stdio_serial_init(CONSOLE_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

/************************************************************************/
/* port lvgl                                                            */
/************************************************************************/

void my_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
	ili9341_set_top_left_limit(area->x1, area->y1);   ili9341_set_bottom_right_limit(area->x2, area->y2);
	ili9341_copy_pixels_to_screen(color_p,  (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
	
	/* IMPORTANT!!!
	* Inform the graphics library that you are ready with the flushing*/
	lv_disp_flush_ready(disp_drv);
}

void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data) {
	int px, py, pressed;
	
	if (readPoint(&px, &py))
		data->state = LV_INDEV_STATE_PRESSED;
	else
		data->state = LV_INDEV_STATE_RELEASED; 
	
   data->point.x = py;
   data->point.y = 320 - px;
}

void configure_lvgl(void) {
	lv_init();
	lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX);
	
	lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
	disp_drv.draw_buf = &disp_buf;          /*Set an initialized buffer*/
	disp_drv.flush_cb = my_flush_cb;        /*Set a flush callback to draw to the display*/
	disp_drv.hor_res = LV_HOR_RES_MAX;      /*Set the horizontal resolution in pixels*/
	disp_drv.ver_res = LV_VER_RES_MAX;      /*Set the vertical resolution in pixels*/

	lv_disp_t * disp;
	disp = lv_disp_drv_register(&disp_drv); /*Register the driver and save the created display objects*/
	
	/* Init input on LVGL */
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = my_input_read;
	lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/
int main(void) {
	/* board and sys init */
	board_init();
	sysclk_init();
	configure_console();
	
	/* LCd, touch and lvgl init*/
	configure_lcd();
	ili9341_set_orientation(ILI9341_FLIP_Y | ILI9341_SWITCH_XY);
	
	configure_touch();
	configure_lvgl();
	
	sensor_init();
	
	xSemaphoreConfig = xSemaphoreCreateBinary();
	if (xSemaphoreConfig == NULL)
	printf("falha em criar o semaforo \n");
	
	xSemaphoreHome = xSemaphoreCreateBinary();
	if (xSemaphoreHome == NULL)
	printf("falha em criar o semaforo \n");
	
	xSemaphoreTrajeto = xSemaphoreCreateBinary();
	if (xSemaphoreTrajeto == NULL)
	printf("falha em criar o semaforo \n");
	
	xSemaphoreSec = xSemaphoreCreateBinary();
	if (xSemaphoreSec == NULL)
	printf("falha em criar o semaforo \n");
	
	xSemaphorePulso = xSemaphoreCreateBinary();
	if (xSemaphoreSec == NULL)
	printf("falha em criar o semaforo \n");
	
	/* Create task to control oled */
	if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}
	
	if (xTaskCreate(task_simulador, "SIMUL", TASK_SIMULATOR_STACK_SIZE, NULL, TASK_SIMULATOR_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}
	
	/* Start the scheduler. */
	vTaskStartScheduler();

	while(1){ }
}
