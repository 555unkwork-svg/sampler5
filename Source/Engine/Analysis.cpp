#include "Analysis.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace eng
{
namespace
{
constexpr int kFftOrder = 10;
constexpr int kFftSize = 1 << kFftOrder;
constexpr int kHop = 256;

std::vector<float> mixToMono (const juce::AudioBuffer<float>& audio)
{
    const int n = audio.getNumSamples();
    const int chans = audio.getNumChannels();
    std::vector<float> mono ((size_t) n, 0.0f);

    for (int c = 0; c < chans; ++c)
    {
        const float* src = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += src[i];
    }

    if (chans > 1)
        for (auto& v : mono)
            v /= (float) chans;

    return mono;
}

/** Flujo espectral: cuanta energia nueva aparece en cada cuadro. */
std::vector<float> spectralFlux (const std::vector<float>& mono)
{
    const int n = (int) mono.size();
    const int frames = n / kHop + 1;
    std::vector<float> flux ((size_t) frames, 0.0f);

    juce::dsp::FFT fft (kFftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann, false);

    std::vector<float> work ((size_t) kFftSize * 2, 0.0f);
    std::vector<float> prev ((size_t) kFftSize / 2, 0.0f);

    for (int f = 0; f < frames; ++f)
    {
        // el cuadro f termina en la muestra (f+1)*kHop: lo nuevo esta en el ultimo salto
        const int end = (f + 1) * kHop;
        const int start = end - kFftSize;

        for (int i = 0; i < kFftSize; ++i)
        {
            const int idx = start + i;
            work[(size_t) i] = (idx >= 0 && idx < n) ? mono[(size_t) idx] : 0.0f;
        }
        std::fill (work.begin() + kFftSize, work.end(), 0.0f);

        window.multiplyWithWindowingTable (work.data(), (size_t) kFftSize);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        float sum = 0.0f;
        for (int b = 1; b < kFftSize / 2; ++b)
        {
            const float mag = std::log1p (50.0f * work[(size_t) b]);
            const float diff = mag - prev[(size_t) b];
            if (diff > 0.0f)
                sum += diff;
            prev[(size_t) b] = mag;
        }
        flux[(size_t) f] = sum;
    }

    return flux;
}

/** Afina la posicion: busca donde arranca de verdad el golpe en la senal. */
juce::int64 refineOnset (const std::vector<float>& mono, int roughEnd)
{
    const int n = (int) mono.size();
    const int from = juce::jlimit (0, n, roughEnd - kFftSize);
    const int to = juce::jlimit (0, n, roughEnd + kHop);
    if (to - from < 8)
        return from;

    // envolvente corta (~1.5 ms a 44.1k)
    const int win = 64;
    float peak = 0.0f;
    std::vector<float> env ((size_t) (to - from), 0.0f);
    float acc = 0.0f;
    for (int i = from; i < to; ++i)
    {
        acc += std::abs (mono[(size_t) i]);
        if (i - win >= from)
            acc -= std::abs (mono[(size_t) (i - win)]);
        env[(size_t) (i - from)] = acc;
        peak = std::max (peak, acc);
    }

    if (peak <= 0.0f)
        return from;

    // nivel previo al golpe: el minimo de la envolvente antes del pico
    int peakIdx = 0;
    for (int i = 0; i < (int) env.size(); ++i)
        if (env[(size_t) i] >= peak) { peakIdx = i; break; }

    float floorLevel = peak;
    for (int i = win; i <= peakIdx; ++i)
        floorLevel = std::min (floorLevel, env[(size_t) i]);
    if (peakIdx < win)
        floorLevel = 0.0f;

    const float threshold = floorLevel + 0.1f * (peak - floorLevel);
    int idx = peakIdx;
    while (idx > 0 && env[(size_t) idx] > threshold)
        --idx;

    // como en los samplers clasicos: el corte va en un cruce por cero, asi no hace click
    int pos = from + idx;
    const int limit = std::max (1, pos - 96);
    for (int i = pos; i >= limit; --i)
        if ((mono[(size_t) i - 1] <= 0.0f) != (mono[(size_t) i] <= 0.0f))
            return i;

    return pos;
}

/** Cuanto sube el nivel en el golpe respecto de lo que venia sonando justo antes. */
float levelRise (const std::vector<float>& mono, juce::int64 position, double sampleRate)
{
    const int n = (int) mono.size();
    const int pos = (int) position;
    auto meanAbs = [&] (int a, int b)
    {
        a = juce::jlimit (0, n, a);
        b = juce::jlimit (0, n, b);
        if (b <= a) return 0.0f;
        double s = 0.0;
        for (int i = a; i < b; ++i) s += std::abs (mono[(size_t) i]);
        return (float) (s / (b - a));
    };

    const int ms = std::max (1, (int) (sampleRate * 0.001));
    const float before = meanAbs (pos - 40 * ms, pos - 3 * ms);
    float after = 0.0f;
    for (int k = 0; k < 6; ++k)
        after = std::max (after, meanAbs (pos + k * 10 * ms, pos + (k + 1) * 10 * ms));
    return after / (before + 1.0e-5f);
}

/** Si el audio es un loop, su largo es una cantidad redonda de compases: lo usamos para
    corregir errores tipicos del tempo (mitad, doble, 3/4, 4/3...). */
double fitBpmToLoopLength (double bpm, double seconds)
{
    if (bpm <= 0.0 || seconds <= 0.5 || seconds > 60.0)
        return bpm;

    double bestErr = 1.0, bestBpm = bpm;
    for (double ratio : { 1.0, 0.5, 2.0, 0.75, 4.0 / 3.0, 2.0 / 3.0, 1.5 })
    {
        const double c = bpm * ratio;
        if (c < 60.0 || c >= 200.0)
            continue;
        for (int bars : { 1, 2, 4, 8, 16, 32, 64 })
        {
            const double target = bars * 4.0;
            const double err = std::abs (seconds * c / 60.0 - target) / target;
            // ante la duda nos quedamos con la estimacion original
            if (err < bestErr - (ratio == 1.0 ? 0.0 : 0.005))
            {
                bestErr = err;
                bestBpm = target * 60.0 / seconds;
            }
        }
    }

    if (bestErr > 0.015)
    {
        // la estimacion no encaja con el largo. Si el largo solo ya da un tempo entero exacto,
        // es casi seguro un loop recortado a compases: le creemos al largo
        double choice = 0.0;
        for (int bars : { 1, 2, 4, 8, 16, 32, 64 })
        {
            const double c = bars * 240.0 / seconds;
            if (c >= 70.0 && c < 180.0 && std::abs (c - std::round (c)) < 0.06
                && (choice == 0.0 || std::abs (std::log (c / bpm)) < std::abs (std::log (choice / bpm))))
                choice = std::round (c);
        }
        return choice > 0.0 ? choice : bpm;
    }
    const double rounded = std::round (bestBpm);
    return std::abs (bestBpm - rounded) < 0.35 ? rounded : std::round (bestBpm * 100.0) / 100.0;
}

double estimateBpm (const std::vector<float>& flux, double sampleRate)
{
    const int n = (int) flux.size();
    const double framesPerSecond = sampleRate / kHop;
    if (n < (int) (framesPerSecond * 2.0))
        return 0.0;

    // quitamos la media para que la autocorrelacion mida ritmo y no nivel
    double mean = 0.0;
    for (float v : flux) mean += v;
    mean /= n;
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = std::max (0.0f, flux[(size_t) i] - (float) mean);

    auto autocorr = [&] (int lag) -> double
    {
        if (lag <= 0 || lag >= n) return 0.0;
        double s = 0.0;
        for (int i = 0; i + lag < n; ++i)
            s += (double) x[(size_t) i] * x[(size_t) (i + lag)];
        return s / (double) (n - lag);
    };

    const int minLag = (int) std::floor (framesPerSecond * 60.0 / 180.0);
    const int maxLag = std::min (n / 2, (int) std::ceil (framesPerSecond * 60.0 / 70.0));
    if (maxLag <= minLag + 2)
        return 0.0;

    double best = 0.0;
    int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        // sumamos tambien los multiplos: un tempo real se repite cada 2, 3 y 4 tiempos
        double score = autocorr (lag) + 0.5 * autocorr (lag * 2) + 0.33 * autocorr (lag * 3) + 0.25 * autocorr (lag * 4);
        // leve preferencia por tempos alrededor de 120
        const double bpm = 60.0 * framesPerSecond / lag;
        const double pref = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 2.0, 2.0));
        score *= pref;

        // si el periodo se divide en 3 partes iguales pero no en 2, casi seguro es
        // "un tiempo y medio" y no el tiempo real: lo castigamos
        const double whole = autocorr (lag);
        if (whole > 0.0
            && autocorr ((int) std::lround (lag / 3.0)) > 0.7 * whole
            && autocorr ((int) std::lround (lag / 2.0)) < 0.5 * whole)
            score *= 0.6;
        if (score > best) { best = score; bestLag = lag; }
    }

    if (bestLag == 0 || best <= 0.0)
        return 0.0;

    // afinamos con multiplos largos del periodo: mas lag = mas resolucion
    double period = bestLag;
    for (int k : { 1, 2, 4, 8, 16 })
    {
        const int centre = (int) std::lround (period * k);
        if (centre + 3 >= n / 2 && k > 1)
            break;

        int peakLag = centre;
        double peakVal = -1.0;
        for (int lag = centre - 2; lag <= centre + 2; ++lag)
        {
            const double v = autocorr (lag);
            if (v > peakVal) { peakVal = v; peakLag = lag; }
        }

        const double a = autocorr (peakLag - 1), b = peakVal, c = autocorr (peakLag + 1);
        const double denom = a - 2.0 * b + c;
        const double offset = std::abs (denom) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom) : 0.0;
        period = (peakLag + offset) / k;
    }

    double bpm = 60.0 * framesPerSecond / period;
    const double rounded = std::round (bpm);
    if (std::abs (bpm - rounded) < 0.25)
        bpm = rounded;
    else
        bpm = std::round (bpm * 100.0) / 100.0;

    return bpm;
}
} // namespace

Analysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    Analysis result;
    if (audio.getNumSamples() < kFftSize || sampleRate <= 0.0)
        return result;

    const auto mono = mixToMono (audio);
    const auto flux = spectralFlux (mono);
    const int frames = (int) flux.size();

    const float maxFlux = *std::max_element (flux.begin(), flux.end());
    if (maxFlux <= 0.0f)
        return result;

    const double framesPerSecond = sampleRate / kHop;
    const int localRadius = std::max (2, (int) std::lround (framesPerSecond * 0.03));
    const int meanRadius = std::max (4, (int) std::lround (framesPerSecond * 0.10));
    const juce::int64 minGap = (juce::int64) (sampleRate * 0.08);

    for (int f = 0; f < frames; ++f)
    {
        const float v = flux[(size_t) f];
        if (v < 0.06f * maxFlux)
            continue;

        bool isPeak = true;
        for (int j = std::max (0, f - localRadius); j <= std::min (frames - 1, f + localRadius) && isPeak; ++j)
            if (flux[(size_t) j] > v || (flux[(size_t) j] == v && j < f))
                isPeak = false;
        if (! isPeak)
            continue;

        double mean = 0.0;
        int count = 0;
        for (int j = std::max (0, f - meanRadius); j <= std::min (frames - 1, f + meanRadius); ++j)
        {
            mean += flux[(size_t) j];
            ++count;
        }
        mean /= std::max (1, count);
        if (v < 1.5 * mean)
            continue;

        const auto pos = refineOnset (mono, (f + 1) * kHop);

        // un golpe "de verdad" arranca desde un nivel mas bajo; un cambio dentro de una
        // nota que ya venia sonando (una silaba, un eco) pesa mucho menos
        const float rise = levelRise (mono, pos, sampleRate);
        const float weight = 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, (rise - 1.0f) / 1.5f);
        const float strength = v / maxFlux * weight;

        if (! result.onsets.empty() && pos - result.onsets.back().position < minGap)
        {
            result.onsets.back().strength = std::max (result.onsets.back().strength, strength);
            continue;
        }

        result.onsets.push_back ({ pos, strength });
    }

    float strongest = 0.0f;
    for (const auto& o : result.onsets) strongest = std::max (strongest, o.strength);
    if (strongest > 0.0f)
        for (auto& o : result.onsets) o.strength /= strongest;

    result.bpm = fitBpmToLoopLength (estimateBpm (flux, sampleRate), audio.getNumSamples() / sampleRate);
    return result;
}

