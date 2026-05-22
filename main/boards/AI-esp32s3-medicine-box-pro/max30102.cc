#include "max30102.h"
#include "config.h"

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MAX30102"

// ===================================================================
// 移植自参考库 "辰哥单片机设计" 的 Hamming 窗系数和 SpO2 查找表
// ===================================================================

// Hamming 窗权重 (5 点, 放大 512 倍): w[n] = 0.54 - 0.46*cos(2*pi*n/(N-1))
static const int32_t auw_hamm[PPG_HAMMING_SIZE] = {41, 276, 512, 276, 41};
// sum(auw_hamm) = 1146

// SpO2 查找表: index = R * 20 (R = (AC_red/DC_red) / (AC_ir/DC_ir))
// 由公式预计算: SpO2 = -45.060*R² + 30.354*R + 94.845
static const uint8_t uch_spo2_table[184] = {
    95, 95, 95, 96, 96, 96, 97, 97, 97, 97, 97, 98, 98, 98, 98, 98,
    99, 99, 99, 99, 99, 99, 99, 99,100,100,100,100,100,100,100,100,
   100,100,100,100,100,100,100,100,100,100,100,100, 99, 99, 99, 99,
    99, 99, 99, 99, 98, 98, 98, 98, 98, 98, 97, 97, 97, 97, 96, 96,
    96, 96, 95, 95, 95, 94, 94, 94, 93, 93, 93, 92, 92, 92, 91, 91,
    90, 90, 89, 89, 89, 88, 88, 87, 87, 86, 86, 85, 85, 84, 84, 83,
    82, 82, 81, 81, 80, 80, 79, 78, 78, 77, 76, 76, 75, 74, 74, 73,
    72, 72, 71, 70, 69, 69, 68, 67, 66, 66, 65, 64, 63, 62, 62, 61,
    60, 59, 58, 57, 56, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46,
    45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 31, 30, 29,
    28, 27, 26, 25, 23, 22, 21, 20, 19, 17, 16, 15, 14, 12, 11, 10,
    9,  7,  6,  5,  3,  2,  1
};

// ===================================================================
// 构造函数 / 析构函数 / I2C 基础操作 (保持不变)
// ===================================================================

MAX30102::MAX30102()
    : i2c_bus_(nullptr)
    , i2c_dev_(nullptr)
{
    memset(&last_result_, 0, sizeof(last_result_));
    last_result_.heart_rate = -1;
    last_result_.spo2 = -1;

    // 预分配算法缓冲区
    an_x_.resize(PPG_BUFFER_SIZE);
    an_y_.resize(PPG_BUFFER_SIZE);
    an_dx_.resize(PPG_BUFFER_SIZE);
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

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = MAX30102_I2C_ADDR;
    dev_cfg.scl_speed_hz = 100000;

    ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
        return false;
    }

    uint8_t part_id = ReadReg(MAX30102_REG_PART_ID);
    ESP_LOGI(TAG, "MAX30102 PART_ID: 0x%02X (期望 0x15)", part_id);
    if (part_id != 0x15) {
        ESP_LOGW(TAG, "PART_ID 不匹配，传感器可能不存在或已损坏");
    }

    // 逐行精确对齐参考库 MAX30102_Init() + MAX30102_Reset()
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x40);  // Reset (参考库写两次, 无延迟)
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x40);

    WriteReg(MAX30102_REG_INT_ENABLE_1, 0xc0);  // A_FULL_EN + PPG_RDY_EN
    WriteReg(MAX30102_REG_INT_ENABLE_2, 0x00);
    WriteReg(MAX30102_REG_FIFO_WR_PTR, 0x00);
    WriteReg(MAX30102_REG_OVF_COUNTER, 0x00);
    WriteReg(MAX30102_REG_FIFO_RD_PTR, 0x00);
    WriteReg(MAX30102_REG_FIFO_CONFIG, 0x0f);
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x03);   // SpO2 mode
    WriteReg(MAX30102_REG_SPO2_CONFIG, 0x27);   // 100Hz, 411μs
    WriteReg(MAX30102_REG_LED1_PA, 0x24);       // ~7mA RED
    WriteReg(MAX30102_REG_LED2_PA, 0x24);       // ~7mA IR
    WriteReg(MAX30102_REG_PILOT_PA, 0x7f);      // PILOT LED

    ESP_LOGI(TAG, "MAX30102 初始化完成 (SpO2 模式运行中)");
    return true;
}

