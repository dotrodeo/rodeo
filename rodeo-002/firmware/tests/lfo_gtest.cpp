// Every LFO destination has to actually go somewhere.
//
// "Space mix" was on the destination list from the start, was offered on the
// panel and stored in presets, and was wired to nothing at all: VoiceManager
// exposed the LFO value for the chain to use and no caller ever used it (found
// 2026-09-24). These tests exist so that cannot happen again quietly -- each
// destination must make an audible difference, and the test walks the whole
// enum, so a new one added without wiring fails here.

#include "gtest/gtest.h"

#include <cmath>
#include <memory>
#include <vector>

#include "lfo_dest.h"
#include "texture_chain.h"
#include "voice.h"

using namespace rodeo002;
using namespace rodeo002::fx;

namespace
{
constexpr float  kSr    = 48000.f;
constexpr size_t kBlock = 48;

} // namespace

// Renders the same patch twice -- LFO off, then LFO at full depth -- and asks
// how different the result is. A destination that goes nowhere gives zero.
double DifferenceFor(LfoDest dest)
{
    std::vector<float> a, b;
    for(int pass = 0; pass < 2; pass++)
    {
        std::vector<float>                 gl(Space::kGrainLen), gr(Space::kGrainLen);
        std::vector<float>                 pl(Space::kPreLen), pr(Space::kPreLen);
        std::unique_ptr<daisysp::ReverbSc> rv(new daisysp::ReverbSc);
        std::unique_ptr<TextureChain>      chain(new TextureChain);
        std::unique_ptr<VoiceManager>      vm(new VoiceManager);
        chain->Init(kSr, {gl.data(), gr.data(), pl.data(), pr.data(), rv.get()});
        vm->Init(kSr, kBlock);

        Patch p;
        p.lfo_dest  = dest;
        p.lfo_depth = pass == 0 ? 0.f : 1.f;
        p.lfo_rate  = 0.55f;
        p.attack    = 0.f;
        p.sustain   = 1.f;
        p.cutoff    = 0.5f;
        p.resonance = 0.4f;
        p.harmonics = 0.5f;
        // NOT morph 0.5. On the virtual-analog engine that is exactly the
        // waveshape crossfade midpoint, where pulse width -- which is what
        // Timbre drives -- cancels out completely, so a Timbre destination
        // would look unwired when it is merely inaudible there.
        p.morph = 0.3f;
        p.amount[TextureChain::kSpace]     = 0.4f;
        p.amount[TextureChain::kCharacter] = 0.4f;
        p.type[TextureChain::kCharacter]   = Character::kDrive;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        vm->ApplyPatch(p);
        for(int s = 0; s < TextureChain::kNumSlots; s++)
        {
            chain->SetType(s, p.type[s]);
            chain->SetAmount(s, p.amount[s]);
        }
        chain->SnapToTargets();
        vm->NoteOn(60, 0.8f);

        std::vector<float>& dst = pass == 0 ? a : b;
        float               mix[kBlock], l[kBlock], r[kBlock];
        for(int i = 0; i < static_cast<int>(kSr * 1.5f / kBlock); i++)
        {
            vm->Render(mix, kBlock);
            for(size_t j = 0; j < kBlock; j++)
                l[j] = r[j] = mix[j];
            ApplyLfoToChain(*chain, p, vm->LfoValue());
            chain->ProcessBlock(l, r, kBlock);
            for(size_t j = 0; j < kBlock; j++)
                dst.push_back(l[j]);
        }
    }
    double diff = 0, ref = 0;
    for(size_t i = a.size() / 3; i < a.size(); i++) // past the settling
    {
        diff += std::fabs(a[i] - b[i]);
        ref += std::fabs(a[i]);
    }
    return ref > 0 ? diff / ref : 0.0;
}

TEST(Lfo, EveryDestinationChangesTheSound)
{
    for(int d = 0; d < static_cast<int>(LfoDest::kCount); d++)
    {
        const LfoDest dest = static_cast<LfoDest>(d);
        SCOPED_TRACE(LfoDestName(dest));
        EXPECT_GT(DifferenceFor(dest), 0.02)
            << "this destination is wired to nothing -- the LFO does not reach it";
    }
}

