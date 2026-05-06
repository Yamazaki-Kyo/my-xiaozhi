#include "max30102.h"
#include "config.h"

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MAX30102"

MAX30102::MAX30102()
    : i2c_bus_(nullptr)
    , i2c_dev_(nullptr)
{
    memset(&last_result_, 0, sizeof(last_result_));
    last_result_.heart_rate = -1;
    last_result_.spo2 = -1;
}

MAX30102::~MAX30102() {
    if (i2c_dev_ != nullptr) {
        i2c_master_bus_rm_device(i2c_dev_);
        i2c_dev_ = nullptr;
    }
    if (i2c_bus_ != nullptr) {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }
}

bool MAX30102::Initialize() {
    ESP_LOGI(TAG, "初始化 MAX30102...");

    // 创建 I2C 总线
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = MAX30102_I2C_SDA_PIN;
    bus_cfg.scl_io_num = MAX30102_I2C_SCL_PIN;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = 1;

    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线创建失败: %s", esp_err_to_name(ret));
        return false;
    }
    i2c_bus_ = bus_handle;

    // 添加 I2C 设备
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = MAX30102_I2C_ADDR;
    dev_cfg.scl_speed_hz = 400000;

    ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
        return false;
    }

    // 检查 PART_ID (应为 0x15)
    uint8_t part_id = ReadReg(MAX30102_REG_PART_ID);
    ESP_LOGI(TAG, "MAX30102 PART_ID: 0x%02X (期望 0x15)", part_id);
    if (part_id != 0x15) {
        ESP_LOGW(TAG, "PART_ID 不匹配，传感器可能不存在或已损坏");
    }

    // 复位传感器
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x40);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 配置 FIFO: 采样平均=4, FIFO满后覆盖, FIFO阈值=17
    WriteReg(MAX30102_REG_FIFO_CONFIG, 0x4F);

    // 禁用中断
    WriteReg(MAX30102_REG_INT_ENABLE_1, 0x00);
    WriteReg(MAX30102_REG_INT_ENABLE_2, 0x00);

    // SpO2 配置: 采样率=100Hz, LED脉宽=411us, ADC范围=16384
    WriteReg(MAX30102_REG_SPO2_CONFIG, 0x27);

    // LED 脉冲电流: RED=0x24 (约6.4mA), IR=0x24 (约6.4mA)
    WriteReg(MAX30102_REG_LED1_PA, 0x24);
    WriteReg(MAX30102_REG_LED2_PA, 0x24);

    // 进入 SpO2 模式
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x03);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "MAX30102 初始化完成");
    return true;
}

bool MAX30102::IsDeviceOnline() {
    uint8_t part_id = ReadReg(MAX30102_REG_PART_ID);
    return (part_id == 0x15);
}

void MAX30102::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    i2c_master_transmit(i2c_dev_, buf, 2, 100);
}

uint8_t MAX30102::ReadReg(uint8_t reg) {
    uint8_t value = 0;
    i2c_master_transmit_receive(i2c_dev_, &reg, 1, &value, 1, 100);
    return value;
}

void MAX30102::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    i2c_master_transmit_receive(i2c_dev_, &reg, 1, buffer, length, 100);
}

void MAX30102::ClearFifo() {
    uint8_t rd = ReadReg(MAX30102_REG_FIFO_RD_PTR);
    uint8_t wr = ReadReg(MAX30102_REG_FIFO_WR_PTR);
    WriteReg(MAX30102_REG_FIFO_RD_PTR, wr);
}

