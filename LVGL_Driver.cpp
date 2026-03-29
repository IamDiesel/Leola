/*****************************************************************************
  | File        :   LVGL_Driver.c
  | help        : 
    The provided LVGL library file must be installed first
******************************************************************************/
#include "LVGL_Driver.h"

uint32_t bufSize;
lv_color_t *disp_draw_buf1;
lv_color_t *disp_draw_buf2;
    
static StaticSemaphore_t lvgl_mutex_buf;
static SemaphoreHandle_t lvgl_mutex = NULL;

// NEU: Globale Variable, damit der Interrupt LVGL findet
lv_display_t * global_disp = NULL;

// NEU: Das ist die Bruecke zwischen dem Hardware-Interrupt und LVGL
void lvgl_flush_ready_callback(void) {
    if (global_disp != NULL) {
        lv_display_flush_ready(global_disp);
    }
}

static void lvgl_port_lock_init(void) {
    if(lvgl_mutex == NULL) {
        lvgl_mutex = xSemaphoreCreateRecursiveMutexStatic(&lvgl_mutex_buf);
        if(lvgl_mutex == NULL) {
            Serial.println("Failed to create LVGL recursive mutex!");
        }
    }
}

bool lvgl_port_lock(uint32_t timeout_ms) {
    if(lvgl_mutex == NULL) return false;
    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if(xSemaphoreTakeRecursive(lvgl_mutex, timeout_ticks) == pdTRUE) {
        return true;
    } else {
        Serial.println("LVGL lock timeout!");
        return false;
    }
}

void lvgl_port_unlock(void) {
    if(lvgl_mutex != NULL) {
        xSemaphoreGiveRecursive(lvgl_mutex);
    }
}

void my_disp_flush( lv_display_t *disp, const lv_area_t *area, uint8_t * px_map)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  
  lv_draw_sw_rgb565_swap(px_map, w * h);
  
  // Das hier loest im Hintergrund die asynchrone SPI DMA Uebertragung aus
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, ( uint16_t *)px_map);
  
  // WICHTIG: Das fruehere lv_display_flush_ready(disp); wurde hier ABSICHTLICH GELÖSCHT!
  // Es wird jetzt erst im Hardware-Interrupt asynchron aufgerufen, wenn die Uebertragung wirklich fertig ist.
}

void my_touchpad_read( lv_indev_t * indev_drv, lv_indev_data_t * data )
{
  Touch_Read_Data();
  if (touch_data.points != 0x00) {
    data->point.x = touch_data.x;
    data->point.y = touch_data.y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  if (touch_data.gesture != NONE ) {    
  }
  touch_data.x = 0;
  touch_data.y = 0;
  touch_data.points = 0;
  touch_data.gesture = NONE;
}

static uint32_t my_tick(void) { return millis(); }

void LVGL_Loop(void *parameter)
{
    while(1)
    {
      lvgl_port_lock(0);
      lv_timer_handler(); 
      lvgl_port_unlock();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}


void Lvgl_Init(void)
{
  lvgl_port_lock_init();

  bufSize = LCD_WIDTH * 15; 
  uint32_t draw_size_bytes = bufSize * 2;
  
  // Perfekt: Hier waren die DMA Puffer bereits richtig angelegt!
  disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(draw_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(draw_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

  if (!disp_draw_buf1 || !disp_draw_buf2) {
      Serial.println("Kritischer Fehler: DMA Speicher fuer LVGL konnte nicht allokiert werden!");
  }

  lv_init();
  lv_tick_set_cb(my_tick);

  global_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  
  lv_display_set_color_format(global_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(global_disp, my_disp_flush);
  
  // Da der Interrupt jetzt laeuft, nutzt LVGL ab sofort astreines Hardware-Ping-Pong
  lv_display_set_buffers(global_disp, disp_draw_buf1, disp_draw_buf2, draw_size_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  xTaskCreatePinnedToCore(LVGL_Loop, "LVGL Loop", 16384, NULL, 4, NULL, 0);
}