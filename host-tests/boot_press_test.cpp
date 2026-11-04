#include "boot_press.hpp"

#include <cassert>
#include <cstdint>

namespace {

BootAction release_after(BootPressClassifier& classifier, uint32_t start_ms,
                         uint32_t duration_ms) {
    assert(classifier.update(true, start_ms) == BootAction::None);
    return classifier.update(false, start_ms + duration_ms);
}

}  // namespace

int main() {
    BootPressClassifier classifier;

    assert(release_after(classifier, 0, 680) == BootAction::NextProfile);
    assert(release_after(classifier, 1'000, 699) == BootAction::NextProfile);

    assert(release_after(classifier, 2'000, 700) == BootAction::None);
    assert(release_after(classifier, 3'000, 1'499) == BootAction::None);

    assert(release_after(classifier, 5'000, 1'500) == BootAction::ToggleMods);
    assert(release_after(classifier, 7'000, 4'999) == BootAction::ToggleMods);

    assert(classifier.update(true, 13'000) == BootAction::None);
    assert(classifier.update(true, 17'999) == BootAction::None);
    assert(classifier.update(true, 18'000) == BootAction::OpenPortal);
    assert(classifier.update(true, 19'000) == BootAction::None);
    assert(classifier.update(false, 19'100) == BootAction::None);

    assert(release_after(classifier, 20'000, 200) == BootAction::NextProfile);
    assert(release_after(classifier, 21'000, 1'500) == BootAction::ToggleMods);
}
