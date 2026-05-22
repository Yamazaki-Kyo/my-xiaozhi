#ifndef _MAX30102_H_
#define _MAX30102_H_

#include <cstdint>
#include <cstddef>
#include <driver/i2c_master.h>
#include <vector>

// MAX30102 寄存器定义
#define MAX30102_REG_INT_STATUS_1   0x00
#define MAX30102_REG_INT_STATUS_2   0x01
#define MAX30102_REG_INT_ENABLE_1   0x02
#define MAX30102_REG_INT_ENABLE_2   0x03
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C
#define MAX30102_REG_LED2_PA        0x0D
#define MAX30102_REG_PILOT_PA       0x10
#define MAX30102_REG_TEMP_INTEGER   0x1F
#define MAX30102_REG_TEMP_FRACTION  0x20
#define MAX30102_REG_REV_ID         0xFE
#define MAX30102_REG_PART_ID        0xFF

// 移植自参考库 "辰哥单片机设计" 的算法常量
#define PPG_BUFFER_SIZE   500   // 5 秒 × 100Hz
#define PPG_MA4_SIZE      4     // 4 点移动平均
#define PPG_HAMMING_SIZE  5     // 5 点 Hamming 窗

struct HealthResult {
    int heart_rate;
    int spo2;
    float signal_quality;
    bool finger_detected;
};

class MAX30102 {
public:
    MAX30102();
    ~MAX30102();

    bool Initialize();
    bool IsDeviceOnline();
    bool IsFingerDetected();
    bool ReadFifo(uint32_t& red, uint32_t& ir);
    void ClearFifo();

    HealthResult PerformMeasurement(int duration_sec = 15);
    HealthResult GetLastResult() const { return last_result_; }

    // ==== 非阻塞测量 API (用于 60 秒实时 PPG 波形) ====
    struct PpgSample {
        uint32_t red;
        uint32_t ir;
    };

    bool StartSampling();
    void StopSampling();
    bool ReadSample(PpgSample& sample);

    void UpdateRollingEstimate();
    int  GetRollingHR()   const { return rolling_hr_; }
    int  GetRollingSpO2() const { return rolling_spo2_; }

    HealthResult ComputeResults(const uint32_t* red, const uint32_t* ir, int count);

private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_dev_;
    HealthResult last_result_;

    void WriteReg(uint8_t reg, uint8_t value);
    uint8_t ReadReg(uint8_t reg);
    void ReadRegs(uint8_t reg, uint8_t* buffer, size_t length);

    // ==== 移植自参考库 "辰哥单片机设计" 的算法 ====

    // 核心算法: 基于 500 样本 (5秒) 窗口计算 HR 和 SpO2
    void maxim_heart_rate_and_oxygen_saturation(
        uint32_t* pun_ir_buffer, int32_t n_ir_buffer_length,
        uint32_t* pun_red_buffer,
        int32_t* pn_spo2, int8_t* pch_spo2_valid,
        int32_t* pn_heart_rate, int8_t* pch_hr_valid);

    // 峰值检测链
    void maxim_find_peaks(int32_t* pn_locs, int32_t* pn_npks,
        int32_t* pn_x, int32_t n_size,
        int32_t n_min_height, int32_t n_min_distance, int32_t n_max_num);
    void maxim_peaks_above_min_height(int32_t* pn_locs, int32_t* pn_npks,
        int32_t* pn_x, int32_t n_size, int32_t n_min_height);
    void maxim_remove_close_peaks(int32_t* pn_locs, int32_t* pn_npks,
        int32_t* pn_x, int32_t n_min_distance);

    // 排序工具
    void maxim_sort_ascend(int32_t* pn_x, int32_t n_size);
    void maxim_sort_indices_descend(int32_t* pn_x, int32_t* pn_indx, int32_t n_size);

    // 算法缓冲区 (500 样本, 避免栈溢出)
    std::vector<int32_t> an_x_;     // IR 处理缓冲
    std::vector<int32_t> an_y_;     // Red 处理缓冲
    std::vector<int32_t> an_dx_;    // 差分信号缓冲

    // 滚动估计缓冲区 (15 秒 × 100Hz)
    std::vector<int32_t> rolling_red_;
    std::vector<int32_t> rolling_ir_;
    int rolling_head_ = 0;
    int rolling_count_ = 0;
    int rolling_cap_ = 1500;
    int rolling_hr_ = -1;
    int rolling_spo2_ = -1;
};

#endif
