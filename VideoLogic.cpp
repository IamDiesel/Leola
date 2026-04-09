#pragma GCC optimize ("O3") 
#include "VideoLogic.h"
#include "SharedData.h"
#include "WebSetupLogic.h" 
#include <WiFi.h>
#include <HTTPClient.h>      
#include "secrets.h"         
#include "GuiManager.h"      
#include "ViewBaby.h"        
#include <JPEGDEC.h> 
#include "esp_lcd_panel_ops.h"
#include "Touch_CST816.h"
#include "esp_attr.h"

#ifndef RGB565_LITTLE_ENDIAN
#define RGB565_LITTLE_ENDIAN 0
#define RGB565_BIG_ENDIAN 1
#endif

#ifndef JPEG_SCALE_HALF
#define JPEG_SCALE_HALF 1
#define JPEG_SCALE_QUARTER 2
#define JPEG_SCALE_EIGHTH 4
#endif

extern esp_lcd_panel_handle_t panel_handle;
extern bool lvgl_port_lock(uint32_t timeout_ms);
extern void lvgl_port_unlock(void);

volatile bool requestImageLoad = false; 

static lv_image_dsc_t cam_img_dsc[2] = {{0}, {0}};
static uint8_t dsc_idx = 0;
static uint8_t* jpg_bufs[2] = {nullptr, nullptr}; 

DMA_ATTR __attribute__((aligned(64))) static uint16_t dma_chunk_bufs[2][360 * 16];
static uint8_t dma_chunk_idx = 0;

static JPEGDEC jpeg;
volatile uint16_t* jpeg_decode_target = nullptr;
volatile uint16_t jpeg_decode_width = 0;
volatile uint16_t jpeg_decode_height = 0;
volatile int jpeg_decode_offset_x = 0;
volatile int jpeg_decode_offset_y = 0;
volatile int currentStreamScale = 0; 

static TaskHandle_t videoTaskHandle = NULL;

#define MAX_JPEG_DOWNLOAD_SIZE 65536    
#define MAX_PIXEL_BUF_SIZE (360 * 360 * 2) 

void VideoLogic_TriggerImageLoad() { requestImageLoad = true; }

static void setUiStatus(const char* msg) {
    if (lvgl_port_lock(portMAX_DELAY)) { ViewBaby_SetStatus(msg); lvgl_port_unlock(); }
}

int JPEGDraw(JPEGDRAW *pDraw) {
    if (!jpeg_decode_target) return 0;
    int x = pDraw->x; int y = pDraw->y; int draw_width = pDraw->iWidth; int draw_height = pDraw->iHeight;
    if (x + draw_width > jpeg_decode_width) draw_width = jpeg_decode_width - x;
    if (y + draw_height > jpeg_decode_height) draw_height = jpeg_decode_height - y;
    if (draw_width <= 0 || draw_height <= 0) return 1;

    if (vidFSMode) {
        uint16_t* dma_safe_buf = dma_chunk_bufs[dma_chunk_idx];
        static int current_dma_y = 0; static int current_dma_h = 0;

        if (x == 0) { current_dma_y = y; current_dma_h = draw_height; }
        if (draw_height > current_dma_h) current_dma_h = draw_height;

        uint16_t *src_ptr = pDraw->pPixels; 
        uint16_t *dst_ptr = &dma_safe_buf[x]; 

        // DIE EINZIGE OPTIMIERUNG DIE BLEIBT: 
        // Hardware Endian Swap per Library + memcpy! Keine Bit-Verschiebungen mehr.
        for (int row = 0; row < draw_height; row++) {
            memcpy(dst_ptr, src_ptr, draw_width * 2);
            src_ptr += pDraw->iWidth; 
            dst_ptr += jpeg_decode_width;        
        }
        
        if (x + draw_width >= jpeg_decode_width) {
            if (showFps) { 
                if (current_dma_y <= 9) {
                    static const uint8_t tinyFont[10][5] = { {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,4,4}, {7,5,7,5,7}, {7,5,7,1,7} };
                    int tempFps = currentFps; int tens = 0; while(tempFps >= 10) { tens++; tempFps -= 10; } int ones = tempFps; const uint16_t green = 0xE007; 
                    for (int by = 0; by < 7; by++) {
                        int global_y = 2 + by;
                        if (global_y >= current_dma_y && global_y < current_dma_y + current_dma_h) {
                            int local_y = global_y - current_dma_y;
                            for (int bx = 0; bx < 11; bx++) {
                                int global_x = 36 + bx; 
                                if (global_x >= 0 && global_x < jpeg_decode_width) {
                                    bool is_text = false;
                                    if (bx >= 1 && bx < 4 && by >= 1 && by < 6) { if (tinyFont[tens][by-1] & (1 << (2 - (bx-1)))) is_text = true; }
                                    else if (bx >= 7 && bx < 10 && by >= 1 && by < 6) { if (tinyFont[ones][by-1] & (1 << (2 - (bx-7)))) is_text = true; }
                                    if (is_text) dma_safe_buf[local_y * jpeg_decode_width + global_x] = green;
                                }
                            }
                        }
                    }
                }
            }
            esp_lcd_panel_draw_bitmap(panel_handle, jpeg_decode_offset_x, jpeg_decode_offset_y + current_dma_y, jpeg_decode_offset_x + jpeg_decode_width, jpeg_decode_offset_y + current_dma_y + current_dma_h, dma_safe_buf);
            dma_chunk_idx = (dma_chunk_idx + 1) & 1; 
        }
    } else {
        uint16_t *src_ptr = pDraw->pPixels; uint16_t *dst_ptr = (uint16_t*)&jpeg_decode_target[y * jpeg_decode_width + x]; 
        for (int row = 0; row < draw_height; row++) { memcpy(dst_ptr, src_ptr, draw_width * 2); src_ptr += pDraw->iWidth; dst_ptr += jpeg_decode_width; }
    }
    return 1; 
}

