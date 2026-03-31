#pragma once

// ============================================================================
// DspDebugLog.h — RT-safe DSP diagnostic logger for ECHO-TR
//
// Captures per-sample snapshots of delay parameters and signal values
// into a lock-free ring buffer.  Auto-dumps CSV on processor destruction.
//
// Usage:
//   #define ECHOTR_DSP_DEBUG_LOG 1   // enable (0 = zero overhead)
//
// ============================================================================

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

#ifndef ECHOTR_DSP_DEBUG_LOG
 #define ECHOTR_DSP_DEBUG_LOG 0
#endif

#if ECHOTR_DSP_DEBUG_LOG

struct DspLogEntry
{
    double   timestampSec;
    float    delaySamplesTarget;   // target delay in samples (block-level)
    float    smoothedDelaySamples; // per-sample EMA value
    float    feedback;             // smoothed feedback
    float    outL;                 // output sample L
    float    outR;                 // output sample R
    float    delayedL;             // raw delayed sample L (from Hermite)
    float    delayedR;             // raw delayed sample R
    float    fbkL;                 // feedback sample L (post DC-blocker)
    float    fbkR;                 // feedback sample R
    float    dcOutL;               // DC blocker output state L
    float    dcOutR;               // DC blocker output state R
    float    writeValL;            // value written to delay buffer L
    float    writeValR;            // value written to delay buffer R
    int      writePos;             // buffer write position
    float    readPosF;             // fractional read position
};

class DspDebugLog
{
public:
    static constexpr int kRingSize = 65536;  // ~1.4s @ 48kHz

    DspDebugLog() = default;

    inline void logSample (float targetDelay, float smoothedDelay, float fb,
                           float oL, float oR,
                           float dL, float dR,
                           float fL, float fR,
                           float dcOL, float dcOR,
                           float wL, float wR,
                           int wPos, float rPos) noexcept
    {
        if (!armed_.load (std::memory_order_relaxed)) return;

        const int idx = writeIndex_.fetch_add (1, std::memory_order_relaxed) & (kRingSize - 1);
        auto& e = ring_[idx];
        e.timestampSec          = juce::Time::getMillisecondCounterHiRes() * 0.001;
        e.delaySamplesTarget    = targetDelay;
        e.smoothedDelaySamples  = smoothedDelay;
        e.feedback              = fb;
        e.outL                  = oL;
        e.outR                  = oR;
        e.delayedL              = dL;
        e.delayedR              = dR;
        e.fbkL                  = fL;
        e.fbkR                  = fR;
        e.dcOutL                = dcOL;
        e.dcOutR                = dcOR;
        e.writeValL             = wL;
        e.writeValR             = wR;
        e.writePos              = wPos;
        e.readPosF              = rPos;
    }

    void arm()   noexcept { writeIndex_.store (0, std::memory_order_relaxed); armed_.store (true, std::memory_order_release); }
    void disarm() noexcept { armed_.store (false, std::memory_order_release); }
    bool isArmed() const noexcept { return armed_.load (std::memory_order_relaxed); }

    bool dumpToFile (const juce::String& filePath) const
    {
        juce::File f (filePath);
        if (auto stream = f.createOutputStream())
        {
            stream->writeText ("timestamp_s,target_delay,smoothed_delay,feedback,"
                               "out_L,out_R,delayed_L,delayed_R,"
                               "fbk_L,fbk_R,dc_out_L,dc_out_R,"
                               "write_val_L,write_val_R,write_pos,read_pos\n",
                               false, false, nullptr);

            const int total = juce::jmin (writeIndex_.load (std::memory_order_relaxed), kRingSize);
            const int startIdx = writeIndex_.load (std::memory_order_relaxed) - total;

            for (int i = 0; i < total; ++i)
            {
                const auto& e = ring_[(startIdx + i) & (kRingSize - 1)];
                juce::String line;
                line << juce::String (e.timestampSec, 6) << ","
                     << juce::String (e.delaySamplesTarget, 4) << ","
                     << juce::String (e.smoothedDelaySamples, 4) << ","
                     << juce::String (e.feedback, 6) << ","
                     << juce::String (e.outL, 8) << ","
                     << juce::String (e.outR, 8) << ","
                     << juce::String (e.delayedL, 8) << ","
                     << juce::String (e.delayedR, 8) << ","
                     << juce::String (e.fbkL, 8) << ","
                     << juce::String (e.fbkR, 8) << ","
                     << juce::String (e.dcOutL, 8) << ","
                     << juce::String (e.dcOutR, 8) << ","
                     << juce::String (e.writeValL, 8) << ","
                     << juce::String (e.writeValR, 8) << ","
                     << e.writePos << ","
                     << juce::String (e.readPosF, 4) << "\n";
                stream->writeText (line, false, false, nullptr);
            }
            stream->flush();
            return true;
        }
        return false;
    }

    void enableDesktopAutoDump (const juce::String& filename = "echotr_dsp_debug.csv")
    {
        auto desktop = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
        autoDumpPath_ = desktop.getChildFile (filename).getFullPathName();
    }

    ~DspDebugLog()
    {
        if (autoDumpPath_.isNotEmpty() && writeIndex_.load (std::memory_order_relaxed) > 0)
            dumpToFile (autoDumpPath_);
    }

private:
    DspLogEntry ring_[kRingSize] {};
    std::atomic<int>  writeIndex_ { 0 };
    std::atomic<bool> armed_ { true };
    juce::String autoDumpPath_;
};

#define ECHOTR_DSP_LOG(log, tgt, sm, fb, oL, oR, dL, dR, fL, fR, dcOL, dcOR, wL, wR, wP, rP) \
    (log).logSample ((tgt), (sm), (fb), (oL), (oR), (dL), (dR), (fL), (fR), (dcOL), (dcOR), (wL), (wR), (wP), (rP))

#else // ECHOTR_DSP_DEBUG_LOG == 0

class DspDebugLog
{
public:
    void arm() noexcept {}
    void disarm() noexcept {}
    bool isArmed() const noexcept { return false; }
    void enableDesktopAutoDump (const juce::String& = {}) {}
    bool dumpToFile (const juce::String&) const { return false; }
};

#define ECHOTR_DSP_LOG(log, tgt, sm, fb, oL, oR, dL, dR, fL, fR, dcOL, dcOR, wL, wR, wP, rP)  ((void)0)

#endif // ECHOTR_DSP_DEBUG_LOG
