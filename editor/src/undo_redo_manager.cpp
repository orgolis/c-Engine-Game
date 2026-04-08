#include "undo_redo_manager.h"

namespace schizo::editor {

void UndoRedoManager::ExecuteCommand(std::unique_ptr<EditorCommand> command) {
    if (!command) return;
    
    // Execute the command
    command->Execute();
    
    // Add to undo stack
    undo_stack_.push_back(std::move(command));
    
    // Clear redo stack when new command is executed
    redo_stack_.clear();
}

void UndoRedoManager::Undo() {
    if (undo_stack_.empty()) return;
    
    // Get last command from undo stack
    auto command = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    
    // Undo it
    command->Undo();
    
    // Move to redo stack
    redo_stack_.push_back(std::move(command));
}

void UndoRedoManager::Redo() {
    if (redo_stack_.empty()) return;
    
    // Get last command from redo stack
    auto command = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    
    // Execute it
    command->Execute();
    
    // Move to undo stack
    undo_stack_.push_back(std::move(command));
}

std::string UndoRedoManager::GetUndoDescription() const {
    if (undo_stack_.empty()) return "Undo";
    return "Undo: " + undo_stack_.back()->GetDescription();
}

std::string UndoRedoManager::GetRedoDescription() const {
    if (redo_stack_.empty()) return "Redo";
    return "Redo: " + redo_stack_.back()->GetDescription();
}

void UndoRedoManager::Clear() {
    undo_stack_.clear();
    redo_stack_.clear();
}

} // namespace schizo::editor