template <bool FAST_MODE>
void processStream(WiFiClient* stream, uint8_t* download_buf) {
    int scaleShift = 0;
    currentStreamScale = -1;

    while (isStreamActive && stream->connected()) {
        
        if (FAST_MODE == screenshotModeActive) return; 

        if (vidFSMode) {
            static uint32_t lastTouchCheck = 0;
            if (millis() - lastTouchCheck > 100) { 
                lastTouchCheck = millis(); Touch_Read_Data(); 
                if (touch_data.points > 0 || alarmActive || babyAlarmActive || disconnectAlarmActive) {
                    vidFSMode = false; touch_data.points = 0; 
                    if (lvgl_port_lock(portMAX_DELAY)) { lv_obj_invalidate(lv_scr_act()); lvgl_port_unlock(); }
                }
            }
        }
        
        int frameSize = 0; bool headerDone = false; int headerLen = 0; char headerBuf[128];
        
        // ZURUECKGEROLLT AUF DIE URSPRUENGLICHE LOGIK (Stabil)
        while (stream->connected() && isStreamActive) {
            int avail = stream->available();
            if (avail > 0) {
                char c = stream->read();
                if (c == '\n') {
                    headerBuf[headerLen] = '\0';
                    if (headerLen > 14) { 
                        String line = String(headerBuf); line.trim(); line.toLowerCase();
                        if (line.startsWith("content-length:")) frameSize = line.substring(15).toInt();
                    }
                    if (headerLen <= 1 && frameSize > 0) { headerDone = true; break; }
                    headerLen = 0; 
                } else if (c != '\r') { if (headerLen < 127) headerBuf[headerLen++] = c; }
            } else { 
                if (FAST_MODE) { taskYIELD(); } else { vTaskDelay(1); }
            }
        }

        if (!headerDone || !isStreamActive) break;

        if (frameSize > 0 && frameSize <= MAX_JPEG_DOWNLOAD_SIZE) {
            bool skipDecoding = false;
            int safeThreshold = (mjpegDropThreshold < 2048) ? 2048 : mjpegDropThreshold;
            if (stream->available() > safeThreshold) skipDecoding = true; 

            if (skipDecoding) {
                static uint8_t trash_buf[512]; size_t skipped = 0; uint32_t startSkip = millis();
                while (skipped < frameSize && stream->connected() && isStreamActive && (millis() - startSkip < 1000)) {
                    int avail = stream->available();
                    if (avail > 0) {
                        size_t toSkip = frameSize - skipped; if (toSkip > sizeof(trash_buf)) toSkip = sizeof(trash_buf);
                        size_t readNow = stream->read(trash_buf, toSkip); if (readNow > 0) skipped += readNow;
                    } else { 
                        if (FAST_MODE) { taskYIELD(); } else { vTaskDelay(1); }
                    }
                }
                continue; 
            }

            size_t bytesRead = 0; uint32_t startReadTime = millis();
            while (bytesRead < frameSize && stream->connected() && isStreamActive && (millis() - startReadTime < 3000)) {
                int avail = stream->available();
                if (avail > 0) {
                    size_t toRead = frameSize - bytesRead; if (toRead > avail) toRead = avail;
                    size_t readNow = stream->read(download_buf + bytesRead, toRead); if (readNow > 0) bytesRead += readNow;
                } else { 
                    if (FAST_MODE) { taskYIELD(); } else { vTaskDelay(1); }
                }
            }

            if (bytesRead == frameSize) {
                if (download_buf[0] == 0xFF && download_buf[1] == 0xD8) {
                    if (jpeg.openRAM(download_buf, frameSize, JPEGDraw)) {
                        
                        // ZWEITER TEIL DER OPTIMIERUNG: Library konvertiert Farben
                        if (vidFSMode) {
                            jpeg.setPixelType(RGB565_BIG_ENDIAN); 
                        } else {
                            jpeg.setPixelType(RGB565_LITTLE_ENDIAN); 
                        }

                        if (currentStreamScale == -1) {
                            int originalWidth = jpeg.getWidth(); int originalHeight = jpeg.getHeight(); int test_shift = 0;
                            while(test_shift < 3) {
                                uint16_t tw = originalWidth >> test_shift; uint16_t th = originalHeight >> test_shift;
                                if (tw <= 360 && th <= 360) break; test_shift++;
                            }
                            scaleShift = test_shift;
                            if (scaleShift == 0) currentStreamScale = 0; else if (scaleShift == 1) currentStreamScale = JPEG_SCALE_HALF; else if (scaleShift == 2) currentStreamScale = JPEG_SCALE_QUARTER; else if (scaleShift == 3) currentStreamScale = JPEG_SCALE_EIGHTH;
                            uint16_t w_temp = originalWidth >> scaleShift; uint16_t h_temp = originalHeight >> scaleShift;
                            jpeg_decode_offset_x = (360 - w_temp) >> 1; jpeg_decode_offset_y = (360 - h_temp) >> 1;
                            if (jpeg_decode_offset_x < 0) jpeg_decode_offset_x = 0; if (jpeg_decode_offset_y < 0) jpeg_decode_offset_y = 0;
                        }

                        uint16_t w = jpeg.getWidth() >> scaleShift; uint16_t h = jpeg.getHeight() >> scaleShift; uint32_t raw_size = w * h * 2; 

                        if (raw_size <= MAX_PIXEL_BUF_SIZE && w <= 360 && h <= 360) {
                            dsc_idx = (dsc_idx + 1) & 1; jpeg_decode_target = (uint16_t*)jpg_bufs[dsc_idx]; jpeg_decode_width = w; jpeg_decode_height = h; 
                            if (vidFSMode) {
                                if (lvgl_port_lock(portMAX_DELAY)) { jpeg.decode(0, 0, currentStreamScale); lvgl_port_unlock(); }
                            } else {
                                jpeg.decode(0, 0, currentStreamScale); 
                                if (lvgl_port_lock(portMAX_DELAY)) {
                                    lv_image_cache_drop(&cam_img_dsc[dsc_idx]); cam_img_dsc[dsc_idx].header.magic = LV_IMAGE_HEADER_MAGIC; cam_img_dsc[dsc_idx].header.cf = LV_COLOR_FORMAT_RGB565; cam_img_dsc[dsc_idx].header.w = w; cam_img_dsc[dsc_idx].header.h = h; cam_img_dsc[dsc_idx].header.stride = w * 2; cam_img_dsc[dsc_idx].header.flags = 0; cam_img_dsc[dsc_idx].data_size = raw_size; cam_img_dsc[dsc_idx].data = jpg_bufs[dsc_idx]; ViewBaby_SetImage(&cam_img_dsc[dsc_idx]); lvgl_port_unlock();
                                }
                            }
                            static uint32_t frameCount = 0; static uint32_t lastFpsTime = millis(); frameCount++;
                            if (millis() - lastFpsTime >= 1000) { currentFps = frameCount; frameCount = 0; lastFpsTime = millis(); }
                        }
                    }
                }
            }
        } else if (frameSize > MAX_JPEG_DOWNLOAD_SIZE) {
             size_t skipped = 0; uint32_t startSkip = millis();
             while (skipped < frameSize && stream->connected() && isStreamActive && (millis() - startSkip < 3000)) {
                 int avail = stream->available();
                 if (avail > 0) { size_t toSkip = frameSize - skipped; if (toSkip > MAX_JPEG_DOWNLOAD_SIZE) toSkip = MAX_JPEG_DOWNLOAD_SIZE; stream->read(download_buf, toSkip); skipped += toSkip; } 
                 else { if (FAST_MODE) { taskYIELD(); } else { vTaskDelay(1); } }
             }
        }
        
        if (FAST_MODE) { taskYIELD(); } else { vTaskDelay(pdMS_TO_TICKS(60)); }
    }
}

