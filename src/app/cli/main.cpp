#include "app/cli/cli_app.hpp"

int main(int, char**){
    CliApp app;
    app.open();
    app.run();
    return 0;
}
