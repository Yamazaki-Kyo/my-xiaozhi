#ifndef _MAX30102_H_
#define _MAX30102_H_

#include <cstdint>
#include <cstddef>
#include <driver/i2c_master.h>

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
#define MAX30102_REG_LED1_PA        0x0C  // RED LED pulse amplitude
#define MAX30102_REG_LED2_PA        0x0D  // IR LED pulse amplitude
#define MAX30102_REG_TEMP_INTEGER   0x1F
#define MAX30102_REG_TEMP_FRACTION  0x20
#define MAX30102_REG_REV_ID         0xFE
#define MAX30102_REG_PART_ID        0xFF

// 采样结果
struct HealthResult {
    int heart_rate;       // 心率 (bpm), -1 表示无效
    int spo2;             // 血氧饱和度 (%), -1 表示无效
    float signal_quality; // 信号质量 (0.0 - 1.0)
    bool finger_detected;
};

class MAX30102 {
public:
    MAX30102();
    ~MAX30102();

    // 初始化传感器
    bool Initialize();

    // 检测传感器是否在线 (扫描I2C总线)
    bool IsDeviceOnline();

    // 判断是否有手指按压 (IR信号强度阈值)
    bool IsFingerDetected();

    // 从FIFO读取一组IR/RED采样数据
    bool ReadFifo(uint32_t& red, uint32_t& ir);

    // 清空FIFO
    void ClearFifo();

    // 执行完整检测流程，返回心率/血氧结果
    // 采样时间秒数 (建议15-20秒)
    HealthResult PerformMeasurement(int duration_sec = 15);

    // 获取最后一次检测结果
    HealthResult GetLastResult() const { return last_result_; }

private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_dev_;
    HealthResult last_result_;

    void WriteReg(uint8_t reg, uint8_t value);
    uint8_t ReadReg(uint8_t reg);
    void ReadRegs(uint8_t reg, uint8_t* buffer, size_t length);

    // PPG信号处理算法
    struct PpgData {
        uint32_t* red_samples;
        uint32_t* ir_samples;
        int sample_count;
        int sample_rate;  // Hz
    };

    // 去直流分量 (高通滤波)
    void RemoveDcComponent(PpgData& data);

    // 移动平均滤波
    void MovingAverageFilter(PpgData& data, int window_size = 4);

    // 心率计算: 基于峰值检测的自相关方法
    int CalculateHeartRate(const PpgData& data);

    // 血氧计算: 基于AC/DC比值
    int CalculateSpO2(const PpgData& data);

    // 信号质量评估
    float EvaluateSignalQuality(const PpgData& data);
};

#endif // _MAX30102_H_