bool MAX30102::IsFingerDetected() {
    // 读取几组数据，检查IR信号强度是否超过阈值
    const int check_samples = 5;
    uint32_t max_ir = 0;
    for (int i = 0; i < check_samples; i++) {
        uint32_t red, ir;
        if (ReadFifo(red, ir)) {
            if (ir > max_ir) max_ir = ir;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    // IR 信号强度阈值：一般情况下有手指时 IR > 5000
    return max_ir > 5000;
}

bool MAX30102::ReadFifo(uint32_t& red, uint32_t& ir) {
    uint8_t samples = (ReadReg(MAX30102_REG_FIFO_WR_PTR) - ReadReg(MAX30102_REG_FIFO_RD_PTR)) & 0x1F;
    if (samples < 1) return false;

    uint8_t buf[6];
    ReadRegs(MAX30102_REG_FIFO_DATA, buf, 6);

    // 3字节 RED + 3字节 IR, 每字节高2位无效
    red = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    ir  = ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5];
    red &= 0x03FFFF;
    ir  &= 0x03FFFF;

    return true;
}

// ============ PPG 信号处理 ============

void MAX30102::RemoveDcComponent(PpgData& data) {
    // 简易高通滤波：减去滑动平均值
    if (data.sample_count < 4) return;

    std::vector<float> red_float(data.sample_count);
    std::vector<float> ir_float(data.sample_count);

    // 计算DC分量 (均值)
    float red_mean = 0, ir_mean = 0;
    for (int i = 0; i < data.sample_count; i++) {
        red_float[i] = (float)data.red_samples[i];
        ir_float[i]  = (float)data.ir_samples[i];
        red_mean += red_float[i];
        ir_mean  += ir_float[i];
    }
    red_mean /= data.sample_count;
    ir_mean  /= data.sample_count;

    // 去除DC
    for (int i = 0; i < data.sample_count; i++) {
        red_float[i] -= red_mean;
        ir_float[i]  -= ir_mean;
        data.red_samples[i] = (uint32_t)std::abs(red_float[i]);
        data.ir_samples[i]  = (uint32_t)std::abs(ir_float[i]);
    }
}

void MAX30102::MovingAverageFilter(PpgData& data, int window_size) {
    if (data.sample_count < window_size) return;

    std::vector<uint32_t> red_filtered(data.sample_count);
    std::vector<uint32_t> ir_filtered(data.sample_count);

    for (int i = window_size / 2; i < data.sample_count - window_size / 2; i++) {
        uint64_t red_sum = 0, ir_sum = 0;
        for (int j = -window_size / 2; j <= window_size / 2; j++) {
            red_sum += data.red_samples[i + j];
            ir_sum  += data.ir_samples[i + j];
        }
        red_filtered[i] = (uint32_t)(red_sum / window_size);
        ir_filtered[i]  = (uint32_t)(ir_sum / window_size);
    }

    memcpy(data.red_samples, red_filtered.data(), data.sample_count * sizeof(uint32_t));
    memcpy(data.ir_samples, ir_filtered.data(), data.sample_count * sizeof(uint32_t));
}

int MAX30102::CalculateHeartRate(const PpgData& data) {
    // 基于峰值检测的心率计算
    if (data.sample_count < 10) return -1;

    const int min_peak_distance = (data.sample_rate * 60) / 200;  // 最高200bpm
    int peak_count = 0;
    std::vector<int> peak_intervals;

    uint32_t threshold = 0;
    for (int i = 0; i < data.sample_count; i++) {
        if (data.ir_samples[i] > threshold) {
            threshold = data.ir_samples[i];
        }
    }
    threshold = threshold * 4 / 10;  // 峰值的 40% 作为检测阈值

    int last_peak = -min_peak_distance;
    for (int i = 1; i < data.sample_count - 1; i++) {
        // 峰值检测: 当前值大于左右邻居且超过阈值
        if (data.ir_samples[i] > threshold &&
            data.ir_samples[i] > data.ir_samples[i - 1] &&
            data.ir_samples[i] > data.ir_samples[i + 1]) {

            if (i - last_peak >= min_peak_distance) {
                if (last_peak > 0) {
                    peak_intervals.push_back(i - last_peak);
                }
                last_peak = i;
                peak_count++;
            }
        }
    }

    if (peak_count < 2) return -1;

    // 计算平均峰间隔
    float avg_interval = 0;
    for (size_t i = 0; i < peak_intervals.size(); i++) {
        avg_interval += peak_intervals[i];
    }
    avg_interval /= peak_intervals.size();

    // 心率 = 60 * 采样率 / 平均峰间隔
    int heart_rate = (int)(60.0f * data.sample_rate / avg_interval + 0.5f);

    // 合理范围检查
    if (heart_rate < 40 || heart_rate > 200) return -1;

    return heart_rate;
}

int MAX30102::CalculateSpO2(const PpgData& data) {
    // 基于 AC/DC 比值的血氧计算
    if (data.sample_count < 10) return -1;

    // 计算各通道的 DC 分量 (均值)
    float red_dc = 0, ir_dc = 0;
    for (int i = 0; i < data.sample_count; i++) {
        red_dc += (float)data.red_samples[i];
        ir_dc  += (float)data.ir_samples[i];
    }
    red_dc /= data.sample_count;
    ir_dc  /= data.sample_count;

    // 计算各通道的 AC 分量 (RMS)
    float red_ac = 0, ir_ac = 0;
    for (int i = 0; i < data.sample_count; i++) {
        float red_diff = (float)data.red_samples[i] - red_dc;
        float ir_diff  = (float)data.ir_samples[i]  - ir_dc;
        red_ac += red_diff * red_diff;
        ir_ac  += ir_diff * ir_diff;
    }
    red_ac = std::sqrt(red_ac / data.sample_count);
    ir_ac  = std::sqrt(ir_ac  / data.sample_count);

    if (ir_dc < 1.0f || red_dc < 1.0f) return -1;

    // R = (AC_red / DC_red) / (AC_ir / DC_ir)
    float R = (red_ac / red_dc) / (ir_ac / ir_dc);

    // 经验公式: SpO2 = 104 - 17 * R (典型校准系数)
    int spo2 = (int)(104.0f - 17.0f * R + 0.5f);

    if (spo2 < 70 || spo2 > 100) return -1;

    return spo2;
}

float MAX30102::EvaluateSignalQuality(const PpgData& data) {
    if (data.sample_count < 10) return 0.0f;

    // 基于PPG波形周期性评估信号质量
    float ir_mean = 0;
    for (int i = 0; i < data.sample_count; i++) {
        ir_mean += (float)data.ir_samples[i];
    }
    ir_mean /= data.sample_count;

    if (ir_mean < 1000.0f) return 0.0f;  // 信号太弱

    // 计算变异系数作为质量指标
    float variance = 0;
    for (int i = 0; i < data.sample_count; i++) {
        float diff = (float)data.ir_samples[i] - ir_mean;
        variance += diff * diff;
    }
    variance /= data.sample_count;
    float cv = std::sqrt(variance) / ir_mean;  // 变异系数

    // 理想的PPG信号应有适中的波动 (cv在0.05-0.3之间)
    if (cv < 0.02f) return 0.3f;   // 几乎无波动
    if (cv < 0.05f) return 0.5f;
    if (cv < 0.15f) return 0.8f;
    if (cv < 0.30f) return 0.6f;
    return 0.3f;
}

// ============ 完整检测流程 ============

HealthResult MAX30102::PerformMeasurement(int duration_sec) {
    HealthResult result;
    result.heart_rate = -1;
    result.spo2 = -1;
    result.signal_quality = 0.0f;
    result.finger_detected = false;

    const int sample_rate = 100;  // 100Hz
    const int total_samples = duration_sec * sample_rate;
    const int max_samples = 2000;

    int actual_samples = (total_samples < max_samples) ? total_samples : max_samples;

    std::vector<uint32_t> red_buf(actual_samples);
    std::vector<uint32_t> ir_buf(actual_samples);
    int sample_idx = 0;

    ESP_LOGI(TAG, "开始采集 PPG 数据 (%d秒, ~%d采样点)...", duration_sec, actual_samples);

    ClearFifo();

    int64_t start_time = esp_timer_get_time() / 1000;  // ms

    while (sample_idx < actual_samples) {
        uint32_t red, ir;
        if (ReadFifo(red, ir)) {
            red_buf[sample_idx] = red;
            ir_buf[sample_idx]  = ir;
            sample_idx++;
        }
        vTaskDelay(pdMS_TO_TICKS(8));  // ~100Hz 采样率

        // 超时保护
        if ((esp_timer_get_time() / 1000 - start_time) > (duration_sec + 5) * 1000) {
            ESP_LOGW(TAG, "采样超时，已收集 %d 个采样点", sample_idx);
            break;
        }
    }

    ESP_LOGI(TAG, "采集完成，共 %d 个采样点", sample_idx);

    if (sample_idx < 50) {
        ESP_LOGW(TAG, "采样点不足，无法计算");
        return result;
    }

    // 判断手指是否在位
    uint64_t ir_sum = 0;
    for (int i = 0; i < sample_idx; i++) {
        ir_sum += ir_buf[i];
    }
    float ir_avg = (float)(ir_sum / sample_idx);
    result.finger_detected = (ir_avg > 5000.0f);

    if (!result.finger_detected) {
        ESP_LOGW(TAG, "未检测到手指");
        return result;
    }

    // PPG信号处理
    PpgData ppg;
    ppg.red_samples = red_buf.data();
    ppg.ir_samples  = ir_buf.data();
    ppg.sample_count = sample_idx;
    ppg.sample_rate  = sample_rate;

    // 去直流 + 平滑滤波
    RemoveDcComponent(ppg);
    MovingAverageFilter(ppg, 4);

    // 信号质量评估
    result.signal_quality = EvaluateSignalQuality(ppg);

    if (result.signal_quality < 0.3f) {
        ESP_LOGW(TAG, "信号质量差 (%.2f)，手指可能移动", (double)result.signal_quality);
    }

    // 计算心率和血氧
    result.heart_rate = CalculateHeartRate(ppg);
    result.spo2 = CalculateSpO2(ppg);

    ESP_LOGI(TAG, "检测结果: HR=%d bpm, SpO2=%d%%, 质量=%.2f, 手指=%s",
             result.heart_rate, result.spo2,
             (double)result.signal_quality,
             result.finger_detected ? "是" : "否");

    last_result_ = result;
    return result;
}
