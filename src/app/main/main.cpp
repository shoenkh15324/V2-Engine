#include "app/main/main_app.hpp"
#include "core/common/log.hpp"

int main(int, char**){
    V2_LOG_INFO("Project Name: %s", V2_ENGINE_NAME);
    V2_LOG_INFO("Project Version: v%s", V2_ENGINE_VERSION);
    MainApp app;
    app.open();
    app.run();
    return 0;
}