std::vector<juce::int64> makeSlices (SliceMode mode, const Analysis& analysis,
                                     juce::int64 numSamples, double sampleRate,
                                     double bpm, double gridBeats, juce::int64 randomSeed,
                                     double sensitivity)
{
    std::vector<juce::int64> out;
    if (numSamples <= 0)
        return out;

    auto equalParts = [&]
    {
        for (int i = 0; i < kNumPads; ++i)
            out.push_back (numSamples * i / kNumPads);
    };

    switch (mode)
    {
        case SliceMode::transients:
        {
            auto onsets = analysis.onsets;
            if (onsets.empty())
            {
                equalParts();
                break;
            }
            // nos quedamos con los mas fuertes: la sensibilidad decide cuantos entran
            std::stable_sort (onsets.begin(), onsets.end(),
                              [] (const Onset& a, const Onset& b) { return a.strength > b.strength; });
            const double s = juce::jlimit (0.0, 1.0, sensitivity);
            const float threshold = (float) (0.6 * (1.0 - s) * (1.0 - s));
            size_t keep = 1;
            while (keep < onsets.size() && keep < (size_t) kNumPads && onsets[keep].strength >= threshold)
                ++keep;
            onsets.resize (keep);
            for (const auto& o : onsets)
                out.push_back (o.position);
            break;
        }

        case SliceMode::grid:
        {
            if (bpm <= 0.0 || sampleRate <= 0.0 || gridBeats <= 0.0)
            {
                equalParts();
                break;
            }
            const double step = sampleRate * 60.0 / bpm * gridBeats;
            const double origin = analysis.onsets.empty() ? 0.0 : (double) analysis.onsets.front().position;
            // si el primer golpe esta a mas de un paso del inicio, la grilla arranca antes
            double first = origin;
            while (first - step >= 0.0)
                first -= step;
            for (int i = 0; i < kNumPads; ++i)
            {
                const auto pos = (juce::int64) std::llround (first + step * i);
                if (pos >= numSamples)
                    break;
                out.push_back (pos);
            }
            break;
        }

        case SliceMode::equal:
            equalParts();
            break;

        case SliceMode::random:
        {
            juce::Random rng (randomSeed);
            if ((int) analysis.onsets.size() > kNumPads)
            {
                auto onsets = analysis.onsets;
                for (int i = 0; i < kNumPads; ++i)
                {
                    const int pick = rng.nextInt ((int) onsets.size());
                    out.push_back (onsets[(size_t) pick].position);
                    onsets.erase (onsets.begin() + pick);
                }
            }
            else
            {
                const juce::int64 minGap = std::max<juce::int64> (1, numSamples / (kNumPads * 8));
                int guard = 0;
                while ((int) out.size() < kNumPads && guard++ < 2000)
                {
                    const auto pos = (juce::int64) (rng.nextDouble() * (double) (numSamples - 1));
                    bool tooClose = false;
                    for (auto p : out)
                        if (std::abs (p - pos) < minGap) { tooClose = true; break; }
                    if (! tooClose)
                        out.push_back (pos);
                }
            }
            break;
        }

        case SliceMode::manual:
            break;
    }

    std::sort (out.begin(), out.end());
    return out;
}

double bpmFromFileName (const juce::String& name)
{
    juce::StringArray tokens;
    tokens.addTokens (name, " _-.()[]", "");
    for (const auto& t : tokens)
        if (t.length() >= 2 && t.length() <= 3 && t.containsOnly ("0123456789"))
        {
            const int v = t.getIntValue();
            if (v >= 60 && v <= 200)
                return (double) v;
        }
    return 0.0;
}
}