bool MAX30102::IsDeviceOnline() {
    uint8_t part_id = ReadReg(MAX30102_REG_PART_ID);
    return (part_id == 0x15);
}

void MAX30102::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(i2c_dev_, buf, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 写寄存器 0x%02X=0x%02X 失败: %s", reg, value, esp_err_to_name(ret));
    }
}

uint8_t MAX30102::ReadReg(uint8_t reg) {
    uint8_t value = 0;
    esp_err_t ret = i2c_master_transmit_receive(i2c_dev_, &reg, 1, &value, 1, 100);
    if (ret != ESP_OK) {
        static int err_cnt = 0;
        if (++err_cnt <= 5) {
            ESP_LOGW(TAG, "I2C 读寄存器 0x%02X 失败: %s", reg, esp_err_to_name(ret));
        } else if (err_cnt == 100) {
            err_cnt = 6;
        }
    }
    return value;
}

void MAX30102::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    i2c_master_transmit_receive(i2c_dev_, &reg, 1, buffer, length, 100);
}

void MAX30102::ClearFifo() {
    uint8_t rd = ReadReg(MAX30102_REG_FIFO_RD_PTR);
    uint8_t wr = ReadReg(MAX30102_REG_FIFO_WR_PTR);
    (void)rd;
    WriteReg(MAX30102_REG_FIFO_RD_PTR, wr);
}

bool MAX30102::IsFingerDetected() {
    const int check_samples = 5;
    uint32_t max_ir = 0;
    for (int i = 0; i < check_samples; i++) {
        uint32_t red, ir;
        if (ReadFifo(red, ir)) {
            if (ir > max_ir) max_ir = ir;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return max_ir > 5000;
}

bool MAX30102::ReadFifo(uint32_t& red, uint32_t& ir) {
    uint8_t stat1 = ReadReg(MAX30102_REG_INT_STATUS_1);
    ReadReg(MAX30102_REG_INT_STATUS_2);

    uint8_t buf[6];
    ReadRegs(MAX30102_REG_FIFO_DATA, buf, 6);

    red = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    ir  = ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5];
    red &= 0x03FFFF;
    ir  &= 0x03FFFF;

    // 每 5 秒输出诊断
    static int64_t last_diag_ms = 0;
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_diag_ms > 5000) {
        last_diag_ms = now_ms;
        uint8_t wr   = ReadReg(MAX30102_REG_FIFO_WR_PTR);
        uint8_t rd   = ReadReg(MAX30102_REG_FIFO_RD_PTR);
        uint8_t stat = ReadReg(MAX30102_REG_INT_STATUS_1);
        uint8_t ovf  = ReadReg(MAX30102_REG_OVF_COUNTER);
        uint8_t mode = ReadReg(MAX30102_REG_MODE_CONFIG);
        ESP_LOGI(TAG, "FIFO 诊断: WR=%d RD=%d MODE=0x%02X INT_ST=0x%02X OVF=%d PWR_RDY=%d",
                 wr, rd, mode, stat, ovf, (stat >> 4) & 1);
    }

    return (stat1 & 0x40) != 0;
}

// ===================================================================
// 非阻塞测量 API (保持不变)
// ===================================================================

