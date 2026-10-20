#include "hem/ui/Face.hpp"

namespace hem::ui {

EffectFeed::EffectFeed(hc::fx::Effect& e) noexcept
    : fx_(&e),
      spectrum_(dynamic_cast<rack::SpectrumSource*>(&e)),
      noise_(dynamic_cast<rack::NoiseProfileSource*>(&e)),
      eq_(dynamic_cast<rack::EliteEqView*>(&e)),
      analyzer_(dynamic_cast<rack::AnalyzerSource*>(&e)),
      clone_(dynamic_cast<rack::CloneSource*>(&e)) {}

float EffectFeed::before(int b) const { return spectrum_ ? spectrum_->spectrumBefore().level(b) : rack::kSpectrumFloorDb; }
float EffectFeed::after(int b) const { return spectrum_ ? spectrum_->spectrumAfter().level(b) : rack::kSpectrumFloorDb; }
void EffectFeed::setSpectrumActive(bool on) {
    if (!spectrum_) return;
    spectrum_->spectrumBefore().setActive(on);
    spectrum_->spectrumAfter().setActive(on);
}
float EffectFeed::noiseFloor(int b) const { return noise_ ? noise_->noiseFloorDb(b) : rack::kSpectrumFloorDb; }
float EffectFeed::removedDb() const { return noise_ ? noise_->removedDb() : 0.0f; }
float EffectFeed::dynamicGainDb(int band) const { return eq_ ? eq_->dynamicGainDb(band) : 0.0f; }
float EffectFeed::autoGainDb() const { return eq_ ? eq_->autoGainDb() : 0.0f; }
float EffectFeed::rtaLevel(int side, int b) const { return analyzer_ ? analyzer_->rtaLevel(side, b) : -120.0f; }
float EffectFeed::rtaMax(int side, int b) const { return analyzer_ ? analyzer_->rtaMax(side, b) : -120.0f; }
float EffectFeed::spdAngle(int b) const { return analyzer_ ? analyzer_->spdAngle(b) : 0.0f; }
float EffectFeed::spdLevel(int b) const { return analyzer_ ? analyzer_->spdLevel(b) : -120.0f; }

} // namespace hem::ui
