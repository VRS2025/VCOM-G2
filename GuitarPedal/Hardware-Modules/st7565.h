#pragma once
// Драйвер ST7565R для LCD GMG12864-06D (128x64, 4-wire SPI).
// Совместим с daisy::OledDisplay<...> — тот же интерфейс, что у SSD130xDriver.
// Переиспользуем SSD130x4WireSpiTransport (те же пины SPI, что у старого OLED).
//
// Если картинка кривая — крутить ТОЛЬКО константы в блоке ниже:
//   зеркало по X      -> ADC_DIR  (0xA0 <-> 0xA1), при смене обычно меняй и COL_OFFSET
//   перевёрнуто по Y  -> COM_DIR  (0xC8 <-> 0xC0)
//   сдвиг по горизонтали -> COL_OFFSET (0 / 1 / 2 / 4)
//   пусто / весь чёрный  -> CONTRAST (0x10..0x30) и BIAS (0xA2 <-> 0xA3)

#include "dev/oled_ssd130x.h" // даёт SSD130x4WireSpiTransport

namespace daisy
{
template <size_t width, size_t height, typename Transport>
class ST7565Driver
{
  public:
    // ===== подстройка под конкретный модуль (значения из рабочей либы под GMG12864-06D) =====
    static constexpr uint8_t BIAS       = 0xA2; // 0xA2=1/9, 0xA3=1/7
    static constexpr uint8_t ADC_DIR    = 0xA0; // 0xA0 норм / 0xA1 зеркало по X
    static constexpr uint8_t COM_DIR    = 0xC8; // 0xC8 / 0xC0 — переворот по Y
    static constexpr uint8_t RES_RATIO  = 0x27; // 0x20..0x27
    static constexpr uint8_t CONTRAST   = 0x13; // 0x00..0x3F (19) — если бледно/пусто, поднять
    static constexpr uint8_t COL_OFFSET = 0;    // у этого модуля столбцы с 0
    // ===========================================================================

    struct Config
    {
        typename Transport::Config transport_config;
    };

    void Init(Config config)
    {
        transport_.Init(config.transport_config); // сброс + SPI делает транспорт
        transport_.SendCommand(0xAE);             // display off
        transport_.SendCommand(BIAS);             // LCD bias 1/9
        transport_.SendCommand(ADC_DIR);          // ADC (зеркало X)
        transport_.SendCommand(COM_DIR);          // COM (переворот Y)
        transport_.SendCommand(RES_RATIO);        // V5 resistor ratio
        transport_.SendCommand(0x2F);             // power: booster+regulator+follower ON
        System::Delay(50);                        // дать booster стабилизироваться
        transport_.SendCommand(0x81);             // contrast mode
        transport_.SendCommand(CONTRAST);         // contrast value
        transport_.SendCommand(0x40);             // start line = 0
        transport_.SendCommand(0xAF);             // display on
        transport_.SendCommand(0xA6);             // нормальная (не инверсная)
    }

    // Контраст (electronic volume) ST7565: команда 0x81 + значение 0..63.
    void SetContrast(uint8_t v)
    {
        if(v > 0x3F)
            v = 0x3F;
        transport_.SendCommand(0x81);
        transport_.SendCommand(v);
    }

    size_t Width() const { return width; }
    size_t Height() const { return height; }

    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
    {
        if(x >= width || y >= height)
            return;
        if(on)
            buffer_[x + (y / 8) * width] |= (1 << (y % 8));
        else
            buffer_[x + (y / 8) * width] &= ~(1 << (y % 8));
    }

    void Fill(bool on)
    {
        for(size_t i = 0; i < sizeof(buffer_); i++)
            buffer_[i] = on ? 0xFF : 0x00;
    }

    void Update()
    {
        for(uint8_t p = 0; p < (height / 8); p++)
        {
            transport_.SendCommand(0xB0 + p);                   // page
            transport_.SendCommand(0x00 | (COL_OFFSET & 0x0F)); // column low nibble
            transport_.SendCommand(0x10 | (COL_OFFSET >> 4));   // column high nibble
            transport_.SendData(&buffer_[width * p], width);
        }
    }

    bool UpdateFinished() { return true; }

  protected:
    Transport transport_;
    uint8_t   buffer_[width * height / 8];
};

// LCD GMG12864-06D: ST7565R, 128x64, 4-wire SPI (те же пины, что старый OLED).
using ST75654WireSpi128x64Driver
    = daisy::ST7565Driver<128, 64, SSD130x4WireSpiTransport>;
} // namespace daisy
