#include "ml307_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

// 定义日志标签
#define TAG "CompactMl307Board"

// 声明LVGL字体资源
LV_FONT_DECLARE(font_puhui_14_1);    // 声明普惠字体，用于显示中文
LV_FONT_DECLARE(font_awesome_14_1);  // 声明Font Awesome字体，用于显示图标

/**
 * @brief CompactMl307Board类 - 紧凑型ML307开发板实现
 * 
 * 该类继承自Ml307Board基类，实现了一个带有OLED显示屏和按钮的紧凑型4G开发板
 */
class CompactMl307Board : public Ml307Board {
private:
    i2c_master_bus_handle_t display_i2c_bus_;     // I2C总线句柄，用于OLED显示屏通信
    esp_lcd_panel_io_handle_t panel_io_ = nullptr; // LCD面板IO句柄
    esp_lcd_panel_handle_t panel_ = nullptr;       // LCD面板句柄
    Display* display_ = nullptr;                   // 显示器对象指针
    Button boot_button_;                           // Boot按钮对象，用于切换聊天状态
    Button touch_button_;                          // 触摸按钮对象，用于语音输入控制
    Button volume_up_button_;                      // 音量增加按钮
    Button volume_down_button_;                    // 音量减少按钮

    /**
     * @brief 初始化显示屏I2C总线
     * 
     * 配置并初始化用于OLED显示屏的I2C总线
     */
    void InitializeDisplayI2c() {
        // 配置I2C总线参数
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,              // 使用I2C端口0
            .sda_io_num = DISPLAY_SDA_PIN,          // SDA引脚
            .scl_io_num = DISPLAY_SCL_PIN,          // SCL引脚
            .clk_source = I2C_CLK_SRC_DEFAULT,      // 默认时钟源
            .glitch_ignore_cnt = 7,                 // 忽略毛刺计数
            .intr_priority = 0,                     // 中断优先级
            .trans_queue_depth = 0,                 // 传输队列深度
            .flags = {
                .enable_internal_pullup = 1,        // 启用内部上拉电阻
            },
        };
        // 创建新的I2C主总线
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    /**
     * @brief 初始化SSD1306 OLED显示屏
     * 
     * 配置并初始化SSD1306 OLED显示屏驱动
     */
    void InitializeSsd1306Display() {
        // SSD1306 I2C配置
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,                      // SSD1306的I2C地址
            .on_color_trans_done = nullptr,        // 颜色传输完成回调
            .user_ctx = nullptr,                   // 用户上下文
            .control_phase_bytes = 1,              // 控制阶段字节数
            .dc_bit_offset = 6,                    // 数据/命令位偏移
            .lcd_cmd_bits = 8,                     // LCD命令位数
            .lcd_param_bits = 8,                   // LCD参数位数
            .flags = {
                .dc_low_on_data = 0,               // 数据时DC低电平
                .disable_control_phase = 0,        // 禁用控制阶段
            },
            .scl_speed_hz = 400 * 1000,            // I2C时钟频率400kHz
        };

        // 创建新的LCD面板I2C接口
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        // 配置LCD面板参数
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;          // 不使用复位引脚
        panel_config.bits_per_pixel = 1;           // 单色显示，每像素1位

        // SSD1306特定配置
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT), // 显示屏高度
        };
        panel_config.vendor_config = &ssd1306_config;

        // 创建新的SSD1306面板
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // 复位显示屏
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        // 初始化面板
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();  // 初始化失败时使用空显示
            return;
        }

        // 打开显示
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        // 创建OLED显示对象
        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            {&font_puhui_14_1, &font_awesome_14_1});
    }

    /**
     * @brief 初始化按钮功能
     * 
     * 为各个按钮设置点击、长按等事件的回调函数
     */
    void InitializeButtons() {
        // Boot按钮点击事件 - 切换聊天状态
        boot_button_.OnClick([this]() {
            Application::GetInstance().ToggleChatState();
        });
        
        // 触摸按钮按下事件 - 开始语音输入
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        
        // 触摸按钮释放事件 - 停止语音输入
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        // 音量增加按钮点击事件 - 增加音量10%
        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        // 音量增加按钮长按事件 - 设置最大音量
        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        // 音量减少按钮点击事件 - 减少音量10%
        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        // 音量减少按钮长按事件 - 静音
        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    /**
     * @brief 物联网初始化
     * 
     * 添加对AI可见的设备，使AI能够控制这些设备
     */
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker")); // 添加扬声器设备
        thing_manager.AddThing(iot::CreateThing("Lamp"));    // 添加灯设备
    }

public:
    /**
     * @brief 构造函数
     * 
     * 初始化ML307板，配置按钮、显示屏和其他功能
     */
    CompactMl307Board() : Ml307Board(ML307_TX_PIN, ML307_RX_PIN, 4096), // 初始化ML307基板，设置TX/RX引脚和缓冲区大小
        boot_button_(BOOT_BUTTON_GPIO),                                 // 初始化Boot按钮
        touch_button_(TOUCH_BUTTON_GPIO),                               // 初始化触摸按钮
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),                       // 初始化音量增加按钮
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {                  // 初始化音量减少按钮

        InitializeDisplayI2c();     // 初始化I2C总线
        InitializeSsd1306Display(); // 初始化OLED显示屏
        InitializeButtons();        // 初始化按钮功能
        InitializeIot();            // 初始化物联网功能
    }

    /**
     * @brief 获取LED对象
     * 
     * @return Led* LED控制对象指针
     */
    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO); // 创建单LED对象，使用内置LED引脚
        return &led;
    }

    /**
     * @brief 获取音频编解码器对象
     * 
     * @return AudioCodec* 音频编解码器对象指针
     */
    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        // 单工模式 - 使用不同的引脚进行输入和输出
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        // 双工模式 - 使用相同的引脚进行输入和输出
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    /**
     * @brief 获取显示器对象
     * 
     * @return Display* 显示器对象指针
     */
    virtual Display* GetDisplay() override {
        return display_;
    }
};

// 声明此类为板级实现，使系统能够识别并使用它
DECLARE_BOARD(CompactMl307Board);