bool MAX30102::StartSampling() {
    if (!IsDeviceOnline()) {
        ESP_LOGE(TAG, "MAX30102 不在线，无法启动采样");
        return false;
    }

    WriteReg(MAX30102_REG_MODE_CONFIG, 0x40);  // Reset
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x40);

    WriteReg(MAX30102_REG_INT_ENABLE_1, 0xc0);
    WriteReg(MAX30102_REG_INT_ENABLE_2, 0x00);
    WriteReg(MAX30102_REG_FIFO_WR_PTR, 0x00);
    WriteReg(MAX30102_REG_OVF_COUNTER, 0x00);
    WriteReg(MAX30102_REG_FIFO_RD_PTR, 0x00);
    WriteReg(MAX30102_REG_FIFO_CONFIG, 0x0f);
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x03);
    WriteReg(MAX30102_REG_SPO2_CONFIG, 0x27);
    WriteReg(MAX30102_REG_LED1_PA, 0x24);
    WriteReg(MAX30102_REG_LED2_PA, 0x24);
    WriteReg(MAX30102_REG_PILOT_PA, 0x7f);

    ReadReg(MAX30102_REG_INT_STATUS_1);
    ReadReg(MAX30102_REG_INT_STATUS_2);
    uint8_t wr = ReadReg(MAX30102_REG_FIFO_WR_PTR);
    WriteReg(MAX30102_REG_FIFO_RD_PTR, wr);

    rolling_red_.resize(rolling_cap_);
    rolling_ir_.resize(rolling_cap_);
    rolling_head_ = 0;
    rolling_count_ = 0;
    rolling_hr_ = -1;
    rolling_spo2_ = -1;

    ESP_LOGI(TAG, "连续采样已启动 (MODE=0x%02X WR=%d)",
             ReadReg(MAX30102_REG_MODE_CONFIG), wr);
    return true;
}

void MAX30102::StopSampling() {
    WriteReg(MAX30102_REG_MODE_CONFIG, 0x00);
    ESP_LOGI(TAG, "采样已停止");
}

bool MAX30102::ReadSample(PpgSample& sample) {
    if (!ReadFifo(sample.red, sample.ir)) return false;

    if (rolling_cap_ > 0) {
        rolling_red_[rolling_head_] = sample.red;
        rolling_ir_[rolling_head_] = sample.ir;
        rolling_head_ = (rolling_head_ + 1) % rolling_cap_;
        if (rolling_count_ < rolling_cap_) rolling_count_++;
    }
    return true;
}

// ===================================================================
// 滚动估值 (使用参考库算法, 取最近 500 样本)
// ===================================================================

void MAX30102::UpdateRollingEstimate() {
    if (rolling_count_ < 50) {
        rolling_hr_ = -1;
        rolling_spo2_ = -1;
        return;
    }

    // 从环形缓冲区提取最近最多 500 样本
    int n_samples = (rolling_count_ < PPG_BUFFER_SIZE) ? rolling_count_ : PPG_BUFFER_SIZE;
    std::vector<uint32_t> ir_buf(n_samples);
    std::vector<uint32_t> red_buf(n_samples);

    int start_idx = (rolling_head_ - n_samples + rolling_cap_) % rolling_cap_;
    for (int i = 0; i < n_samples; i++) {
        int idx = (start_idx + i) % rolling_cap_;
        ir_buf[i]  = (uint32_t)rolling_ir_[idx];
        red_buf[i] = (uint32_t)rolling_red_[idx];
    }

    int32_t spo2_val = -1, hr_val = -1;
    int8_t spo2_valid = 0, hr_valid = 0;

    maxim_heart_rate_and_oxygen_saturation(
        ir_buf.data(), n_samples,
        red_buf.data(),
        &spo2_val, &spo2_valid,
        &hr_val, &hr_valid);

    if (hr_valid)  rolling_hr_   = hr_val;
    if (spo2_valid) rolling_spo2_ = spo2_val;

    ESP_LOGD(TAG, "滚动估值: HR=%d(%s) SpO2=%d(%s) samples=%d",
             hr_val, hr_valid ? "V" : "X",
             spo2_val, spo2_valid ? "V" : "X",
             n_samples);
}

// ===================================================================
// 完整计算结果 (使用参考库算法, 取最后 500 样本)
// ===================================================================

