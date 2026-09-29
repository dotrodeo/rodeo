// The cost model has to match the hardware, or the voice cap is fiction.
//
// These tests pin it against measurements taken from the real board
// (rodeo-002/docs/bench_voices-2026-09-22-optimised.log, raw timer ticks,
// under the bootloader). If a DSP change makes the model drift, this fails and
// the benchmark needs re-running -- which is the point.

#include "gtest/gtest.h"

#include <memory>
#include <vector>

#include "cost_model.h"

using namespace rodeo002;
using fx::Space;
using fx::TextureChain;

namespace
{
// engine, voices, measured load with the chain bypassed
struct Point
{
    EngineId id;
    int      voices;
    float    measured;
};
const Point kMeasured[] = {
    {EngineId::kVirtualAnalog, 4, 13.5f}, {EngineId::kVirtualAnalog, 8, 26.9f},
    {EngineId::kFm2, 4, 24.9f},           {EngineId::kFm2, 8, 49.5f},
    {EngineId::kHarmonic, 4, 27.5f},      {EngineId::kHarmonic, 8, 54.8f},
    {EngineId::kVosim, 4, 35.2f},         {EngineId::kVosim, 8, 70.3f},
};

// The worst-case chain measured 35.4%: Warm + Phaser + Tape + Reverb.
constexpr float kWorstChain = 35.4f;
} // namespace

TEST(CostModel, PredictsMeasuredLoadWithinTwoPoints)
{
    for(const Point& p : kMeasured)
    {
        const float predicted = CostModel::PredictLoad(p.id, p.voices, 0.f);
        SCOPED_TRACE(testing::Message() << EngineName(p.id) << " x" << p.voices);
        EXPECT_NEAR(predicted, p.measured, 2.f);
    }
}

TEST(CostModel, PredictsTheWorstCaseChain)
{
    // Warm + Phaser + Tape + Reverb, summed from the per-type table.
    const float sum = CostModel::EffectCost(TextureChain::kCharacter, fx::Character::kWarm)
                      + CostModel::EffectCost(TextureChain::kMovement, fx::Movement::kPhaser)
                      + CostModel::EffectCost(TextureChain::kTexture, fx::Texture::kTape)
                      + CostModel::EffectCost(TextureChain::kSpace, Space::kReverb);
    EXPECT_NEAR(sum, kWorstChain, 1.f);
}

// The cap's whole job: whatever it allows must fit inside the budget.
TEST(CostModel, CappedVoicesAlwaysFitTheBudget)
{
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
    {
        const EngineId id = static_cast<EngineId>(e);
        for(float chain : {0.f, 10.f, kWorstChain})
        {
            const int   n    = CostModel::MaxVoices(id, chain);
            const float load = CostModel::PredictLoad(id, n, chain);
            // Copies, so the constexpr members are not odr-used (C++14).
            const float budget   = CostModel::kBudget;
            const int   max_poly = CostModel::kMaxPoly;
            SCOPED_TRACE(testing::Message()
                         << EngineName(id) << " chain " << chain << " -> " << n << " voices");
            EXPECT_GE(n, 1);
            EXPECT_LE(n, max_poly);
            // One voice of an expensive engine plus an expensive chain can
            // exceed the budget -- that is Overcommitted(), reported, not hidden.
            if(!CostModel::Overcommitted(id, chain))
                EXPECT_LE(load, budget);
        }
    }
}

TEST(CostModel, MatchesTheBenchmarksVoiceCounts)
{
    // From the log's summary (voices at 70% with the worst-case chain).
    EXPECT_NEAR(CostModel::MaxVoices(EngineId::kVirtualAnalog, kWorstChain), 8, 1);
    EXPECT_NEAR(CostModel::MaxVoices(EngineId::kVariableSaw, kWorstChain), 8, 1); // capped at 8
    EXPECT_NEAR(CostModel::MaxVoices(EngineId::kFm2, kWorstChain), 4, 1);
    EXPECT_NEAR(CostModel::MaxVoices(EngineId::kHarmonic, kWorstChain), 4, 1);
    EXPECT_NEAR(CostModel::MaxVoices(EngineId::kVosim, kWorstChain), 2, 1);
    // A light chain instead: Warm + Chorus + Crush + Echo.
    const float light = 3.0f + 3.6f + 1.9f + 7.8f;
    EXPECT_EQ(CostModel::MaxVoices(EngineId::kFm2, light), 8);
}

// There are no mono-only engines any more: String (16.43%) and Karplus
// (14.04%) were the last two, and they went on 2026-09-29. What replaced
// IsMonoOnly is this: every engine left is POLYPHONIC, so nothing overrides the
// voice mode you chose. Not every one reaches eight -- Vosim at 8.78% and ZOsc
// at 9.02% get seven on a bare chain -- but seven is polyphony, and the cap is
// the cost model's job rather than a property of the engine.
TEST(CostModel, EveryEngineIsPolyphonicOnABareChain)
{
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
    {
        const EngineId id = static_cast<EngineId>(e);
        SCOPED_TRACE(EngineName(id));
        EXPECT_GE(CostModel::MaxVoices(id, 0.f), 2)
            << "this engine is mono in all but name";
        EXPECT_LE(CostModel::VoiceCost(id), 9.02f)
            << "an engine dearer than ZOsc arrived; check it is still polyphonic";
    }
}

// Modal was dropped because one voice did not fit beside an expensive Space.
TEST(CostModel, NoEngineIsOvercommittedOnItsOwn)
{
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
        EXPECT_FALSE(CostModel::Overcommitted(static_cast<EngineId>(e), 0.f))
            << EngineName(static_cast<EngineId>(e));
}

// A live chain's cost must agree with the table it was configured from.
TEST(CostModel, ChainCostFollowsTheConfiguredSlots)
{
    std::vector<float>                 gl(Space::kGrainLen), gr(Space::kGrainLen);
    std::vector<float>                 pl(Space::kPreLen), pr(Space::kPreLen);
    std::unique_ptr<daisysp::ReverbSc> rev(new daisysp::ReverbSc);
    std::unique_ptr<TextureChain>      chain(new TextureChain);
    chain->Init(48000.f, {gl.data(), gr.data(), pl.data(), pr.data(), rev.get()});

    for(int s = 0; s < TextureChain::kNumSlots; s++)
        chain->SetBypass(s, true);
    EXPECT_FLOAT_EQ(CostModel::ChainCost(*chain), 0.f);

    chain->SetBypass(TextureChain::kSpace, false);
    chain->SetType(TextureChain::kSpace, Space::kReverb);
    EXPECT_NEAR(CostModel::ChainCost(*chain), 20.6f, 0.01f);

    chain->SetBypass(TextureChain::kCharacter, false);
    chain->SetType(TextureChain::kCharacter, fx::Character::kWarm);
    EXPECT_NEAR(CostModel::ChainCost(*chain), 23.6f, 0.01f);
}
