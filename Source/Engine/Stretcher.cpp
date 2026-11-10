#include "Stretcher.h"

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
#elif defined (_MSC_VER)
 #pragma warning (push, 0)
#endif

#include "signalsmith-stretch/signalsmith-stretch.h"

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic pop
#elif defined (_MSC_VER)
 #pragma warning (pop)
#endif

namespace eng
{
juce::AudioBuffer<float> stretchOffline (const juce::AudioBuffer<float>& input, double sampleRate,
                                         double speed, double semitones)
{
    const int chans = input.getNumChannels();
    const int inLen = input.getNumSamples();
    speed = juce::jlimit (0.25, 4.0, speed);
    const int outLen = (int) std::llround ((double) inLen / speed);

    juce::AudioBuffer<float> output (std::max (1, chans), std::max (1, outLen));
    output.clear();
    if (chans == 0 || inLen == 0 || outLen <= 0)
        return output;

    signalsmith::stretch::SignalsmithStretch<float> stretch (1234);
    stretch.presetDefault (chans, (float) sampleRate);
    stretch.setTransposeSemitones ((float) semitones);

    const bool ok = stretch.exact (input.getArrayOfReadPointers(), inLen,
                                   output.getArrayOfWritePointers(), outLen);

    if (! ok)
    {
        // audio demasiado corto para el algoritmo: lo remuestreamos y listo
        for (int c = 0; c < chans; ++c)
        {
            const float* src = input.getReadPointer (c);
            float* dst = output.getWritePointer (c);
            for (int i = 0; i < outLen; ++i)
            {
                const double pos = (double) i * speed;
                const int i0 = std::min (inLen - 1, (int) pos);
                const int i1 = std::min (inLen - 1, i0 + 1);
                const float frac = (float) (pos - i0);
                dst[i] = src[i0] + (src[i1] - src[i0]) * frac;
            }
        }
    }

    return output;
}
}