HealthResult MAX30102::ComputeResults(const uint32_t* red, const uint32_t* ir, int count) {
    HealthResult result;
    result.heart_rate = -1;
    result.spo2 = -1;
    result.signal_quality = 0.0f;
    result.finger_detected = false;

    if (count < 50) return result;

    // 手指检测: IR 均值 > 5000
    uint64_t ir_sum = 0;
    for (int i = 0; i < count; i++) ir_sum += ir[i];
    result.finger_detected = ((float)(ir_sum / count) > 5000.0f);
    if (!result.finger_detected) return result;

    // 取最后 PPG_BUFFER_SIZE (500) 样本用于算法
    int n_samples = (count < PPG_BUFFER_SIZE) ? count : PPG_BUFFER_SIZE;
    const uint32_t* red_ptr = red + (count - n_samples);
    const uint32_t* ir_ptr  = ir  + (count - n_samples);

    int32_t spo2_val = -1, hr_val = -1;
    int8_t spo2_valid = 0, hr_valid = 0;

    maxim_heart_rate_and_oxygen_saturation(
        (uint32_t*)ir_ptr, n_samples,
        (uint32_t*)red_ptr,
        &spo2_val, &spo2_valid,
        &hr_val, &hr_valid);

    result.heart_rate = hr_valid ? hr_val : -1;
    result.spo2 = spo2_valid ? spo2_val : -1;

    // 信号质量评估: 基于有效峰值数和 IR 幅度
    if (hr_valid && spo2_valid) {
        result.signal_quality = 0.8f;
    } else if (hr_valid) {
        result.signal_quality = 0.5f;
    } else {
        // 使用 IR 信号幅度评估
        uint32_t ir_min = ir_ptr[0], ir_max = ir_ptr[0];
        for (int i = 1; i < n_samples; i++) {
            if (ir_ptr[i] < ir_min) ir_min = ir_ptr[i];
            if (ir_ptr[i] > ir_max) ir_max = ir_ptr[i];
        }
        float ac = (float)(ir_max - ir_min);
        float dc = (float)(ir_sum / count);
        if (dc > 0 && ac / dc > 0.001f) {
            result.signal_quality = 0.3f;
        }
    }

    ESP_LOGI(TAG, "计算结果: HR=%d bpm(%s), SpO2=%d%%(%s), 质量=%.2f, 手指=%s, samples=%d",
             result.heart_rate, hr_valid ? "V" : "X",
             result.spo2, spo2_valid ? "V" : "X",
             (double)result.signal_quality,
             result.finger_detected ? "是" : "否",
             count);

    last_result_ = result;
    return result;
}

// ===================================================================
// 参考库核心算法: maxim_heart_rate_and_oxygen_saturation
// 移植自 "辰哥单片机设计" STM32 参考库, 逐行对齐
// ===================================================================

