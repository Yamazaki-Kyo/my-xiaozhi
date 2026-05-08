#ifndef CONVERSATION_LOGGER_H
#define CONVERSATION_LOGGER_H

#include <string>
#include <vector>
#include <mutex>
#include <ctime>

struct ConversationEntry {
    std::string role;       // "user" / "assistant"
    std::string content;
    time_t timestamp;
};

class ConversationLogger {
public:
    static ConversationLogger& GetInstance() {
        static ConversationLogger instance;
        return instance;
    }

    void Append(const std::string& role, const std::string& content) {
        // 过滤 MCP 工具调用（以 % 开头的 assistant 消息）
        if (role == "assistant" && !content.empty() && content[0] == '%') {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        history_.push_back({role, content, time(nullptr)});
        if (history_.size() > 200) {
            history_.erase(history_.begin());
        }
    }

    // Markdown 格式，用于 WxPusher content (contentType=3)
    std::string ToMarkdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (history_.empty()) return "暂无对话记录";

        std::string md;
        for (size_t i = 0; i < history_.size(); i++) {
            char time_buf[16];
            strftime(time_buf, sizeof(time_buf), "%H:%M",
                     localtime(&history_[i].timestamp));
            md += "**[";
            md += time_buf;
            md += "]** ";
            md += (history_[i].role == "user") ? "老人" : "小智";
            md += "：";
            md += history_[i].content;
            md += "  \n";
        }
        return md;
    }

    // 摘要，≤100字符，用于 WxPusher summary 字段
    std::string ToSummary() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (history_.empty()) return "暂无对话";

        std::string summary;
        int start = std::max(0, (int)history_.size() - 3);
        for (size_t i = start; i < history_.size(); i++) {
            if (!summary.empty()) summary += " | ";
            summary += (history_[i].role == "user" ? "老人: " : "小智: ");
            summary += history_[i].content;
        }
        if (summary.length() > 100) {
            summary = summary.substr(0, 97) + "...";
        }
        return summary;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        history_.clear();
    }

private:
    ConversationLogger() = default;
    std::mutex mutex_;
    std::vector<ConversationEntry> history_;
};

#endif
