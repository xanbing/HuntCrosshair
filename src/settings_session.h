#pragma once
#include "core.h"
#include <functional>

namespace hc {
// Semantic, exact comparison. Pixel masks are compared by value; no padding or
// vector addresses participate. Two copied NaNs count as the same invalid value.
bool SettingsEqual(const Settings& a,const Settings& b) noexcept;

class SettingsSession {
public:
    using SaveCallback=std::function<bool(const Settings&)>;
    using RuntimeMutator=std::function<void(Settings&)>;

    SettingsSession()=default;
    explicit SettingsSession(const Settings& applied) { Begin(applied); }
    void Begin(const Settings& applied);
    Settings& Draft() { return draft_; }
    const Settings& Draft() const { return draft_; }
    const Settings& Applied() const { return applied_; }
    bool HasChanges() const noexcept { return !SettingsEqual(draft_,applied_); }

    // The baseline advances only after a successful save. A failed or empty
    // callback leaves both values unchanged. New edits made by a callback are
    // retained in Draft and remain dirty after its saved snapshot is committed.
    bool Apply(const SaveCallback& save);
    void Cancel();
    void ResetDefaults();

    // Runtime hotkeys alter the applied copy once. Only fields actually changed
    // by the mutator are then merged into Draft, preserving other pending edits.
    // If supplied, save is transactional too; without it this is memory-only.
    bool CommitRuntimeChange(const RuntimeMutator& change,const SaveCallback& save={});
private:
    Settings draft_,applied_;
};
}