static void videoTask(void * pvParameters) {
    uint8_t* download_buf = (uint8_t*)heap_caps_malloc(MAX_JPEG_DOWNLOAD_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(!download_buf) { download_buf = (uint8_t*)heap_caps_malloc(MAX_JPEG_DOWNLOAD_SIZE, MALLOC_CAP_SPIRAM); }

    jpg_bufs[0] = (uint8_t*)heap_caps_malloc(MAX_PIXEL_BUF_SIZE, MALLOC_CAP_SPIRAM);
    jpg_bufs[1] = (uint8_t*)heap_caps_malloc(MAX_PIXEL_BUF_SIZE, MALLOC_CAP_SPIRAM);

    if (download_buf && jpg_bufs[0] && jpg_bufs[1]) {
        HTTPClient http; 
        http.setReuse(true); // ORIGINAL: Keine abgebrochenen Sockets mehr!
        http.setTimeout(3000); 

        while(isStreamActive) {
            if (WiFi.status() != WL_CONNECTED) { setUiStatus("WLAN fehlt... Retry"); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
            
            String url = camEntity;
            if (!url.startsWith("http")) { if (!url.startsWith("/")) url = "/" + url; url = "http://" + haIP + ":" + String(haPort) + url; }

            if (camHackMode > 0) {
                setUiStatus("Init Aufloesung...");
                int protoEnd = url.indexOf("://");
                if (protoEnd != -1) {
                    int ipStart = protoEnd + 3; int ipEnd = url.indexOf(':', ipStart); if (ipEnd == -1) ipEnd = url.indexOf('/', ipStart);  
                    if (ipEnd != -1) {
                        String ipStr = url.substring(ipStart, ipEnd);        
                        HTTPClient httpHack; httpHack.setTimeout(2000); httpHack.begin("http://" + ipStr + ":8080/api/v1/camera/change-resolution");
                        httpHack.addHeader("accept", "application/json"); httpHack.addHeader("content-type", "application/x-www-form-urlencoded");
                        String payload = "";
                        if (camHackMode == 1) { payload = "{width:320,height:240}"; } else if (camHackMode == 3) { payload = "{width:640,height:360}"; } else if (camHackMode == 8) { payload = "{width:1280,height:720}"; }
                        httpHack.POST(payload); httpHack.end(); vTaskDelay(pdMS_TO_TICKS(500)); 
                    }
                }
            }
            
            if(!isStreamActive) break; 
            
            setUiStatus("Verbinde Stream...");
            http.begin(url); int httpCode = http.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                WiFiClient * stream = http.getStreamPtr();
                stream->setNoDelay(true); 
                setUiStatus("Stream laeuft!");

                while (isStreamActive && stream->connected()) {
                    bool runFast = !screenshotModeActive;
                    if (runFast) { processStream<true>(stream, download_buf); } 
                    else { processStream<false>(stream, download_buf); }
                }
            } else {
                char errBuf[256]; snprintf(errBuf, sizeof(errBuf), "HTTP %d Retry", httpCode); setUiStatus(errBuf);
            }
            http.end(); 
            if(isStreamActive) vTaskDelay(pdMS_TO_TICKS(1000)); 
        }
    } else {
        setUiStatus("Kritischer RAM Fehler!"); 
        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }

    if (download_buf) heap_caps_free(download_buf);
    if (jpg_bufs[0]) heap_caps_free(jpg_bufs[0]);
    if (jpg_bufs[1]) heap_caps_free(jpg_bufs[1]);
    jpg_bufs[0] = nullptr;
    jpg_bufs[1] = nullptr;
    
    videoTaskHandle = NULL;
    vTaskDelete(NULL); 
}

void VideoLogic_Start() {
    if (videoTaskHandle == NULL && isStreamActive) {
        xTaskCreatePinnedToCore(videoTask, "VideoTask", 8192, NULL, 5, &videoTaskHandle, 1); 
    }
}

void VideoLogic_Stop() {
    isStreamActive = false; 
    // ORIGINAL: Kein erzwungenes Warten. Das verhinderte das saubere Neustarten!
}