TEST(Lfo, ChainDestinationsMoveTheSlotTheyName)
{
    EXPECT_TRUE(LfoDestIsChain(LfoDest::kSpaceMix));
    EXPECT_TRUE(LfoDestIsChain(LfoDest::kCharacterAmt));
    EXPECT_FALSE(LfoDestIsChain(LfoDest::kCutoff));
    EXPECT_FALSE(LfoDestIsChain(LfoDest::kPitch));
}

// The stored value is an index, so the order is a compatibility contract: a new
// destination inserted in the middle would silently re-point every saved patch.
TEST(Lfo, TheDestinationOrderIsTheOnePresetsWereSavedWith)
{
    EXPECT_EQ(static_cast<int>(LfoDest::kPitch), 0);
    EXPECT_EQ(static_cast<int>(LfoDest::kTimbre), 1);
    EXPECT_EQ(static_cast<int>(LfoDest::kMorph), 2);
    EXPECT_EQ(static_cast<int>(LfoDest::kCutoff), 3);
    EXPECT_EQ(static_cast<int>(LfoDest::kAmp), 4);
    EXPECT_EQ(static_cast<int>(LfoDest::kSpaceMix), 5);
}

// The mod wheel's destinations, held to the same rule as the LFO's: a
// destination that goes nowhere is a lie the panel tells.
TEST(Lfo, EveryModWheelDestinationChangesTheSound)
{
    for(int d = 0; d < static_cast<int>(ModDest::kCount); d++)
    {
        const ModDest dest = static_cast<ModDest>(d);
        SCOPED_TRACE(ModDestName(dest));
        std::vector<float> out[2];
        for(int pass = 0; pass < 2; pass++)
        {
            std::vector<float>                 gl(Space::kGrainLen), gr(Space::kGrainLen);
            std::vector<float>                 pl(Space::kPreLen), pr(Space::kPreLen);
            std::unique_ptr<daisysp::ReverbSc> rv(new daisysp::ReverbSc);
            std::unique_ptr<TextureChain>      chain(new TextureChain);
            std::unique_ptr<VoiceManager>      vm(new VoiceManager);
            chain->Init(kSr, {gl.data(), gr.data(), pl.data(), pr.data(), rv.get()});
            vm->Init(kSr, kBlock);

            Patch p;
            p.mod_dest  = dest;
            p.lfo_depth = 0.f; // so LFO depth only moves if the wheel moves it
            p.lfo_rate  = 0.6f;
            p.attack    = 0.f;
            p.sustain   = 1.f;
            p.cutoff    = 0.5f;
            p.morph     = 0.3f;
            p.amount[TextureChain::kSpace]     = 0.4f;
            p.amount[TextureChain::kCharacter] = 0.4f;
            p.type[TextureChain::kCharacter]   = Character::kDrive;
            p.type[TextureChain::kSpace]       = Space::kEcho;
            vm->ApplyPatch(p);
            for(int s = 0; s < TextureChain::kNumSlots; s++)
            {
                chain->SetType(s, p.type[s]);
                chain->SetAmount(s, p.amount[s]);
            }
            chain->SnapToTargets();
            vm->SetModWheel(pass == 0 ? 0.f : 1.f);
            vm->NoteOn(60, 0.8f);

            float mix[kBlock], l[kBlock], r[kBlock];
            for(int i = 0; i < static_cast<int>(kSr * 1.0f / kBlock); i++)
            {
                vm->Render(mix, kBlock);
                for(size_t j = 0; j < kBlock; j++)
                    l[j] = r[j] = mix[j];
                ApplyLfoToChain(*chain, p, vm->LfoValue());
                ApplyModWheelToChain(*chain, p, vm->ModWheel());
                chain->ProcessBlock(l, r, kBlock);
                for(size_t j = 0; j < kBlock; j++)
                    out[pass].push_back(l[j]);
            }
        }
        double diff = 0, ref = 0;
        for(size_t i = out[0].size() / 3; i < out[0].size(); i++)
        {
            diff += std::fabs(out[0][i] - out[1][i]);
            ref += std::fabs(out[0][i]);
        }
        EXPECT_GT(ref > 0 ? diff / ref : 0.0, 0.02)
            << "the mod wheel does not reach this destination";
    }
}
