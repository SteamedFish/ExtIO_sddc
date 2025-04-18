#include "../RadioHandler.h"

#define R828D_FREQ (16000000) // R820T reference frequency
#define R828D_IF_CARRIER (4570000)

#define HIGH_MODE 0x80
#define LOW_MODE 0x00

#define GAIN_SWEET_POINT 18
#define HIGH_GAIN_RATIO (0.409f)
#define LOW_GAIN_RATIO (0.059f)

#define MODE HIGH_MODE

const float RX888R2Radio::vhf_rf_steps[RX888R2Radio::vhf_rf_step_size] = {
    0.0f, 0.9f, 1.4f, 2.7f, 3.7f, 7.7f, 8.7f, 12.5f, 14.4f, 15.7f,
    16.6f, 19.7f, 20.7f, 22.9f, 25.4f, 28.0f, 29.7f, 32.8f,
    33.8f, 36.4f, 37.2f, 38.6f, 40.2f, 42.1f, 43.4f, 43.9f,
    44.5f, 48.0f, 49.6f};

const float RX888R2Radio::vhf_if_steps[RX888R2Radio::vhf_if_step_size] = {
    -4.7f, -2.1f, 0.5f, 3.5f, 7.7f, 11.2f, 13.6f, 14.9f, 16.3f, 19.5f, 23.1f, 26.5f, 30.0f, 33.7f, 37.2f, 40.8f};

RX888R2Radio::RX888R2Radio(fx3class *fx3)
    : RadioHardware(fx3)
{
    for (uint8_t i = 0; i < hf_rf_step_size; i++)
    {
        this->hf_rf_steps[hf_rf_step_size - i - 1] = -(
            ((i & 0x01) != 0) * 0.5f +
            ((i & 0x02) != 0) * 1.0f +
            ((i & 0x04) != 0) * 2.0f +
            ((i & 0x08) != 0) * 4.0f +
            ((i & 0x010) != 0) * 8.0f +
            ((i & 0x020) != 0) * 16.0f);
    }

    for (uint8_t i = 0; i < hf_if_step_size; i++)
    {
        if (i > GAIN_SWEET_POINT)
            this->hf_if_steps[i] = 20.0f * log10f(HIGH_GAIN_RATIO * (i - GAIN_SWEET_POINT + 3));
        else
            this->hf_if_steps[i] = 20.0f * log10f(LOW_GAIN_RATIO * (i + 1));
    }
}

void RX888R2Radio::Initialize(uint32_t adc_rate)
{
    SampleRate = adc_rate;
    Fx3->Control(STARTADC, adc_rate);
}

rf_mode RX888R2Radio::PrepareLo(uint64_t freq)
{
    if (freq < 10 * 1000) return NOMODE;
    if (freq > 1750 * 1000 * 1000) return NOMODE;

    if ( freq >= this->SampleRate / 2)
        return VHFMODE;
    else
        return HFMODE;
}

bool RX888R2Radio::UpdatemodeRF(rf_mode mode)
{
    if (mode == VHFMODE)
    {
        // disable HF by set max ATT
        UpdateattRF(0);  // max att 0 -> -31.5 dB

        // switch to VHF Attenna
        FX3SetGPIO(VHF_EN);

        // high gain, 0db
        uint8_t gain = 0x80 | 3;
        Fx3->SetArgument(AD8340_VGA, gain);
        // Enable Tuner reference clock
        uint32_t ref = R828D_FREQ;
        return Fx3->Control(TUNERINIT, ref); // Initialize Tuner
    }
    else if (mode == HFMODE)
    {
        Fx3->Control(TUNERSTDBY); // Stop Tuner

        return FX3UnsetGPIO(VHF_EN);                // switch to HF Attenna
    }

    return false;
}

bool RX888R2Radio::UpdateattRF(int att)
{
    if (!(gpios & VHF_EN))
    {
        // hf mode
        if (att > hf_rf_step_size - 1)
            att = hf_rf_step_size - 1;
        if (att < 0)
            att = 0;
        uint8_t d = hf_rf_step_size - att - 1;

        DbgPrintf("UpdateattRF %f \n", this->hf_rf_steps[att]);

        return Fx3->SetArgument(DAT31_ATT, d);
    }
    else
    {
        uint16_t index = att;
        // this is in VHF mode
        return Fx3->SetArgument(R82XX_ATTENUATOR, index);
    }
}

uint64_t RX888R2Radio::TuneLo(uint64_t freq)
{
    if (!(gpios & VHF_EN))
    {
        // this is in HF mode
        return 0;
    }
    else
    {
        // this is in VHF mode
        Fx3->Control(TUNERTUNE, freq);
        return freq - R828D_IF_CARRIER;
    }
}

int RX888R2Radio::getRFSteps(const float **steps) const
{
    if (!(gpios & VHF_EN))
    {
        // hf mode
        *steps = this->hf_rf_steps;
        return hf_rf_step_size;
    }
    else
    {
        *steps = this->vhf_rf_steps;
        return vhf_rf_step_size;
    }
}

int RX888R2Radio::getIFSteps(const float **steps) const
{
    if (!(gpios & VHF_EN))
    {
        *steps = this->hf_if_steps;
        return hf_if_step_size;
    }
    else
    {
        *steps = this->vhf_if_steps;
        return vhf_if_step_size;
    }
}

bool RX888R2Radio::UpdateGainIF(int gain_index)
{
    if (!(gpios & VHF_EN))
    {
        // this is in HF mode
        uint8_t gain;
        if (gain_index > GAIN_SWEET_POINT)
            gain = HIGH_MODE | (gain_index - GAIN_SWEET_POINT + 3);
        else
            gain = LOW_MODE | (gain_index + 1);

        DbgPrintf("UpdateGainIF %d \n", gain);

        return Fx3->SetArgument(AD8340_VGA, gain);
    }
    else
    {
        // this is in VHF mode
        return Fx3->SetArgument(R82XX_VGA, (uint16_t)gain_index);
    }
}