void MAX30102::maxim_heart_rate_and_oxygen_saturation(
    uint32_t* pun_ir_buffer, int32_t n_ir_buffer_length,
    uint32_t* pun_red_buffer,
    int32_t* pn_spo2, int8_t* pch_spo2_valid,
    int32_t* pn_heart_rate, int8_t* pch_hr_valid)
{
    uint32_t un_ir_mean;
    int32_t k, n_i_ratio_count;
    int32_t i, s, m, n_exact_ir_valley_locs_count, n_middle_idx;
    int32_t n_th1, n_npks, n_c_min;
    int32_t an_ir_valley_locs[15];
    int32_t an_exact_ir_valley_locs[15];
    int32_t an_dx_peak_locs[15];
    int32_t n_y_ac, n_x_ac;
    int32_t n_spo2_calc;
    int32_t n_y_dc_max, n_x_dc_max;
    int32_t n_y_dc_max_idx = 0, n_x_dc_max_idx = 0;
    int32_t an_ratio[5], n_ratio_average;
    int32_t n_nume, n_denom;

    // 实际处理长度 (不超过缓冲区)
    int32_t n_size = (n_ir_buffer_length < PPG_BUFFER_SIZE) ? n_ir_buffer_length : PPG_BUFFER_SIZE;

    // ---- Step 1: 去除 IR 信号 DC 分量 ----
    un_ir_mean = 0;
    for (k = 0; k < n_size; k++) un_ir_mean += pun_ir_buffer[k];
    un_ir_mean = un_ir_mean / n_size;
    for (k = 0; k < n_size; k++) an_x_[k] = (int32_t)pun_ir_buffer[k] - (int32_t)un_ir_mean;

    // ---- Step 2: 4 点移动平均 ----
    for (k = 0; k < n_size - PPG_MA4_SIZE; k++) {
        n_denom = (an_x_[k] + an_x_[k + 1] + an_x_[k + 2] + an_x_[k + 3]);
        an_x_[k] = n_denom / (int32_t)4;
    }

    // ---- Step 3: 差分信号 an_dx[k] = x[k+1] - x[k] ----
    for (k = 0; k < n_size - PPG_MA4_SIZE - 1; k++)
        an_dx_[k] = (an_x_[k + 1] - an_x_[k]);

    // ---- Step 4: 对差分信号做 2 点移动平均 ----
    for (k = 0; k < n_size - PPG_MA4_SIZE - 2; k++)
        an_dx_[k] = (an_dx_[k] + an_dx_[k + 1]) / 2;

    // ---- Step 5: Hamming 窗滤波 (负号翻转波形, 使谷值变峰值) ----
    int32_t hamm_range = n_size - PPG_HAMMING_SIZE - PPG_MA4_SIZE - 2;
    for (i = 0; i < hamm_range; i++) {
        s = 0;
        for (k = i; k < i + PPG_HAMMING_SIZE; k++)
            s -= an_dx_[k] * auw_hamm[k - i];
        an_dx_[i] = s / (int32_t)1146;  // sum(auw_hamm) = 1146
    }

    // ---- Step 6: 阈值 — 使用最大值的 30% 替代自适应均值，更稳健地排除噪声尖峰 ----
    int32_t n_max = 0;
    for (k = 0; k < n_size - PPG_HAMMING_SIZE; k++)
        if (an_dx_[k] > n_max) n_max = an_dx_[k];
    if (n_max < 50) {  // 信号过弱，无法检测
        *pn_spo2 = -999; *pch_spo2_valid = 0;
        *pn_heart_rate = -999; *pch_hr_valid = 0;
        return;
    }
    n_th1 = n_max / 3 + 1;  // 阈值 = 1/3 最大值

    // ---- Step 7: 峰值检测 (更高阈值 + 更大最小间距) ----
    maxim_find_peaks(an_dx_peak_locs, &n_npks, an_dx_.data(),
                     n_size - PPG_HAMMING_SIZE, n_th1, 35, 6);

    // ---- Step 8: 心率计算 (中位数间隔 + 生理范围过滤) ----
    if (n_npks >= 2) {
        int32_t intervals[15];
        int n_intervals = 0;
        for (k = 1; k < n_npks; k++) {
            int32_t ival = an_dx_peak_locs[k] - an_dx_peak_locs[k - 1];
            // 生理范围: 35~150 样本 (100Hz 下 40~171 bpm)
            if (ival >= 35 && ival <= 150) {
                intervals[n_intervals++] = ival;
            }
        }
        if (n_intervals >= 1) {
            maxim_sort_ascend(intervals, n_intervals);
            int32_t median_ival = intervals[n_intervals / 2];
            *pn_heart_rate = (int32_t)(6000 / median_ival);
            *pch_hr_valid = 1;
        } else {
            *pn_heart_rate = -999;
            *pch_hr_valid = 0;
        }
    } else {
        *pn_heart_rate = -999;
        *pch_hr_valid = 0;
    }

    ESP_LOGI(TAG, "HR检测: 峰值数=%d 阈值=%ld 最大值=%ld",
             (int)n_npks, (long)n_th1, (long)n_max);

    // ---- Step 9: 谷值位置 = 峰值位置 + HAMMING_SIZE/2 ----
    for (k = 0; k < n_npks; k++)
        an_ir_valley_locs[k] = an_dx_peak_locs[k] + PPG_HAMMING_SIZE / 2;

    // ---- Step 10: 加载原始 IR 和 RED 数据 ----
    for (k = 0; k < n_size; k++) {
        an_x_[k] = (int32_t)pun_ir_buffer[k];
        an_y_[k] = (int32_t)pun_red_buffer[k];
    }

    // ---- Step 11: 精确定位谷值 (在 ±5 范围内搜索最小值) ----
    n_exact_ir_valley_locs_count = 0;
    for (k = 0; k < n_npks; k++) {
        uint32_t un_only_once = 1;
        m = an_ir_valley_locs[k];
        n_c_min = 16777216;  // 2^24
        if (m + 5 < n_size - PPG_HAMMING_SIZE && m - 5 > 0) {
            for (i = m - 5; i < m + 5; i++) {
                if (an_x_[i] < n_c_min) {
                    if (un_only_once > 0) un_only_once = 0;
                    n_c_min = an_x_[i];
                    an_exact_ir_valley_locs[k] = i;
                }
            }
            if (un_only_once == 0)
                n_exact_ir_valley_locs_count++;
        }
    }
    if (n_exact_ir_valley_locs_count < 2) {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
        return;
    }

    // ---- Step 12: 4 点 MA 平滑原始信号 ----
    for (k = 0; k < n_size - PPG_MA4_SIZE; k++) {
        an_x_[k] = (an_x_[k] + an_x_[k + 1] + an_x_[k + 2] + an_x_[k + 3]) / (int32_t)4;
        an_y_[k] = (an_y_[k] + an_y_[k + 1] + an_y_[k + 2] + an_y_[k + 3]) / (int32_t)4;
    }

    // ---- Step 13: 谷值越界检查 ----
    for (k = 0; k < n_exact_ir_valley_locs_count; k++) {
        if (an_exact_ir_valley_locs[k] >= n_size) {
            *pn_spo2 = -999;
            *pch_spo2_valid = 0;
            return;
        }
    }

    // ---- Step 14: 逐脉冲周期计算 SpO2 比值 ----
    n_ratio_average = 0;
    n_i_ratio_count = 0;
    for (k = 0; k < 5; k++) an_ratio[k] = 0;

    for (k = 0; k < n_exact_ir_valley_locs_count - 1; k++) {
        n_y_dc_max = -16777216;
        n_x_dc_max = -16777216;
        if (an_exact_ir_valley_locs[k + 1] - an_exact_ir_valley_locs[k] > 10) {
            // 在两个谷值之间找 DC 最大值 (峰值)
            for (i = an_exact_ir_valley_locs[k]; i < an_exact_ir_valley_locs[k + 1]; i++) {
                if (an_x_[i] > n_x_dc_max) { n_x_dc_max = an_x_[i]; n_x_dc_max_idx = i; }
                if (an_y_[i] > n_y_dc_max) { n_y_dc_max = an_y_[i]; n_y_dc_max_idx = i; }
            }
            // RED AC: 线性插值 DC 分量, 从峰值减去
            n_y_ac = (an_y_[an_exact_ir_valley_locs[k + 1]] - an_y_[an_exact_ir_valley_locs[k]])
                   * (n_y_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y_[an_exact_ir_valley_locs[k]]
                   + n_y_ac / (an_exact_ir_valley_locs[k + 1] - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y_[n_y_dc_max_idx] - n_y_ac;

            // IR AC: 同上
            n_x_ac = (an_x_[an_exact_ir_valley_locs[k + 1]] - an_x_[an_exact_ir_valley_locs[k]])
                   * (n_x_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x_[an_exact_ir_valley_locs[k]]
                   + n_x_ac / (an_exact_ir_valley_locs[k + 1] - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x_[n_y_dc_max_idx] - n_x_ac;

            // R = (AC_red/DC_red) / (AC_ir/DC_ir), 放大 20 倍用于查表
            n_nume = (n_y_ac * n_x_dc_max) >> 7;
            n_denom = (n_x_ac * n_y_dc_max) >> 7;
            if (n_denom > 0 && n_i_ratio_count < 5 && n_nume != 0) {
                an_ratio[n_i_ratio_count] = (n_nume * 20) / n_denom;
                n_i_ratio_count++;
            }
        }
    }

    // ---- Step 15: 排序取中位数 ----
    maxim_sort_ascend(an_ratio, n_i_ratio_count);
    n_middle_idx = n_i_ratio_count / 2;
    if (n_middle_idx > 1)
        n_ratio_average = (an_ratio[n_middle_idx - 1] + an_ratio[n_middle_idx]) / 2;
    else
        n_ratio_average = an_ratio[n_middle_idx];

    // ---- Step 16: 查表得 SpO2 ----
    if (n_ratio_average > 2 && n_ratio_average < 184) {
        n_spo2_calc = uch_spo2_table[n_ratio_average];
        *pn_spo2 = n_spo2_calc;
        *pch_spo2_valid = 1;
    } else {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
    }
}

// ===================================================================
// 峰值检测链 (移植自参考库, 逐行对齐)
// ===================================================================

void MAX30102::maxim_find_peaks(int32_t* pn_locs, int32_t* pn_npks,
    int32_t* pn_x, int32_t n_size,
    int32_t n_min_height, int32_t n_min_distance, int32_t n_max_num)
{
    maxim_peaks_above_min_height(pn_locs, pn_npks, pn_x, n_size, n_min_height);
    maxim_remove_close_peaks(pn_locs, pn_npks, pn_x, n_min_distance);
    *pn_npks = (*pn_npks < n_max_num) ? *pn_npks : n_max_num;
}

void MAX30102::maxim_peaks_above_min_height(int32_t* pn_locs, int32_t* pn_npks,
    int32_t* pn_x, int32_t n_size, int32_t n_min_height)
{
    int32_t i = 1, n_width;
    *pn_npks = 0;

    while (i < n_size - 1) {
        if (pn_x[i] > n_min_height && pn_x[i] > pn_x[i - 1]) {
            n_width = 1;
            while (i + n_width < n_size && pn_x[i] == pn_x[i + n_width])
                n_width++;
            if (pn_x[i] > pn_x[i + n_width] && (*pn_npks) < 15) {
                pn_locs[(*pn_npks)++] = i;
                i += n_width + 1;
            } else {
                i += n_width;
            }
        } else {
            i++;
        }
    }
}

void MAX30102::maxim_remove_close_peaks(int32_t* pn_locs, int32_t* pn_npks,
    int32_t* pn_x, int32_t n_min_distance)
{
    // 按位置顺序去重: 保留每个心动周期中最早的峰 (收缩峰),
    // 丢弃后面 min_distance 内的峰 (重搏切迹/反射峰),
    // 避免因按高度排序而误保留切迹、丢弃主峰
    if (*pn_npks <= 1) return;

    maxim_sort_ascend(pn_locs, *pn_npks);

    int32_t n_old_npks = *pn_npks;
    int32_t n_new = 1;  // 保留第一个峰
    for (int32_t j = 1; j < n_old_npks; j++) {
        if (pn_locs[j] - pn_locs[n_new - 1] >= n_min_distance) {
            pn_locs[n_new++] = pn_locs[j];
        }
    }
    *pn_npks = n_new;
}

void MAX30102::maxim_sort_ascend(int32_t* pn_x, int32_t n_size)
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_x[i];
        for (j = i; j > 0 && n_temp < pn_x[j - 1]; j--)
            pn_x[j] = pn_x[j - 1];
        pn_x[j] = n_temp;
    }
}

void MAX30102::maxim_sort_indices_descend(int32_t* pn_x, int32_t* pn_indx, int32_t n_size)
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_indx[i];
        for (j = i; j > 0 && pn_x[n_temp] > pn_x[pn_indx[j - 1]]; j--)
            pn_indx[j] = pn_indx[j - 1];
        pn_indx[j] = n_temp;
    }
}

