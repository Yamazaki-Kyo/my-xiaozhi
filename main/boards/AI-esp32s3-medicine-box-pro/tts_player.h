#ifndef CHINESE_TTS_PLAYER_H
#define CHINESE_TTS_PLAYER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_ae_rate_cvt.h>
#include <esp_tts.h>
// xiaole 语音模板，音频数据通过 .dat 文件嵌入 flash 后运行时加载
extern "C" {
extern const esp_tts_voice_t esp_tts_voice_xiaole;
// 嵌入的 .dat 文件数据 (由 CMake objcopy 生成)
extern const uint8_t tts_voice_data_xiaole_start[] asm("_binary_esp_tts_voice_data_xiaole_dat_start");
extern const uint8_t tts_voice_data_xiaole_end[] asm("_binary_esp_tts_voice_data_xiaole_dat_end");
}
#include <vector>
#include <string>
#include <mutex>

#include "application.h"

#define TAG_TTS "TtsPlayer"

#define RATE_CVT_CFG(_src_rate, _dest_rate, _channel)        \
    (esp_ae_rate_cvt_cfg_t)                                  \
    {                                                        \
        .src_rate        = (uint32_t)(_src_rate),            \
        .dest_rate       = (uint32_t)(_dest_rate),           \
        .channel         = (uint8_t)(_channel),              \
        .bits_per_sample = ESP_AUDIO_BIT16,                  \
        .complexity      = 2,                                \
        .perf_type       = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,  \
    }

class ChineseTtsPlayer {
public:
    static ChineseTtsPlayer& GetInstance() {
        static ChineseTtsPlayer instance;
        return instance;
    }

    void Speak(const std::string& text) {
        if (!initialized_) {
            ESP_LOGW(TAG_TTS, "TTS 未初始化，跳过播报");
            return;
        }
        auto* msg = new std::string(text);
        xQueueSend(cmd_queue_, &msg, portMAX_DELAY);
    }

private:
    bool initialized_ = false;
    esp_tts_voice_t* voice_ = nullptr;
    esp_tts_handle_t tts_ = nullptr;
    esp_ae_rate_cvt_handle_t resampler_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    QueueHandle_t cmd_queue_ = nullptr;

    ChineseTtsPlayer() {
        cmd_queue_ = xQueueCreate(4, sizeof(std::string*));
        if (!cmd_queue_) {
            ESP_LOGE(TAG_TTS, "创建命令队列失败");
            return;
        }

        voice_ = esp_tts_voice_set_init(&esp_tts_voice_xiaole, (void*)tts_voice_data_xiaole_start);
        if (!voice_) {
            ESP_LOGE(TAG_TTS, "初始化语音集失败");
            return;
        }

        tts_ = esp_tts_create(voice_);
        if (!tts_) {
            ESP_LOGE(TAG_TTS, "创建 TTS 句柄失败");
            esp_tts_voice_set_free(voice_);
            voice_ = nullptr;
            return;
        }

        // 暂时禁用 TTS 重采样器，定位堆损坏原因
        // esp_ae_rate_cvt_cfg_t resampler_cfg = RATE_CVT_CFG(16000, 24000, ESP_AUDIO_MONO);
        // esp_ae_rate_cvt_open(&resampler_cfg, &resampler_);
        // if (!resampler_) {
        //     ESP_LOGW(TAG_TTS, "重采样器创建失败，TTS 可能无声");
        // }
        ESP_LOGW(TAG_TTS, "重采样器已禁用，TTS 将以 16kHz 输出");

        xTaskCreate(TtsTaskEntry, "tts_player", 16384, this, 10, &task_handle_);
        initialized_ = true;
        ESP_LOGI(TAG_TTS, "中文 TTS 播放器已就绪");
    }

    ~ChineseTtsPlayer() {
        initialized_ = false;
        if (task_handle_) {
            std::string* stop = nullptr;
            xQueueSend(cmd_queue_, &stop, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (resampler_) esp_ae_rate_cvt_close(resampler_);
        if (tts_) esp_tts_destroy(tts_);
        if (voice_) esp_tts_voice_set_free(voice_);
        if (cmd_queue_) vQueueDelete(cmd_queue_);
    }

    static void TtsTaskEntry(void* arg) {
        static_cast<ChineseTtsPlayer*>(arg)->TtsTask();
    }

    void TtsTask() {
        std::string* msg;
        while (xQueueReceive(cmd_queue_, &msg, portMAX_DELAY) == pdTRUE) {
            if (!msg) break; // stop signal
            std::string text = std::move(*msg);
            delete msg;
            SynthesizeAndPlay(text);
        }
        vTaskDelete(NULL);
    }

    void SynthesizeAndPlay(const std::string& text) {
        esp_tts_stream_reset(tts_);
        if (!esp_tts_parse_chinese(tts_, text.c_str())) {
            ESP_LOGE(TAG_TTS, "TTS 文本解析失败: %s", text.c_str());
            return;
        }

        ESP_LOGI(TAG_TTS, "TTS 播报: %s", text.c_str());
        auto& audio = Application::GetInstance().GetAudioService();
        int len;
        short* data;
        const int speed = 3;

        while ((data = esp_tts_stream_play(tts_, &len, speed)) != nullptr && len > 0) {
            std::vector<int16_t> pcm(data, data + len);

            if (resampler_) {
                uint32_t target_size = 0;
                esp_ae_rate_cvt_get_max_out_sample_num(resampler_, pcm.size(), &target_size);
                if (target_size == 0) {
                    audio.PushPcmToPlaybackQueue(std::move(pcm));
                    continue;
                }
                std::vector<int16_t> resampled(target_size);
                uint32_t actual_output = target_size;
                esp_err_t ret = esp_ae_rate_cvt_process(resampler_,
                    (esp_ae_sample_t)pcm.data(), pcm.size(),
                    (esp_ae_sample_t)resampled.data(), &actual_output);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG_TTS, "重采样失败: %d", ret);
                    audio.PushPcmToPlaybackQueue(std::move(pcm));
                    continue;
                }
                if (actual_output < target_size) {
                    resampled.resize(actual_output);
                }
                pcm = std::move(resampled);
            }

            audio.PushPcmToPlaybackQueue(std::move(pcm));
        }
    }
};

#endif
