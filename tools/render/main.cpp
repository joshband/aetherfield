#include "dsp/GainProcessor.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>

namespace {

void writeU16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void writeU32(std::ofstream& output, std::uint32_t value) {
    writeU16(output, static_cast<std::uint16_t>(value & 0xffffU));
    writeU16(output, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: aetherfield_render OUTPUT.wav\n";
        return 2;
    }

    // Integer-derived values intentionally avoid a libm-dependent source fixture.
    constexpr std::array<std::int16_t, 16> input {
        -30000, -24000, -18000, -12000, -6000, -3000, 0, 3000,
        6000, 12000, 18000, 24000, 30000, 18000, 6000, 0,
    };
    std::array<float, input.size()> samples {};
    for (std::size_t index = 0; index < input.size(); ++index) {
        samples[index] = static_cast<float>(input[index]);
    }
    aetherfield::dsp::processGain(samples.data(), samples.size(), 0.5F);

    std::ofstream output(argv[1], std::ios::binary);
    if (!output) {
        std::cerr << "Unable to open output WAV.\n";
        return 1;
    }

    constexpr std::uint32_t sampleRate = 48000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint32_t dataBytes = static_cast<std::uint32_t>(input.size() * sizeof(std::int16_t));
    output.write("RIFF", 4);
    writeU32(output, 36U + dataBytes);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16);
    writeU16(output, 1);
    writeU16(output, channels);
    writeU32(output, sampleRate);
    writeU32(output, sampleRate * channels * (bitsPerSample / 8U));
    writeU16(output, channels * (bitsPerSample / 8U));
    writeU16(output, bitsPerSample);
    output.write("data", 4);
    writeU32(output, dataBytes);
    for (float sample : samples) {
        writeU16(output, static_cast<std::uint16_t>(static_cast<std::int16_t>(sample)));
    }

    output.close();
    if (!output) {
        std::cerr << "Unable to write output WAV.\n";
        return 1;
    }
    return 0;
}
