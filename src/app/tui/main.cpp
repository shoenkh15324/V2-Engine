#include "tui_app.hpp"
#include "core/common/util/return.hpp"

int main(int argc, char** argv){
    (void)argc; (void)argv;
    TuiApp app;
    if(app.open() != Ok) return 1;
    app.run();
    return app.close();
}
