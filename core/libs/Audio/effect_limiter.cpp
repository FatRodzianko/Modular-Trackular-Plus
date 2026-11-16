#include "effect_limiter.h"
#include "ampLogarythmicValues.h"
#include "masterfxDsp.h" // MT:
#include "mtStructs.h"
#include "util_math.h"
#include <cmath>

int16_t zeroblock[AUDIO_BLOCK_SAMPLES];

AUDIO_CORE_FLASH void AudioEffectLimiter::begin()
{
    setAttack(mtProject.values.limiterAttack);
    setRelease(mtProject.values.limiterRelease);
    setThreshold(mtProject.values.limiterThreshold);
    isSideChainEnable = (bool)mtProject.values.limiterSidechain;
    isAverageGain = false;

    memset(inputStorageBuffer, 0, 10);
#if MT_HAS_MASTERFX // MT: the dynamics slot's plugin starts over too (main loop: resetLimiter, init)
    if (this == mods::masterfx::limiters[0])
    {
        mods::masterfx::dynamicsReset();
    }
#endif
}

AUDIO_CORE_FLASH void AudioEffectLimiter::setThreshold(uint16_t ts)
{
    uint8_t quantizedThresholdIndex = mapClamped<int>(ts, 0, MAX_SIGNED_16BIT, 0, 100);
    threshold = limiterThresholdValues[quantizedThresholdIndex];
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}
AUDIO_CORE_FLASH void AudioEffectLimiter::setAttack(uint16_t a)
{
#if MT_HAS_MASTERFX // MT:
    modsAttack = a;
#endif
    if (mtProject.values.limiterMode == limiterModeSubtle)
        attackTime = 1 - expf(-2.2f * (1000.0f / (44100.0f * a)));
    else if (mtProject.values.limiterMode == limiterModeHard)
        attackTime = a / 1000.0f;
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}
AUDIO_CORE_FLASH void AudioEffectLimiter::setRelease(float r)
{
#if MT_HAS_MASTERFX // MT:
    modsRelease = r;
#endif
    if (mtProject.values.limiterMode == limiterModeSubtle)
        releaseTime = 1 - expf(-2.2f * (1000.0f / (44100.0f * r)));
    else if (mtProject.values.limiterMode == limiterModeHard)
        releaseTime = r / 1000.0f;
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}

AUDIO_CORE_FLASH void AudioEffectLimiter::setSidechainEnable(bool isEnable)
{
    isSideChainEnable = isEnable;
#ifdef AUDIO_NULL_IS_SILENCE
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    relaxed = false;
#endif
}

AUDIO_CORE_FLASH float AudioEffectLimiter::getAverageGainChange()
{
    isAverageGain = false;
    return averageGain;
}
AUDIO_CORE_FLASH bool AudioEffectLimiter::isAvailableGainToGet()
{
    return isAverageGain;
}

AUDIO_CORE_RAM void AudioEffectLimiter::update(void)
{
    if (isBlocked)
        return;

#if MT_HAS_MASTERFX // MT: the dynamics slot: the left object runs both sides, the right one has nothing left
    if (mods::masterfx::activeDynamics != 0 && mods::masterfx::limiters[1] != nullptr)
    {
        if (this == mods::masterfx::limiters[0])
        {
            modsUpdate();
        }
        return;
    }
#endif

    if (isSideChainEnable)
        processSidechain();
    else
        processMainStream();
}

#ifdef AUDIO_NULL_IS_SILENCE
// The peak detector is 0 and nothing is left in the look-ahead delay (the lines of the sidechain mode included). With silent input the
// output is then zero whatever the gain does.
AUDIO_CORE_RAM bool AudioEffectLimiter::isSteadyOnSilence(bool sidechain) const
{
    if (xpeak != 0)
        return false;
    for (uint8_t i = 0; i < LIMITER_DELAY; i++)
    {
        if (inputStorageBuffer[i] != 0 || (sidechain && outputStorageBuffer[i] != 0))
            return false;
    }
    return true;
}
#endif

AUDIO_CORE_RAM void AudioEffectLimiter::processSidechain()
{
    audio_block_t* inputBlock;
    audio_block_t* sidechainBlock;
    int16_t* inputData;
    int16_t* outputData;

    inputBlock = receiveWritable(0);
    sidechainBlock = receiveReadOnly(1);
#ifdef AUDIO_NULL_IS_SILENCE
    bool silentIn = false;
    float gainBefore = 0.0f;
#endif
    if (!inputBlock)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        // Silence in: the limiter still has to run (gain release, look-ahead delay, sidechain) until a block of silence leaves its whole
        // state as it found it (relaxed). From then on every block of silence gives the same state, the same zero output and the same
        // meter value, so it is skipped.
        if (!sidechainBlock && relaxed)
        {
            isAverageGain = true;
            return;
        }
        inputBlock = allocate();
        if (inputBlock)
        {
            memset(inputBlock->data, 0, sizeof(inputBlock->data));
            silentIn = !sidechainBlock && isSteadyOnSilence(true);
            gainBefore = g;
        }
        else
#endif
        {
            if (sidechainBlock)
                release(sidechainBlock);
            return;
        }
    }

