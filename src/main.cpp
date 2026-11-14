#include <string_view>

#include "app/App.hpp"
#include "core/PathUtil.hpp"

int main(int argc, char* argv[]) {
    vcut::App app;
    int maxFrames = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--smoke-test") {
            maxFrames = 5;
        } else if (vcut::pathFromUtf8(std::string(arg)).extension() == ".vcut") {
            app.openProjectFile(std::string(arg));
        } else {
            app.importFile(std::string(arg));
        }
    }

    return app.run(maxFrames);
}
