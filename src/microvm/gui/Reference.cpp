#include "Viewer.h"
namespace av::microvm::gui {
std::vector<guardian::graphics::Event> ReferenceInput() {
    using namespace guardian::graphics;
    return {{Input::Button, 0, 30, 65, 0, 1, 0}, // Parent is blocked by modal dialog.
            {Input::Key, 0, 0, 0, 13, 0, 0},     // Dismiss the dialog.
            {Input::Button, 0, 40, 65, 0, 1, 0},
            {Input::Key, 0, 0, 0, 'A', 0, 0}, // Paint owns focus; Notes must not change.
            {Input::Button, 0, 160, 145, 0, 1, 0},
            {Input::Key, 0, 0, 0, 'G', 0, 0},
            {Input::Blur, 0, 0, 0, 0, 0, 0},
            {Input::Key, 0, 0, 0, 'X', 0, 0},    // No focus: must be discarded.
            {Input::Button, 0, 310, 2, 0, 1, 0}, // Outside guest windows: discarded.
            {Input::Button, 0, 160, 145, 0, 1, 0},
            {Input::Key, 0, 0, 0, 'K', 0, 0},
            {Input::Close, 0, 0, 0, 0, 0, 0}};
}
} // namespace av::microvm::gui
