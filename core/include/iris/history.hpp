// IW4Iris — Cinematic Studio for IW4x
// Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.
//
// Snapshot-based undo / redo. Projects are small (kilobytes), so storing a
// copy per step is simple and robust: every step restores exactly the state
// it recorded, whatever the edit was.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace iris
{
    template <typename T>
    class History
    {
       public:
        explicit History(std::size_t limit = 256) : limit_(limit < 2 ? 2 : limit) {}

        // Forgets everything and starts from `state`.
        void Reset(const T& state)
        {
            entries_.clear();
            entries_.push_back({state, "Initial state"});
            cursor_ = 0;
        }

        // Records the state after an edit. With `merge`, an edit with the same
        // label as the newest step replaces it (one undo step for a whole
        // slider drag or gizmo move).
        void Commit(const T& state, std::string label, bool merge = false)
        {
            if (entries_.empty())
            {
                Reset(state);
                return;
            }
            entries_.resize(cursor_ + 1);  // drop the redo branch
            if (merge && cursor_ > 0 && entries_[cursor_].label == label)
            {
                entries_[cursor_].state = state;
                return;
            }
            entries_.push_back({state, std::move(label)});
            ++cursor_;
            while (entries_.size() > limit_)
            {
                entries_.erase(entries_.begin());
                --cursor_;
            }
        }

        bool CanUndo() const { return cursor_ > 0; }
        bool CanRedo() const { return cursor_ + 1 < entries_.size(); }

        // The state to apply, or null when there is nothing to undo / redo.
        const T* Undo()
        {
            if (!CanUndo())
                return nullptr;
            --cursor_;
            return &entries_[cursor_].state;
        }

        const T* Redo()
        {
            if (!CanRedo())
                return nullptr;
            ++cursor_;
            return &entries_[cursor_].state;
        }

        std::string UndoLabel() const { return CanUndo() ? entries_[cursor_].label : std::string(); }
        std::string RedoLabel() const { return CanRedo() ? entries_[cursor_ + 1].label : std::string(); }
        std::size_t Size() const { return entries_.size(); }
        std::size_t Cursor() const { return cursor_; }
        const T* Current() const { return entries_.empty() ? nullptr : &entries_[cursor_].state; }

        // Labels from the oldest to the newest step.
        std::vector<std::string> Labels() const
        {
            std::vector<std::string> out;
            for (const auto& e : entries_)
                out.push_back(e.label);
            return out;
        }

       private:
        struct Entry
        {
            T state;
            std::string label;
        };
        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;
        std::size_t limit_;
    };
}  // namespace iris
