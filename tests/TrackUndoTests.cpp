#include "../Source/Undo/PitchUndoManager.h"
#include "../Source/Undo/ContextUndoAction.h"
#include "TestAssert.h"
#include <iostream>

class Edit final : public UndoableAction {
public:
  Edit(int& value, int oldValue, int newValue) : value(value), oldValue(oldValue), newValue(newValue) {}
  void undo() override { value = oldValue; }
  void redo() override { value = newValue; }
  juce::String getName() const override { return "Edit"; }
private:
  int& value;
  int oldValue, newValue;
};

int main() {
  PitchUndoManager history;
  int a = 1, b = 2, bound = 0;
  history.addAction(std::make_unique<ContextUndoAction>(std::make_unique<Edit>(a, 0, 1),
      "a", "Clip A", [&] { bound = 1; return true; }));
  history.addAction(std::make_unique<ContextUndoAction>(std::make_unique<Edit>(b, 0, 2),
      "b", "Clip B", [&] { bound = 2; return true; }));
  CHECK(history.getUndoName().contains("Clip B"));
  history.undo();
  CHECK(a == 1 && b == 0 && bound == 2);
  // Deleting A removes only its actions, including actions on the redo stack.
  history.removeContext("a");
  CHECK(!history.canUndo() && history.canRedo());
  history.redo();
  CHECK(b == 2);
  // Actions with independent context (track-wide operations) survive removing
  // a local clip context, and can restore their surviving project parts.
  int shared = 3;
  history.addAction(std::make_unique<Edit>(shared, 0, 3));
  history.removeContext("b");
  CHECK(history.canUndo());
  history.undo();
  CHECK(shared == 0 && b == 2);
  history.redo();
  CHECK(shared == 3);
  // Invalidated clip projects must never dereference an action's note pointers.
  history.addAction(std::make_unique<ContextUndoAction>(std::make_unique<Edit>(a, 0, 9),
      "deleted", "Deleted", [] { return false; }));
  history.undo();
  CHECK(a == 1);
  std::cout << "Track undo tests passed\n";
}
