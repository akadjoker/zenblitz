#include <igui/Gui.hpp>
#include <cassert>
#include <cmath>
#include <SDL.h>
#include <igui_sdl2/SdlBackend.hpp>
#include "SymbolIndex.hpp"
#include "Editor.hpp"

class Backend : public ig::Backend {
public:
    ig::TextMetrics measureText(ig::FontId,ig::StringView text,float size,float) override {
        ig::TextMetrics result; result.width=text.size()*size*0.5f; result.height=size; return result;
    }
    bool render(const ig::DrawData&) override { return true; }
};
int main() {
    Backend backend;
    ig::Context ui(backend);
    ui.beginFrame(ig::FrameInfo(800,600));
    assert(ui.beginMainWindow("layout"));
    ui.setCursor(ig::Vec2(0,80));
    assert(std::fabs(ui.availableHeight()-520)<0.01f);
    assert(ui.beginChild("panel",180,true,220));
    float before=ui.availableHeight();
    ui.spacing(20);
    assert(std::fabs(ui.availableHeight()-(before-20))<0.01f);
    ui.endChild();
    ui.endWindow(); ui.endFrame();
    assert(ui.availableHeight()==0);

    // Disabling folding must use direct rows, including previously collapsed text.
    for (bool folding : {false, true}) {
        ig::Context context(backend);
        ig::CodeEditorState code;
        code.setText("Type Player\n Field hiddenMarker\nEnd Type\nPrint 1\n");
        code.setHighlighterForFile("test.bb");
        code.toggleFoldAt(0);
        assert(code.isLineHidden(1));
        ig::CodeEditorOptions options;
        options.showFolding = folding;
        context.beginFrame(ig::FrameInfo(800,600));
        assert(context.beginMainWindow("source"));
        context.codeEditor("source",code,ig::Rect(0,0,700,500),options);
        context.endWindow();
        const auto& drawing = context.endFrame();
        const ig::String text(drawing.textBytes.data(),drawing.textBytes.size());
        assert((text.find("hiddenMarker") != ig::String::npos) == !folding);
    }

    {
        ig::CodeEditorState code;
        code.setText("Type Outer\n Type Inner\n Field x\n End Type\nEnd Type\nPrint 1");
        code.setHighlighterForFile("folds.bb");
        auto checkMap = [&]() {
            const auto& cached = code.visibleLines();
            size_t row=0;
            for (int line=0; line<code.lineCount(); ++line)
                if (!code.isLineHidden(line)) { assert(row<cached.size()); assert(cached[row++]==line); }
            assert(row==cached.size());
            assert(code.visibleLines().data()==cached.data());
        };
        checkMap(); code.foldAll(); checkMap();
        code.toggleFoldAt(0); checkMap();
        code.unfoldAll(); checkMap();
        code.insertText(0,0,"; heading\n"); checkMap();
        code.setText("Print 2"); checkMap();
        code.setHighlighter(nullptr); checkMap();
    }

    ig::syntax::BlitzHighlighter highlighter;
    using T=ig::syntax::SyntaxHighlighter::TokenType;
    auto comment=highlighter.highlightLine(0,"; player's position",0);
    assert(comment.spans.size()==1 && comment.spans[0].type==T::Comment);
    auto literal=highlighter.highlightLine(0,"Print \"a;b\" ; comment",0);
    bool string=false;
    for (const auto& span:literal.spans) if(span.type==T::String) string=true;
    assert(string && literal.spans.back().type==T::Comment);
    const auto symbols=zed::scanSymbols("; .fake\n.start\nType Player\nFunction Update()\n");
    assert(symbols.size()==3);
    assert(symbols[0].kind==zed::SymbolKind::Label && symbols[0].line==1);
    assert(symbols[1].kind==zed::SymbolKind::Type);
    assert(symbols[2].kind==zed::SymbolKind::Function);
    // Exercise document ownership, tab creation and closing the final tab.
    {
        zed::Editor editor;
        editor.openInitial(nullptr);
        const ig::KeyCode actions[]={ig::KeyCode::N,ig::KeyCode::F4,ig::KeyCode::F4};
        for (auto key:actions) {
            ui.pushEvent(ig::Event::keyDown(key,true));
            ui.beginFrame(ig::FrameInfo(800,600));
            editor.update(ui); ui.endFrame();
        }
    }
    const SDL_Keycode keys[]={SDLK_n,SDLK_o,SDLK_F4,SDLK_F5};
    const ig::KeyCode expected[]={ig::KeyCode::N,ig::KeyCode::O,ig::KeyCode::F4,ig::KeyCode::F5};
    for(int i=0;i<4;++i) {
        SDL_Event native; SDL_zero(native); native.type=SDL_KEYDOWN; native.key.keysym.sym=keys[i];
        ig::Event event; assert(ig::sdl2::translateEvent(native,event)); assert(event.key==expected[i]);
    }
}
