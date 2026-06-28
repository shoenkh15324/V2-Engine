#include "app/tui/tui_app.hpp"

int main(int argc, char** argv){
    TuiApp app;
    app.open();
    app.run();
    return 0;
}