#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = false;
#endif
    inputData = sidechainBlock ? sidechainBlock->data : zeroblock;
    outputData = inputBlock->data;

    {
        // State in locals and the look-ahead delay in registers, written back once. While the peak is at or under the threshold
        // threshold / xpeak is at least 1 and f is clamped to 1 anyway (xpeak 0 gives inf or NaN, f is 1 there too), so the divide is skipped.
        // Same results bit for bit as the member version.
        uint16_t lxpeak = xpeak;
        uint16_t la = a;
        float lg = g, lcoeff = coeff, lf = f, lsum = sumGain;
        const float att = attackTime, rel = releaseTime;
        const uint16_t thr = threshold;
        const float thrf = (float)thr;
        int16_t i0 = inputStorageBuffer[0], i1 = inputStorageBuffer[1], i2 = inputStorageBuffer[2], i3 = inputStorageBuffer[3],
                i4 = inputStorageBuffer[4];
        int16_t o0 = outputStorageBuffer[0], o1 = outputStorageBuffer[1], o2 = outputStorageBuffer[2], o3 = outputStorageBuffer[3],
                o4 = outputStorageBuffer[4];
        static_assert(LIMITER_DELAY == 5, "the look-ahead delay is kept in five registers");
        for (uint8_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
        {
            const int16_t in = inputData[n];
            const int16_t out = outputData[n];
            la = (in < 0) ? -in : in;
            lcoeff = (la > lxpeak) ? att : rel;
            lxpeak = (1 - lcoeff) * lxpeak + lcoeff * la;
            if (lxpeak <= thr)
                lf = 1.0f;
            else
            {
                const float localf = thrf / (float)lxpeak;
                lf = (localf < 1.0f) ? localf : 1.0f;
            }
            lcoeff = (lf < lg) ? att : rel;
            lg = (1 - lcoeff) * lg + lcoeff * lf;
            outputData[n] = lg * o4 + (1 - lg) * in;
            lsum += lg;
            i4 = i3; i3 = i2; i2 = i1; i1 = i0; i0 = in;
            o4 = o3; o3 = o2; o2 = o1; o1 = o0; o0 = out;
        }
        inputStorageBuffer[0] = i0; inputStorageBuffer[1] = i1; inputStorageBuffer[2] = i2; inputStorageBuffer[3] = i3;
        inputStorageBuffer[4] = i4;
        outputStorageBuffer[0] = o0; outputStorageBuffer[1] = o1; outputStorageBuffer[2] = o2; outputStorageBuffer[3] = o3;
        outputStorageBuffer[4] = o4;
        xpeak = lxpeak; a = la; g = lg; coeff = lcoeff; f = lf; sumGain = lsum;
    }

    averageGain = sumGain / AUDIO_BLOCK_SAMPLES;
    sumGain = 0.0f;
    isAverageGain = true;
#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = silentIn && g == gainBefore && isSteadyOnSilence(true);
#endif

    transmit(inputBlock, 0);
    release(inputBlock);
    if (sidechainBlock)
        release(sidechainBlock);
}
AUDIO_CORE_RAM void AudioEffectLimiter::processMainStream()
{
    audio_block_t* inputBlock;
    audio_block_t* sidechainBlock;
    int16_t* inputData;

    inputBlock = receiveWritable(0);
    sidechainBlock = receiveReadOnly(1);
#ifdef AUDIO_NULL_IS_SILENCE
    bool silentIn = false;
    float gainBefore = 0.0f;
#endif
    if (!inputBlock)
    {
#ifdef AUDIO_NULL_IS_SILENCE
        if (relaxed)
        {
            if (sidechainBlock)
                release(sidechainBlock);
            isAverageGain = true;
            return;
        }
        inputBlock = allocate();
        if (inputBlock)
        {
            memset(inputBlock->data, 0, sizeof(inputBlock->data));
            silentIn = isSteadyOnSilence(false);
            gainBefore = g;
        }
        else
#endif
        {
            if (sidechainBlock)
                release(sidechainBlock);
            return;
        }
    }

    if (sidechainBlock)
    {
        release(sidechainBlock);
        sidechainBlock = nullptr;
    }
#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = false;
#endif
    inputData = inputBlock->data;

    {
        // Same as the sidechain loop: locals, the delay in registers, no divide at or under the threshold.
        uint16_t lxpeak = xpeak;
        uint16_t la = a;
        float lg = g, lcoeff = coeff, lf = f, lsum = sumGain;
        const float att = attackTime, rel = releaseTime;
        const uint16_t thr = threshold;
        const float thrf = (float)thr;
        int16_t d0 = inputStorageBuffer[0], d1 = inputStorageBuffer[1], d2 = inputStorageBuffer[2], d3 = inputStorageBuffer[3],
                d4 = inputStorageBuffer[4];
        static_assert(LIMITER_DELAY == 5, "the look-ahead delay is kept in five registers");
        for (uint8_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
        {
            const int16_t in = inputData[n];
            la = (in < 0) ? -in : in;
            lcoeff = (la > lxpeak) ? att : rel;
            lxpeak = (1 - lcoeff) * lxpeak + lcoeff * la;
            if (lxpeak <= thr)
                lf = 1.0f;
            else
            {
                const float localf = thrf / (float)lxpeak;
                lf = (localf < 1.0f) ? localf : 1.0f;
            }
            lcoeff = (lf < lg) ? att : rel;
            lg = (1 - lcoeff) * lg + lcoeff * lf;
            inputData[n] = lg * d4;
            lsum += lg;
            d4 = d3; d3 = d2; d2 = d1; d1 = d0; d0 = in;
        }
        inputStorageBuffer[0] = d0; inputStorageBuffer[1] = d1; inputStorageBuffer[2] = d2; inputStorageBuffer[3] = d3;
        inputStorageBuffer[4] = d4;
        xpeak = lxpeak; a = la; g = lg; coeff = lcoeff; f = lf; sumGain = lsum;
    }

    averageGain = sumGain / AUDIO_BLOCK_SAMPLES;
    sumGain = 0.0f;
    isAverageGain = true;
#ifdef AUDIO_NULL_IS_SILENCE
    relaxed = silentIn && g == gainBefore && isSteadyOnSilence(false);
#endif

    transmit(inputBlock, 0);
    release(inputBlock);
    if (sidechainBlock)
        release(sidechainBlock);
}

#if MT_HAS_MASTERFX // MT:
// The dynamics slot's plugin on the stereo master, from the left object (CE builds it first, and
// both after the mixer, so the right block and both keys are queued): the right object's blocks
// are taken here and sent on from it. The meter's gain is the plugin's, on both. CE's own state is
// kept as at power-up, so its limiter starts clean if it plays again.
AUDIO_CORE_RAM void AudioEffectLimiter::modsUpdate()
{
    AudioEffectLimiter* right = mods::masterfx::limiters[1];
    audio_block_t* leftBlock = receiveWritable(0);
    audio_block_t* leftKey = receiveReadOnly(1);
    audio_block_t* rightBlock = right->receiveWritable(0);
    audio_block_t* rightKey = right->receiveReadOnly(1);

#ifdef AUDIO_NULL_IS_SILENCE
    // A missing master block is silence (CE's 7482cc8). The plugin still runs on it, as CE's own
    // limiter does until it has relaxed: its level has to release over the silence, and Ducks'
    // gain has to follow the key, or the next sound starts with the state of the last one.
    // A made block the plugin leaves silent goes on as silence, so nothing is sent.
    bool madeLeft = false;
    bool madeRight = false;
    if (!leftBlock)
    {
        leftBlock = allocate();
        if (leftBlock)
        {
            memset(leftBlock->data, 0, sizeof(leftBlock->data));
            madeLeft = true;
        }
    }
    if (!rightBlock)
    {
        rightBlock = allocate();
        if (rightBlock)
        {
            memset(rightBlock->data, 0, sizeof(rightBlock->data));
            madeRight = true;
        }
    }
#endif

    float gain = 1.0f;
    if (leftBlock && rightBlock)
    {
        mods::masterfx::DynamicsKnobs knobs;
        knobs.attack = modsAttack;
        knobs.release = modsRelease;
        knobs.threshold = threshold * (1.0f / 32767.0f);
        knobs.keyOn = isSideChainEnable ? 1u : 0u;
        const int16_t* keyLeft = nullptr;
        const int16_t* keyRight = nullptr;
        if (isSideChainEnable)
        {
            keyLeft = leftKey ? leftKey->data : zeroblock;
            keyRight = rightKey ? rightKey->data : zeroblock;
        }
        gain = mods::masterfx::dynamicsProcess(knobs, leftBlock->data, rightBlock->data, keyLeft, keyRight);
    }

    AudioEffectLimiter* const pair[2] = { this, right };
    for (AudioEffectLimiter* l : pair)
    {
        l->averageGain = gain;
        l->sumGain = 0.0f;
        l->isAverageGain = true;
        l->g = 1.0f;
        l->xpeak = 0;
        for (uint8_t i = 0; i < LIMITER_DELAY; i++)
        {
            l->inputStorageBuffer[i] = 0;
            l->outputStorageBuffer[i] = 0;
        }
    }

#ifdef AUDIO_NULL_IS_SILENCE
    if (madeLeft && madeRight)
    {
        int32_t all = 0;
        for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            all |= leftBlock->data[i] | rightBlock->data[i];
        }
        if (all == 0)
        {
            release(leftBlock);
            release(rightBlock);
            leftBlock = nullptr;
            rightBlock = nullptr;
        }
    }
#endif
    if (leftBlock)
    {
        transmit(leftBlock, 0);
        release(leftBlock);
    }
    if (rightBlock)
    {
        right->transmit(rightBlock, 0);
        release(rightBlock);
    }
    if (leftKey)
        release(leftKey);
    if (rightKey)
        release(rightKey);
}
#endif

AUDIO_CORE_FLASH void AudioEffectLimiter::blockUpdate()
{
    isBlocked = true;
}
AUDIO_CORE_FLASH void AudioEffectLimiter::unblockUpdate()
{
    isBlocked = false;
}
