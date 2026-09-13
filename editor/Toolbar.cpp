#include "Toolbar.hpp"

namespace zed
{

ToolbarAction Toolbar::draw(ig::Context& ui, const ToolbarState& state)
{
    ToolbarAction action = ToolbarAction::None;

    const ig::Vec2 menuOrigin = ui.cursor();
    const float menuHeight = ui.theme().menuBarHeight;
    if (ui.beginMenuBar(ig::Rect(menuOrigin.x, menuOrigin.y, ui.availableWidth(), menuHeight)))
    {
        if (ui.beginMenu("File"))
        {
            if (ui.menuItem("New")) action = ToolbarAction::New;
            if (ui.menuItem("Open")) action = ToolbarAction::Open;
            if (ui.menuItem("Save")) action = ToolbarAction::Save;
            if (!state.recentFiles.empty() && ui.beginSubMenu("Recent Files"))
            {
                for (const ig::String& path : state.recentFiles)
                {
                    if (ui.menuItem(path.c_str()))
                    {
                        action = ToolbarAction::OpenRecent;
                        recentFilePicked_ = path;
                    }
                }
                ui.endSubMenu();
            }
            ui.endMenu();
        }
        if (ui.beginMenu("Edit"))
        {
            if (ui.menuItem("Cut")) action = ToolbarAction::Cut;
            if (ui.menuItem("Copy")) action = ToolbarAction::Copy;
            if (ui.menuItem("Paste")) action = ToolbarAction::Paste;
            ui.menuSeparator();
            if (ui.menuItem("Find")) action = ToolbarAction::Find;
            if (ui.menuItem("Find & Replace")) action = ToolbarAction::FindReplace;
            ui.endMenu();
        }
        if (ui.beginMenu("Program"))
        {
            if (state.isRunning)
            {
                if (ui.menuItem("Stop program (F5)")) action = ToolbarAction::Stop;
            }
            else if (ui.menuItem("Run program (F5)")) action = ToolbarAction::Play;
            ui.endMenu();
        }
        if (ui.beginMenu("Theme")) {
            if (ui.menuItem("Blitz classic")) action = ToolbarAction::ThemeClassic;
            if (ui.menuItem("Light")) action = ToolbarAction::ThemeLight;
            if (ui.menuItem("Dark")) action = ToolbarAction::ThemeDark;
            ui.endMenu();
        }
        ui.endMenuBar();
    }
    // beginMenuBar/endMenuBar draws in the given Rect but doesn't advance
    // the layout cursor itself (see raylib_dock_demo's dockInset for the
    // same pattern) - without this, the button row below would overlap it.
    const float stripWidth = ui.availableWidth();
    const ig::Color dividerColor = ui.theme().borderColor;
    const float menuDividerY = menuOrigin.y + menuHeight + 1.0f;
    ui.drawLine(ig::Vec2(menuOrigin.x, menuDividerY),
                ig::Vec2(menuOrigin.x + stripWidth, menuDividerY), dividerColor, 1.0f);

    ui.setCursor(ig::Vec2(menuOrigin.x, menuOrigin.y + menuHeight + 5.0f));

    struct Item { const char* tip; int icon; ToolbarAction action; bool gap; };
    const Item items[] = {
        {"New (Ctrl+N)",0,ToolbarAction::New,false},
        {"Open (Ctrl+O)",1,ToolbarAction::Open,false},
        {"Save (Ctrl+S)",2,ToolbarAction::Save,false},
        {"Close file (Ctrl+F4)",3,ToolbarAction::Close,false},
        {"Cut (Ctrl+X)",4,ToolbarAction::Cut,true},
        {"Copy (Ctrl+C)",5,ToolbarAction::Copy,false},
        {"Paste (Ctrl+V)",6,ToolbarAction::Paste,false},
        {"Find (Ctrl+F)",7,ToolbarAction::Find,true},
        {state.isRunning ? "Stop program (F5)" : "Run program (F5)", 8,
         state.isRunning ? ToolbarAction::Stop : ToolbarAction::Play, true}
    };
    const float y = ui.cursor().y;
    float x = menuOrigin.x;
    for (const auto& item : items) {
        if (item.gap) x += 8;
        const ig::Rect rect(x,y,24,24);
        if (ui.invisibleButton(item.tip, rect)) action = item.action;
        if (ui.isHovered(item.tip,rect)) ui.drawRect(rect,ui.theme().borderColor);
        ui.image(icons,ig::Rect(x+4,y+4,16,16),
            ig::Vec2(item.icon/12.0f,0), ig::Vec2((item.icon+1)/12.0f,1));
        ui.tooltip(item.tip);
        x += 25;
    }
    const float rowDividerY = y + 26.0f;
    ui.drawLine(ig::Vec2(menuOrigin.x, rowDividerY),
                ig::Vec2(menuOrigin.x + stripWidth, rowDividerY), dividerColor, 1.0f);

    if (ui.shortcut(ig::KeyCode::N)) action = ToolbarAction::New;
    if (ui.shortcut(ig::KeyCode::O)) action = ToolbarAction::Open;
    if (ui.shortcut(ig::KeyCode::S)) action = ToolbarAction::Save;
    if (ui.shortcut(ig::KeyCode::F)) action = ToolbarAction::Find;
    if (ui.shortcut(ig::KeyCode::F4)) action = ToolbarAction::Close;
    if (ui.isKeyPressed(ig::KeyCode::F5))
        action = state.isRunning ? ToolbarAction::Stop : ToolbarAction::Play;
    ui.setCursor(ig::Vec2(menuOrigin.x, y + 31));
    return action;
}

} // namespace zed
