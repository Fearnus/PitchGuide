#pragma once

#include "../Utils/ReferenceMatch.h"
#include "UndoableAction.h"
#include <utility>
#include <vector>

/**
 * Undo/redo for Match Reference: swaps the matched notes between the states
 * captured before the popup opened and after it was confirmed, together with
 * what the project remembers about matching (settings and pre-match pitch).
 */
class ReferenceMatchAction final : public UndoableAction
{
public:
    ReferenceMatchAction(Project *projectToEdit,
                         std::vector<ReferenceMatch::NoteState> beforeStates,
                         std::vector<ReferenceMatch::NoteState> afterStates,
                         ReferenceMatchMemory memoryBefore,
                         ReferenceMatchMemory memoryAfter)
        : project(projectToEdit), before(std::move(beforeStates)),
          after(std::move(afterStates)), beforeMemory(std::move(memoryBefore)),
          afterMemory(std::move(memoryAfter))
    {
    }

    void undo() override
    {
        if (project == nullptr)
            return;
        ReferenceMatch::restore(*project, before);
        project->getReferenceMatch() = beforeMemory;
    }
    void redo() override
    {
        if (project == nullptr)
            return;
        ReferenceMatch::restore(*project, after);
        project->getReferenceMatch() = afterMemory;
    }
    juce::String getName() const override { return "Match Reference"; }

private:
    Project *project = nullptr;
    std::vector<ReferenceMatch::NoteState> before;
    std::vector<ReferenceMatch::NoteState> after;
    ReferenceMatchMemory beforeMemory;
    ReferenceMatchMemory afterMemory;
};
