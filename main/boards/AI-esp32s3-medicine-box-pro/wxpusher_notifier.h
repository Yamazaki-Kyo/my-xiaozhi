#ifndef WXPUSHER_NOTIFIER_H
#define WXPUSHER_NOTIFIER_H

#include <string>
#include <cstdlib>
#include <cctype>
#include "esp_http_client.h"
#include "esp_log.h"

#define TAG_WXP "WxPusher"

#define WXPUSHER_APP_TOKEN "AT_L5k2LTux7tEFbz62YFcy38ebqPWpyJVO"
#define WXPUSHER_UID "UID_6n0C3cRBd8qdzPVyFMROFiPurm2L"

class WxPusherNotifier {
public:
    WxPusherNotifier() {}

    bool SendText(const char* summary, const char* text,
                  const char* uid = WXPUSHER_UID) {
        return DoGet(1, summary, text, uid);
    }

    bool SendMarkdown(const char* summary, const char* markdown,
                      const char* uid = WXPUSHER_UID) {
        return DoGet(3, summary, markdown, uid);
    }

private:
    bool DoGet(int content_type, const char* summary, const char* content,
               const char* uid) {
        std::string enc_summary = UrlEncode(summary);
        std::string enc_content = UrlEncode(content);

        // 堆分配 URL，避免长内容撑爆栈
        size_t url_size = enc_summary.size() + enc_content.size() + 1024;
        char* url = (char*)malloc(url_size);
        if (!url) {
            ESP_LOGE(TAG_WXP, "OOM for URL");
            return false;
        }

        snprintf(url, url_size,
            "http://wxpusher.zjiecode.com/api/send/message"
            "?appToken=%s"
            "&uid=%s"
            "&contentType=%d"
            "&summary=%s"
            "&content=%s",
            WXPUSHER_APP_TOKEN, uid, content_type,
            enc_summary.c_str(), enc_content.c_str());

        ESP_LOGI(TAG_WXP, "Sending to WxPusher (len=%u)...", (unsigned int)strlen(url));

        esp_http_client_config_t config = {};
        config.url = url;
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = 5000;
        config.buffer_size = 2048;
        config.buffer_size_tx = 2048;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        bool success = false;
        if (client) {
            esp_err_t err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            if (status == 301 || status == 302 || status == 307 || status == 308) {
                ESP_LOGW(TAG_WXP, "Redirect to HTTPS, trying POST...");
            }
            success = (err == ESP_OK && status == 200);
            ESP_LOGI(TAG_WXP, "WxPusher: err=%s HTTP=%d",
                     err == ESP_OK ? "OK" : esp_err_to_name(err), status);
            esp_http_client_cleanup(client);
        } else {
            ESP_LOGE(TAG_WXP, "Failed to init HTTP client");
        }

        free(url);
        return success;
    }

    static std::string UrlEncode(const std::string& src) {
        std::string dst;
        dst.reserve(src.size() * 3);
        char hex[4];
        for (unsigned char c : src) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                dst += c;
            } else {
                snprintf(hex, sizeof(hex), "%%%02X", c);
                dst += hex;
            }
        }
        return dst;
    }
};

#endif
