#pragma once

#include "../JuceHeader.h"
#include <memory>

/**
 * Base class for undoable actions.
 */
class UndoableAction
{
public:
    virtual ~UndoableAction() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual juce::String getName() const = 0;
    virtual bool ownsProjectContext() const { return false; }
    virtual bool belongsToContext(const juce::String&) const { return false; }

    // Most edits alter rendered audio. Layout-only actions can override this
    // so undo/redo mirrors the original action's processing behaviour.
    virtual bool requiresAudioResynthesis() const { return true; }
};