// ===================================================================
// PerformMeasurement (保留兼容旧用法, 使用新算法)
// ===================================================================

HealthResult MAX30102::PerformMeasurement(int duration_sec) {
    HealthResult result;
    result.heart_rate = -1;
    result.spo2 = -1;
    result.signal_quality = 0.0f;
    result.finger_detected = false;

    const int sample_rate = 100;
    const int total_samples = duration_sec * sample_rate;
    const int max_samples = 2000;
    int actual_samples = (total_samples < max_samples) ? total_samples : max_samples;

    std::vector<uint32_t> red_buf(actual_samples);
    std::vector<uint32_t> ir_buf(actual_samples);
    int sample_idx = 0;

    ESP_LOGI(TAG, "开始采集 PPG 数据 (%d秒, ~%d采样点)...", duration_sec, actual_samples);
    ClearFifo();

    int64_t start_time = esp_timer_get_time() / 1000;

    while (sample_idx < actual_samples) {
        uint32_t red, ir;
        if (ReadFifo(red, ir)) {
            red_buf[sample_idx] = red;
            ir_buf[sample_idx]  = ir;
            sample_idx++;
        }
        vTaskDelay(pdMS_TO_TICKS(8));

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

    // 使用移植的参考算法
    return ComputeResults(red_buf.data(), ir_buf.data(), sample_idx);
}
