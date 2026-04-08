#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>

namespace schizo::editor {

/**
 * @class EditorCommand
 * @brief Base class for editor commands that support undo/redo
 */
class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    
    /**
     * Execute the command
     */
    virtual void Execute() = 0;
    
    /**
     * Undo the command
     */
    virtual void Undo() = 0;
    
    /**
     * Get command description for UI
     */
    virtual std::string GetDescription() const = 0;
};

/**
 * @class FunctionCommand
 * @brief Simple command wrapping execute/undo function pairs
 */
class FunctionCommand : public EditorCommand {
public:
    FunctionCommand(std::function<void()> execute_fn,
                   std::function<void()> undo_fn,
                   const std::string& description)
        : execute_fn_(execute_fn), undo_fn_(undo_fn), description_(description) {}
    
    void Execute() override { if (execute_fn_) execute_fn_(); }
    void Undo() override { if (undo_fn_) undo_fn_(); }
    std::string GetDescription() const override { return description_; }

private:
    std::function<void()> execute_fn_;
    std::function<void()> undo_fn_;
    std::string description_;
};

/**
 * @class UndoRedoManager
 * @brief Manages command history for undo/redo functionality
 */
class UndoRedoManager {
public:
    /**
     * Execute a command and add to undo stack
     */
    void ExecuteCommand(std::unique_ptr<EditorCommand> command);
    
    /**
     * Undo last command
     */
    void Undo();
    
    /**
     * Redo last undone command
     */
    void Redo();
    
    /**
     * Check if undo is possible
     */
    bool CanUndo() const { return !undo_stack_.empty(); }
    
    /**
     * Check if redo is possible
     */
    bool CanRedo() const { return !redo_stack_.empty(); }
    
    /**
     * Get description of next undo operation
     */
    std::string GetUndoDescription() const;
    
    /**
     * Get description of next redo operation
     */
    std::string GetRedoDescription() const;
    
    /**
     * Clear all history
     */
    void Clear();
    
    /**
     * Get undo stack size
     */
    size_t GetUndoCount() const { return undo_stack_.size(); }

private:
    std::vector<std::unique_ptr<EditorCommand>> undo_stack_;
    std::vector<std::unique_ptr<EditorCommand>> redo_stack_;
};

} // namespace schizo::editor
