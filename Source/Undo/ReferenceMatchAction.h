#pragma once

#include "../Utils/ReferenceMatch.h"
#include "UndoableAction.h"
#include <utility>
#include <vector>

/**
 * Undo/redo for Match Reference: swaps the matched notes between the states
 * captured before the popup opened and after it was confirmed.
 */
class ReferenceMatchAction final : public UndoableAction
{
public:
    ReferenceMatchAction(Project *projectToEdit,
                         std::vector<ReferenceMatch::NoteState> beforeStates,
                         std::vector<ReferenceMatch::NoteState> afterStates)
        : project(projectToEdit), before(std::move(beforeStates)),
          after(std::move(afterStates))
    {
    }

    void undo() override
    {
        if (project != nullptr)
            ReferenceMatch::restore(*project, before);
    }
    void redo() override
    {
        if (project != nullptr)
            ReferenceMatch::restore(*project, after);
    }
    juce::String getName() const override { return "Match Reference"; }

private:
    Project *project = nullptr;
    std::vector<ReferenceMatch::NoteState> before;
    std::vector<ReferenceMatch::NoteState> after;
};
